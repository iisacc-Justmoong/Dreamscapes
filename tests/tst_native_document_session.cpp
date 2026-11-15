#include "App/Native/NativeDocumentSession.h"
#include <Document/DocumentEditor.h>
#include <Document/CanvasSampling.h>
#include <Serialization/IiscCodec.h>
#include <Validation/Validation.h>
#include <algorithm>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <thread>

void check(bool value,const char *message) {if (!value) throw std::runtime_error(message);}
void good(ii::core::Result<void> value) {if (!value) throw std::runtime_error(value.error().message);}
int main() {
    try {
        using namespace dreamscapes::native;
        using namespace iiSharedCanvas;
        auto session=DocumentSession::create();
        check(!session->hasDocument.get() && !session->selectLayer("layer"),"empty session accepted a selection");
        Document document; document.extent={8,6}; document.timeline={{24,1},48};
        document.assets.emplace_back(RasterAsset{"pixels",makeRasterLayer(8,6,0xff224466)});
        document.layers.emplace_back(StaticBitmapLayer{{"layer","Paint"},StaticSource{"pixels"}});
        document.publicationMetadata=PublicationMetadata{.title="Existing title",.description="Existing description",
            .settings={{"shared.unknown",std::string("preserve")}}};
        check(validate(document).ok(),"fixture document is invalid");
        const auto original=encodeIisc(document);
        check(original.ok(),"fixture document could not be encoded");
        good(session->attach(document,"Example.iisc"));
        check(session->hasDocument.get() && session->title.get()=="Example.iisc" && session->frameCount.get()==48 &&
              session->layers.get().size()==1 && session->layers.get()[0]->name.get()=="Paint",
              "document did not project to the view model");
        good(session->selectLayer("layer")); good(session->seek(10));
        check(!session->dirty.get() && session->frame.get()==10 &&
              encodeIisc(*session->document()).bytes==original.bytes,
              "view state modified the document");
        check(!session->seek(48) && !session->selectLayer("missing"),"invalid selection or seek was accepted");
        const auto generation=session->documentGeneration.get();
        const auto snapshot=session->snapshot();
        check(snapshot && snapshot==session->snapshot() && snapshot.get()==session->document(),
              "immutable document snapshots were copied before an edit");
        good(session->setLayerVisible("layer",false));
        check(snapshot.get()!=session->document() && layerProperties(snapshot->layers[0]).visible &&
              session->documentGeneration.get()==generation,
              "an edit changed a worker snapshot or replaced document identity");
        check(session->dirty.get() && !session->layers.get()[0]->visible.get() &&
              !layerProperties(session->document()->layers[0]).visible && layerProperties(document.layers[0]).visible,
              "layer edit did not change only the session draft");
        good(session->editPublication("title","New title"));
        check(session->publicationTitle.get()=="New title" && session->document()->publicationMetadata &&
              std::get<std::string>(session->document()->publicationMetadata->settings.at("shared.unknown"))=="preserve" &&
              document.publicationMetadata->title=="Existing title",
              "publication edit lost shared metadata or modified the source snapshot");
        check(!session->editPublication("unknown","bad") && session->publicationTitle.get()=="New title",
              "unknown publication field was accepted");
        Document invalid; invalid.extent={0,0};
        check(!session->attach(invalid,"Invalid.iisc") && session->title.get()=="Example.iisc",
              "invalid replacement lost the open document");
        Document another; another.extent={4,4};
        good(session->attach(another,"Other.iisc"));
        check(session->documentGeneration.get()!=generation && snapshot->extent.width==8,
              "replacing a document did not retain the old snapshot identity");
        const auto savedSource=session->snapshot();
        const auto savedGeneration=session->documentGeneration.get(),savedRevision=session->revision.get();
        bool editedDuringNotification=false;
        auto observer=session->revision.observe([&](const auto &,std::uint64_t) {
            if (!editedDuringNotification) {
                editedDuringNotification=true;
                good(session->editPublication("title","Later edit"));
            }
        });
        auto saved=session->acknowledgeSave(savedGeneration,savedRevision,*savedSource,*savedSource);
        check(saved && !*saved && session->dirty.get() && session->publicationTitle.get()=="Later edit",
              "an edit during save notification was incorrectly marked clean");
        ii::core::ErrorCode foreign=ii::core::ErrorCode::failed;
        std::thread worker([&] {auto result=session->seek(2); if (!result) foreign=result.error().code;}); worker.join();
        check(foreign==ii::core::ErrorCode::wrong_executor && session->frame.get()==0,
              "foreign thread changed presentation state");

        // Duplication is a document transaction. Every source is independent,
        // but repeated dynamic-frame references share the copied asset.
        Document moving; moving.extent={2,2}; moving.timeline={{24,1},3};
        moving.assets.emplace_back(RasterAsset{"red",makeRasterLayer(2,2,0xffff0000)});
        moving.assets.emplace_back(RasterAsset{"blue",makeRasterLayer(2,2,0xff0000ff)});
        moving.layers.emplace_back(DynamicBitmapLayer{{"moving","Moving"},KeyframedSource{{0,1,2}}});
        layerProperties(moving.layers[0]).appearance.rasterMask=LayerMask{.assetId="red"};
        moving.frames={{0,{{"moving","red"}}},{1,{{"moving","blue"}}},{2,{{"moving","red"}}}};
        check(validate(moving).ok(),"dynamic duplicate fixture is invalid");
        good(session->attach(moving,"Motion.iisc")); good(session->selectLayer("moving"));
        const auto beforeCopy=session->snapshot();
        const auto versionBeforeCopy=session->revision.get();
        auto copied=session->duplicateLayer("moving");
        check(copied && *copied!="moving" && session->document()->layers.size()==2 &&
              session->selectedLayer.get()==*copied && session->dirty.get() &&
              session->revision.get()==versionBeforeCopy+1 && beforeCopy->layers.size()==1,
              "duplicate did not commit and select a detached layer");
        const auto *copy0=resolveAssetAt(*session->document(),*findLayer(*session->document(),*copied),0);
        const auto *copy1=resolveAssetAt(*session->document(),*findLayer(*session->document(),*copied),1);
        const auto *copy2=resolveAssetAt(*session->document(),*findLayer(*session->document(),*copied),2);
        check(copy0 && copy1 && copy2 && std::get<RasterAsset>(*copy0).id!="red" &&
              std::get<RasterAsset>(*copy1).id!="blue" &&
              std::get<RasterAsset>(*copy0).id==std::get<RasterAsset>(*copy2).id &&
              std::get<RasterAsset>(*copy0).id!=std::get<RasterAsset>(*copy1).id,
              "dynamic copy did not preserve frame references using independent assets");
        const auto copyRed=std::get<RasterAsset>(*copy0).id;
        check(layerProperties(*findLayer(*session->document(),*copied)).appearance.rasterMask->assetId==copyRed,
              "a mask using the same source was not remapped to the independent copied asset");
        auto edited=*session->document();
        DocumentEditor independent(edited);
        check(independent.replaceRasterPixels(copyRed,makeRasterLayer(2,2,0xff00ff00)).ok() &&
              std::get<RasterAsset>(*findAsset(edited,"red")).pixels.pixels[0]==0xffff0000,
              "editing copied pixels changed the original asset");
        check(validate(*session->document()).ok() && encodeIisc(*session->document()).ok(),
              "duplicated dynamic layer is not a valid serializable canvas");
        const auto afterCopy=session->snapshot();
        check(!session->duplicateLayer("missing") && session->snapshot()==afterCopy,
              "failed duplication modified the document");
        // Removal keeps assets and immutable snapshots, but drops layer keyframes.
        const auto beforeRemoval=session->snapshot();
        const auto removalRevision=session->revision.get();
        good(session->removeLayer(*copied));
        check(session->document()->layers.size()==1 && session->layers.get().size()==1 &&
              session->selectedLayer.get()=="moving" && session->layers.get()[0]->selected.get() &&
              session->revision.get()==removalRevision+1 && session->dirty.get() &&
              beforeRemoval->layers.size()==2 && session->document()->assets.size()==beforeRemoval->assets.size(),
              "layer removal lost assets, selection, revision or the immutable snapshot");
        for (const auto &frame:session->document()->frames)
            for (const auto &key:frame.keyframes)
                check(key.layerId!=*copied,"removed layer retained keyframes");
        check(validate(*session->document()).ok() && encodeIisc(*session->document()).ok(),
              "layer removal produced an invalid document");
        const auto afterRemoval=session->snapshot();
        check(!session->removeLayer("missing") && session->snapshot()==afterRemoval,
              "a missing layer removal modified the document");
        good(session->removeLayer("moving"));
        check(session->document()->layers.empty() && session->selectedLayer.get().empty() &&
              session->layers.get().empty() && session->document()->assets.size()==beforeRemoval->assets.size(),
              "removing the final layer lost assets or left a stale selection");
        auto reentrant=DocumentSession::create();
        good(reentrant->attach(moving,"First.iisc")); good(reentrant->selectLayer("moving"));
        Document replacement; replacement.extent={3,3};
        bool switched=false;
        auto rowsObserver=reentrant->layers.observe([&](const auto &rows,std::uint64_t) {
            if (!switched && rows.size()==2) {switched=true; good(reentrant->attach(replacement,"Second.iisc"));}
        });
        const auto interrupted=reentrant->duplicateLayer("moving");
        check(switched && !interrupted && reentrant->title.get()=="Second.iisc" &&
              reentrant->document()->layers.empty() && reentrant->layers.get().empty() && !reentrant->dirty.get(),
              "a document switch during duplication corrupted the replacement document");
        auto duringRemoval=DocumentSession::create();
        good(duringRemoval->attach(moving,"Before.iisc")); good(duringRemoval->selectLayer("moving"));
        bool removalSwitched=false;
        auto removalObserver=duringRemoval->layers.observe([&](const auto &rows,std::uint64_t) {
            if (!removalSwitched && rows.empty()) {
                removalSwitched=true; good(duringRemoval->attach(replacement,"After.iisc"));
            }
        });
        check(!duringRemoval->removeLayer("moving") && removalSwitched &&
              duringRemoval->title.get()=="After.iisc" && duringRemoval->document()->extent.width==3 &&
              !duringRemoval->dirty.get(),"a document switch during removal corrupted the replacement");
        auto duringVisibility=DocumentSession::create();
        good(duringVisibility->attach(moving,"Before.iisc"));
        auto oldRow=duringVisibility->layers.get()[0];
        bool visibilitySwitched=false;
        auto visibilityObserver=oldRow->visible.observe([&](bool visible,std::uint64_t) {
            if (!visible && !visibilitySwitched) {
                visibilitySwitched=true; good(duringVisibility->attach(replacement,"After.iisc"));
            }
        });
        check(!duringVisibility->setLayerVisible("moving",false) && visibilitySwitched &&
              duringVisibility->title.get()=="After.iisc" && !duringVisibility->dirty.get(),
              "a document switch during visibility notification dirtied the replacement");
        auto duringPublication=DocumentSession::create();
        good(duringPublication->attach(moving,"Before.iisc"));
        bool publicationSwitched=false;
        auto publicationObserver=duringPublication->publicationTitle.observe([&](const auto &title,std::uint64_t) {
            if (title=="New title" && !publicationSwitched) {
                publicationSwitched=true;good(duringPublication->attach(replacement,"After.iisc"));
            }
        });
        check(!duringPublication->editPublication("title","New title") && publicationSwitched &&
              duringPublication->title.get()=="After.iisc" && duringPublication->publicationTitle.get().empty() &&
              !duringPublication->dirty.get(),"publication notification corrupted a replacement document");
        auto exhausted=DocumentSession::create(); good(exhausted->attach(moving,"Limit.iisc"));
        (void)exhausted->visualRevision.set(std::numeric_limits<std::uint64_t>::max());
        const auto beforeExhaustion=exhausted->snapshot();
        check(!exhausted->setLayerVisible("moving",false) && exhausted->snapshot()==beforeExhaustion,
              "a visual edit overflowed the canvas revision");
        good(exhausted->editPublication("title","Still editable"));
        check(exhausted->publicationTitle.get()=="Still editable" && exhausted->dirty.get(),
              "visual revision exhaustion incorrectly blocked metadata edits");
        auto resize=DocumentSession::create();good(resize->attach(document,"Resize.iisc"));
        const auto oldCanvas=resize->snapshot();
        const auto oldRevision=resize->revision.get(),oldVisual=resize->visualRevision.get();
        good(resize->resizeCanvasCentered(12,10));
        const auto &resized=*resize->document();
        check(resized.extent.width==12 && resized.extent.height==10 && resize->dimensions.get()=="12 × 10 px" &&
              layerProperties(resized.layers[0]).transform.translationX==2 &&
              layerProperties(resized.layers[0]).transform.translationY==2 && resize->dirty.get() &&
              resize->revision.get()==oldRevision+1 && resize->visualRevision.get()==oldVisual+1 &&
              oldCanvas->extent.width==8 && std::get<RasterAsset>(resized.assets[0]).pixels.pixels==
              std::get<RasterAsset>(oldCanvas->assets[0]).pixels.pixels,
              "centered canvas resize did not preserve source pixels or update the document");
        const auto resizedBytes=encodeIisc(resized);
        check(resizedBytes.ok() && decodeIisc(resizedBytes.bytes).ok(),"resized canvas did not serialize");
        const auto beforeInvalidResize=resize->snapshot();
        check(!resize->resizeCanvasCentered(0,10) && resize->snapshot()==beforeInvalidResize,
              "invalid canvas size modified the document");
        auto masked=document;
        layerProperties(masked.layers[0]).appearance.rasterMask=LayerMask{.assetId="pixels"};
        layerProperties(masked.layers[0]).appearance.rasterMask->linkedToLayer=false;
        layerProperties(masked.layers[0]).appearance.rasterMask->transform.translationX=1;
        layerProperties(masked.layers[0]).appearance.maskStack.push_back({.id="linked",.name="Linked",
            .mask=LayerMask{.assetId="pixels"}});
        layerProperties(masked.layers[0]).motion={{0,{}},{12,{}}};
        const auto beforeMaskedSample=sampleLayerAt(masked,masked.layers[0],12);
        good(resize->attach(masked,"Masked.iisc"));
        const auto beforeMaskedResize=resize->snapshot();
        good(resize->resizeCanvasCentered(12,10));
        const auto &maskedLayer=resize->document()->layers[0];
        const auto afterMaskedSample=sampleLayerAt(*resize->document(),maskedLayer,12);
        check(afterMaskedSample.transform.translationX==beforeMaskedSample.transform.translationX+2 &&
              afterMaskedSample.transform.translationY==beforeMaskedSample.transform.translationY+2 &&
              layerProperties(maskedLayer).motion==layerProperties(beforeMaskedResize->layers[0]).motion &&
              layerProperties(maskedLayer).appearance.rasterMask->transform.translationX==3 &&
              layerProperties(maskedLayer).appearance.rasterMask->transform.translationY==2 &&
              layerProperties(maskedLayer).appearance.maskStack[0].mask.transform.translationX==0,
              "resize did not preserve animation or move only unlinked masks in world space");
        const auto beforeMirror=resize->snapshot();
        const auto beforeMirrorRevision=resize->revision.get();
        good(resize->mirrorCanvas(true));
        const auto &mirrored=layerProperties(resize->document()->layers[0]);
        check(mirrored.transform.m11==-1 && mirrored.transform.translationX==10 &&
              mirrored.appearance.rasterMask->transform.m11==-1 &&
              mirrored.appearance.rasterMask->transform.translationX==9 &&
              mirrored.appearance.maskStack[0].mask.transform.m11==1 &&
              resize->revision.get()==beforeMirrorRevision+1 &&
              layerProperties(beforeMirror->layers[0]).transform.m11==1,
              "horizontal mirror failed to preserve snapshots, motion or mask coordinates");
        good(resize->mirrorCanvas(true));
        const auto &restored=layerProperties(resize->document()->layers[0]).transform;
        const auto &baseline=layerProperties(beforeMirror->layers[0]).transform;
        check(restored.m11==baseline.m11 && restored.m12==baseline.m12 && restored.m21==baseline.m21 &&
              restored.m22==baseline.m22 && restored.translationX==baseline.translationX && restored.translationY==baseline.translationY &&
              layerProperties(resize->document()->layers[0]).appearance==layerProperties(beforeMirror->layers[0]).appearance,
              "two horizontal mirrors did not restore the original geometry");
        good(resize->mirrorCanvas(false));
        check(layerProperties(resize->document()->layers[0]).transform.m22==-1 &&
              layerProperties(resize->document()->layers[0]).transform.translationY==8,
              "vertical mirror did not reflect the actual layer");
        auto rotate=DocumentSession::create();good(rotate->attach(masked,"Rotate.iisc"));
        const auto beforeRotate=rotate->snapshot();const auto rotationRevision=rotate->revision.get();
        good(rotate->rotateCanvas(360));
        check(rotate->snapshot()==beforeRotate && rotate->revision.get()==rotationRevision && !rotate->dirty.get(),
              "a full rotation changed the document");
        good(rotate->rotateCanvas(90));
        const auto &turned=layerProperties(rotate->document()->layers[0]);
        check(rotate->document()->extent.width==6 && rotate->document()->extent.height==8 &&
              turned.transform.m11==0 && turned.transform.m12==1 && turned.transform.m21==-1 &&
              turned.transform.m22==0 && turned.transform.translationX==6 && turned.transform.translationY==0 &&
              turned.appearance.rasterMask->transform.translationX==6 &&
              turned.appearance.rasterMask->transform.translationY==1 &&
              turned.appearance.maskStack[0].mask.transform.m11==1 &&
              turned.motion==layerProperties(beforeRotate->layers[0]).motion && rotate->revision.get()==rotationRevision+1,
              "rotation did not preserve the artwork, animation or mask coordinates");
        good(rotate->rotateCanvas(-90));
        const auto &unturned=layerProperties(rotate->document()->layers[0]).transform;
        check(rotate->document()->extent.width==8 && rotate->document()->extent.height==6 &&
              unturned.m11==1 && unturned.m12==0 && unturned.m21==0 && unturned.m22==1 &&
              unturned.translationX==0 && unturned.translationY==0,
              "inverse quarter turns did not restore the original canvas geometry");
        const auto safeRotation=rotate->snapshot();
        check(!rotate->rotateCanvas(std::numeric_limits<double>::quiet_NaN()) && rotate->snapshot()==safeRotation,
              "an invalid rotation modified the document");
        good(rotate->rotateCanvas(45));
        check(rotate->document()->extent.width==10 && rotate->document()->extent.height==10 &&
              encodeIisc(*rotate->document()).ok(),"custom rotation bounds were not serializable");
        auto dissolve=document;layerProperties(dissolve.layers[0]).blendMode=RasterBlendMode::Dissolve;
        good(resize->attach(dissolve,"Dissolve.iisc"));const auto beforeDissolve=resize->snapshot();
        check(!resize->resizeCanvasCentered(12,10) && resize->snapshot()==beforeDissolve && !resize->dirty.get(),
              "unsupported dissolve geometry was changed by a resize");
        auto resizeSwitch=DocumentSession::create();good(resizeSwitch->attach(document,"Before.iisc"));
        bool switchedDuringResize=false;
        auto dimensionObserver=resizeSwitch->dimensions.observe([&](const auto &value,std::uint64_t) {
            if (value=="12 × 10 px" && !switchedDuringResize) {
                switchedDuringResize=true;good(resizeSwitch->attach(replacement,"After.iisc"));
            }
        });
        check(!resizeSwitch->resizeCanvasCentered(12,10) && switchedDuringResize &&
              resizeSwitch->title.get()=="After.iisc" && resizeSwitch->dimensions.get()=="3 × 3 px" &&
              !resizeSwitch->dirty.get(),"a document switch during canvas resize corrupted the replacement");
        auto rotationSwitch=DocumentSession::create();good(rotationSwitch->attach(document,"Before.iisc"));
        bool switchedDuringRotation=false;
        auto rotationObserver=rotationSwitch->dimensions.observe([&](const auto &value,std::uint64_t) {
            if (value=="6 × 8 px" && !switchedDuringRotation) {
                switchedDuringRotation=true;good(rotationSwitch->attach(replacement,"After.iisc"));
            }
        });
        check(!rotationSwitch->rotateCanvas(90) && switchedDuringRotation &&
              rotationSwitch->title.get()=="After.iisc" && rotationSwitch->dimensions.get()=="3 × 3 px" &&
              !rotationSwitch->dirty.get(),"a document switch during canvas rotation corrupted the replacement");
        auto created=DocumentSession::create();
        check(!created->createLayer(LayerKind::StaticBitmap),"an empty session accepted a new layer");
        good(created->attach(document,"Create.iisc"));
        good(created->selectLayer("layer"));
        const auto beforeCreate=created->snapshot();
        for (const auto kind:{LayerKind::StaticBitmap,LayerKind::StaticVector,
                              LayerKind::DynamicBitmap,LayerKind::DynamicVector}) {
            const auto result=created->createLayer(kind);
            if (!result) throw std::runtime_error(result.error().message);
            const auto *layer=findLayer(*created->document(),*result);
            const auto *asset=layer?resolveAssetAt(*created->document(),*layer,0):nullptr;
            check(layer && asset && created->selectedLayer.get()==*result,
                  "new layer was not attached and selected");
            const bool bitmap=kind==LayerKind::StaticBitmap || kind==LayerKind::DynamicBitmap;
            const bool dynamicKind=kind==LayerKind::DynamicBitmap || kind==LayerKind::DynamicVector;
            check(bitmap?std::holds_alternative<RasterAsset>(*asset):std::holds_alternative<VectorAsset>(*asset),
                  "new layer used the wrong empty source type");
            if (bitmap) check(std::get<RasterAsset>(*asset).pixels.width==8 && std::get<RasterAsset>(*asset).pixels.height==6 &&
                              std::ranges::all_of(std::get<RasterAsset>(*asset).pixels.pixels,[](auto pixel){return pixel==0;}),
                              "finite blank bitmap did not cover the canvas transparently");
            if (dynamicKind) check(keyframedLayerSource(*layer) && keyframedLayerSource(*layer)->frameIndices==
                                   std::vector<FrameIndex>{0},"new dynamic layer did not have its frame-zero source");
            else check(staticLayerSource(*layer)!=nullptr,"new static layer did not have a static source");
        }
        check(created->document()->layers.size()==5 && beforeCreate->layers.size()==1 && created->dirty.get() &&
              validate(*created->document()).ok(),"new layer creation changed a snapshot or produced an invalid canvas");
        const auto createdBytes=encodeIisc(*created->document());
        check(createdBytes.ok() && decodeIisc(createdBytes.bytes).ok(),"new layers did not round-trip through .iisc");
        Document huge;huge.extent={65536,65536};
        auto limited=DocumentSession::create();good(limited->attach(huge,"Large.iisc"));
        const auto beforeLimit=limited->snapshot();
        check(!limited->createLayer(LayerKind::StaticBitmap) && limited->snapshot()==beforeLimit && !limited->dirty.get(),
              "an oversized finite bitmap attempted allocation or changed the document");
        Document infinite;infinite.extent={256,256};infinite.canvasMode=CanvasMode::Infinite;
        auto sparse=DocumentSession::create();good(sparse->attach(infinite,"Infinite.iisc"));
        const auto sparseLayer=sparse->createLayer(LayerKind::StaticBitmap);
        if (!sparseLayer) throw std::runtime_error(sparseLayer.error().message);
        const auto *sparseAsset=resolveAssetAt(*sparse->document(),*findLayer(*sparse->document(),*sparseLayer),0);
        check(sparseAsset && std::holds_alternative<ChunkedRasterAsset>(*sparseAsset) &&
              std::get<ChunkedRasterAsset>(*sparseAsset).chunks.empty(),
              "a blank infinite-canvas bitmap allocated chunks");
        auto createSwitch=DocumentSession::create();good(createSwitch->attach(document,"Before.iisc"));
        bool switchedDuringCreate=false;
        auto createObserver=createSwitch->layers.observe([&](const auto &rows,std::uint64_t) {
            if (rows.size()==2 && !switchedDuringCreate) {
                switchedDuringCreate=true;good(createSwitch->attach(replacement,"After.iisc"));
            }
        });
        check(!createSwitch->createLayer(LayerKind::StaticVector) && switchedDuringCreate &&
              createSwitch->title.get()=="After.iisc" && !createSwitch->dirty.get(),
              "a document switch during layer creation corrupted the replacement");
    } catch (const std::exception &error) {std::cerr << error.what() << '\n'; return 1;}
}
