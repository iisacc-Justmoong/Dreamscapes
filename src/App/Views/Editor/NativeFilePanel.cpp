#include "NativeFilePanel.h"
#include "App/ViewModels/Native/ViewValue.h"
#include <iiJson>
#include <algorithm>
#include <charconv>
#include <cmath>
#include <functional>
#include <map>
#include <set>
#include <stdexcept>
#include <tuple>
#include <type_traits>
#include <utility>

namespace dreamscapes::editor {
namespace {
using namespace iiSharedCanvas;
using Values=std::map<std::string,std::string,std::less<>>;
std::string recorded(const std::string &value) {return value.empty()?"Not recorded":value;}
std::string yes(bool value) {return value?"Yes":"No";}
template<class T> std::string number(T value) {
    char buffer[96];
    const auto result=[&] {
        if constexpr (std::is_floating_point_v<T>) return std::to_chars(buffer,buffer+sizeof(buffer),value,std::chars_format::general,15);
        else return std::to_chars(buffer,buffer+sizeof(buffer),value);
    }();
    return result.ec==std::errc{}?std::string(buffer,result.ptr):std::string("Unavailable");
}
template<class T> std::string optionalNumber(const std::optional<T> &value) {return value?number(*value):"Not recorded";}
std::string dimensions(auto width,auto height) {return number(width)+" × "+number(height)+" px";}
std::string coordinates(auto x,auto y) {return number(x)+", "+number(y);}
std::string rate(FrameRate value) {return number(value.numerator)+"/"+number(value.denominator)+" fps";}
template<class Collection,class Function> std::string records(const Collection &values,Function format) {
    if (values.empty()) return "None recorded";
    std::string result;std::size_t index=0;
    // Avoid building unbounded strings for large documents; the total remains visible.
    constexpr std::size_t limit=256;
    for (const auto &entry:values) {
        if (index==limit) {result+="\n… "+number(values.size()-index)+" more";break;}
        if (index) result+='\n';
        ++index;
        if (values.size()>1) result+="["+number(index)+"] ";
        result+=format(entry);
    }
    return result;
}
template<class T> std::size_t assetCount(const Document &document) {
    return std::ranges::count_if(document.assets,[](const auto &asset){return std::holds_alternative<T>(asset);});
}
std::string entries(const std::vector<StableDiffusionMetadataEntry> &values) {
    return records(values,[](const auto &entry){return entry.key+" = "+entry.value;});
}
std::string jsonText(const vm::Value &value) {
    return std::visit([](const auto &item) -> std::string {
        using T=std::decay_t<decltype(item)>;
        if constexpr (std::is_same_v<T,std::string>) return recorded(item);
        else if constexpr (std::is_same_v<T,bool>) return yes(item);
        else if constexpr (std::is_arithmetic_v<T>) return number(item);
        else if constexpr (std::is_same_v<T,vm::Array>) return records(item,[](const auto &entry){return jsonText(entry);});
        else return "Not recorded";
    },value.storage());
}
Values information(const Document &doc,const native::DocumentSession &session) {
    Values out;
    out["EditorCanvas.documentName"]=session.title.get();
    out["Document.formatVersion.major/minor"]="IISC "+number(doc.formatVersion.major)+"."+number(doc.formatVersion.minor);
    if (!doc.stableDiffusionMetadata) {
        out["Document.stableDiffusionMetadata"]="Not recorded";
    } else {
        const auto &m=*doc.stableDiffusionMetadata;
        out["Document.stableDiffusionMetadata"]="Recorded";
        out["StableDiffusionMetadata.positivePrompt"]=recorded(m.positivePrompt);
        out["StableDiffusionMetadata.negativePrompt"]=recorded(m.negativePrompt);
        out["StableDiffusionMetadata.outputExtent.width/height"]=m.outputExtent?dimensions(m.outputExtent->width,m.outputExtent->height):"Not recorded";
        out["StableDiffusionMetadata.batchSize"]=optionalNumber(m.batchSize);
        out["StableDiffusionMetadata.clipSkip"]=optionalNumber(m.clipSkip);
        out["StableDiffusionMetadata.software"]=recorded(m.software);
        out["StableDiffusionMetadata.softwareVersion"]=recorded(m.softwareVersion);
        out["StableDiffusionMetadata.createdAt"]=recorded(m.createdAt);
        out["StableDiffusionMetadata.positivePrompt/negativePrompt"]="Positive: "+recorded(m.positivePrompt)+"\nNegative: "+recorded(m.negativePrompt);
        out["StableDiffusionMetadata.samplingPasses"]=number(m.samplingPasses.size());
        out["StableDiffusionMetadata.models"]=number(m.models.size());
        out["StableDiffusionMetadata.loras"]=number(m.loras.size());
#define PASS(member, format) out["StableDiffusionSamplingPass." #member]=records(m.samplingPasses,[](const auto &p){return format(p.member);})
        PASS(nodeId,recorded);PASS(seed,optionalNumber);PASS(steps,optionalNumber);PASS(cfgScale,optionalNumber);
        PASS(samplerName,recorded);PASS(scheduler,recorded);PASS(denoiseStrength,optionalNumber);PASS(startStep,optionalNumber);PASS(endStep,optionalNumber);
#undef PASS
#define MODEL(member) out["StableDiffusionModelResource." #member]=records(m.models,[](const auto &entry){return recorded(entry.member);})
        MODEL(role);MODEL(name);MODEL(hash);MODEL(hashType);MODEL(uri);
#undef MODEL
#define LORA(member, format) out["StableDiffusionLora." #member]=records(m.loras,[](const auto &entry){return format(entry.member);})
        LORA(name,recorded);LORA(hash,recorded);LORA(modelStrength,number);LORA(clipStrength,number);
#undef LORA
        out["StableDiffusionMetadata.generationParametersText"]=recorded(m.generationParametersText);
        out["ComfyUiMetadata.promptJson"]=recorded(m.comfyUi.promptJson);
        out["ComfyUiMetadata.workflowJson"]=recorded(m.comfyUi.workflowJson);
        out["ComfyUiMetadata.extraPngInfo[].key/value"]=entries(m.comfyUi.extraPngInfo);
        out["StableDiffusionMetadata.extraParameters[].key/value"]=entries(m.extraParameters);
        out["ComfyUiMetadata.promptJson/workflowJson"]="Prompt: "+recorded(m.comfyUi.promptJson)+"\nWorkflow: "+recorded(m.comfyUi.workflowJson);
        out["StableDiffusionMetadata.extraParameters"]=number(m.extraParameters.size());
    }
    const auto &authors=doc.authorship;
    out["Authorship.schemaVersion"]=number(decltype(doc.authorship)::SchemaVersion);
    out["Authorship.revision"]=number(authors.revision());
    out["Authorship.isEmpty"]=yes(authors.isEmpty());
    out["Authorship.links"]=number(authors.links().size());
    if (auto first=authors.firstEditor()) out["Authorship.firstEditor"]=recorded(first->displayLabel());
    else out["Authorship.firstEditor"]="None recorded";
    out["Authorship.participants"]=records(authors.participants(),[](const auto &author){return recorded(author.displayLabel());});
    out["FileLink.name"]=records(authors.links(),[](const auto &link){return recorded(link.name);});
    out["FileLink.urlText"]=records(authors.links(),[](const auto &link){return recorded(link.url);});
    if (const auto parsed=ii::json::parse(authorshipDump(authors))) {
        const auto &items=vm::array(vm::get(*parsed,"authors"));
        out["Authorship.modifiedAt"]=jsonText(vm::get(*parsed,"modifiedAt"));
        const auto last=vm::text(vm::get(*parsed,"lastAuthor"));
        const auto match=std::ranges::find_if(items,[&](const auto &entry){return vm::text(vm::get(entry,"key"))==last;});
        if (match!=items.end()) {
            const auto text=ii::json::stringify(vm::get(*match,"author"));
            const auto author=text?DocumentFileAuthor::fromJsonText(*text):std::nullopt;
            out["Authorship.lastAuthor"]=author?recorded(author->displayLabel()):"Not recorded";
        } else out["Authorship.lastAuthor"]="Not recorded";
        for (const auto field:{"key","firstChangedAt","lastChangedAt"})
            out[std::string("Authorship.authors[].")+field]=records(items,[&](const auto &entry){return jsonText(vm::get(entry,field));});
        const std::pair<std::string_view,std::string_view> fields[]{
            {"FileAuthor.SchemaVersion","schemaVersion"},{"IisaccAccount.sub","account.sub"},
            {"IisaccAccount.userId","account.userId"},{"IisaccAccount.displayName","account.displayName"},
            {"IisaccAccount.email","account.email"},{"IisaccAccount.societyCloudMembership","account.societyCloudMembership"},
            {"IisaccAccount.avatarUrl","account.avatarUrl"},{"AuthorMetadata.serviceOrigin","serviceOrigin"},
            {"AuthorMetadata.capturedAt","capturedAt"},{"AuthorDetails.fullName","details.fullName"},
            {"AuthorDetails.givenName","details.givenName"},{"AuthorDetails.additionalName","details.additionalName"},
            {"AuthorDetails.familyName","details.familyName"},{"AuthorDetails.pseudonym","details.pseudonym"},
            {"AuthorDetails.biography","details.biography"},{"AuthorDetails.organization","details.organization"},
            {"AuthorDetails.organizationId","details.organizationId"},{"AuthorDetails.department","details.department"},
            {"AuthorDetails.team","details.team"},{"AuthorDetails.jobTitle","details.jobTitle"},
            {"AuthorDetails.contactEmail","details.contactEmail"},{"AuthorDetails.phoneNumber","details.phoneNumber"},
            {"AuthorDetails.locale","details.locale"},{"AuthorDetails.timeZone","details.timeZone"},
            {"AuthorDetails.countryCode","details.countryCode"},{"AuthorDetails.region","details.region"},
            {"AuthorDetails.city","details.city"},{"FileAttribution.roles","attribution.roles"},
            {"FileAttribution.credit","attribution.credit"},{"FileAttribution.copyrightNotice","attribution.copyrightNotice"},
            {"FileAttribution.licenseIdentifier","attribution.licenseIdentifier"},{"FileAttribution.licenseUrl","attribution.licenseUrl"},
            {"FileAttribution.documentId","attribution.documentId"},{"FileAttribution.projectId","attribution.projectId"},
            {"FileAttribution.workspaceId","attribution.workspaceId"},{"FileAttribution.createdAt","attribution.createdAt"},
            {"FileAttribution.modifiedAt","attribution.modifiedAt"},{"AuthorDevice.id","device.id"},
            {"AuthorDevice.type","device.type"},{"AuthorDevice.name","device.name"},{"AuthorDevice.platform","device.platform"},
            {"AuthorDevice.osVersion","device.osVersion"},{"AuthorDevice.appId","device.appId"},{"AuthorDevice.appVersion","device.appVersion"}};
        for (const auto &[key,path]:fields) out[std::string(key)]=records(items,[&](const auto &entry) {
            const auto value=vm::pathValue(vm::get(entry,"author"),path);return value?jsonText(*value):std::string("Not recorded");
        });
        for (const auto &[prefix,collection,members]:{
             std::tuple{"AuthorLink.","details.links",std::vector<std::string_view>{"relation","label","url"}},
             std::tuple{"AuthorIdentifier.","details.identifiers",std::vector<std::string_view>{"scheme","value"}}}) {
            for (const auto member:members) out[std::string(prefix)+std::string(member)]=records(items,[&](const auto &entry) {
                const auto nested=vm::pathValue(vm::get(entry,"author"),collection);
                return nested?records(vm::array(*nested),[&](const auto &item){return jsonText(vm::get(item,member));}):std::string("None recorded");
            });
        }
        out["AuthorMetadata.device"]=records(items,[](const auto &entry){
            const auto &value=vm::get(vm::get(entry,"author"),"device");
            return vm::object(value).empty()?std::string("Not recorded"):std::string("Recorded");
        });
    }
    return out;
}
Values contents(const Document &doc,const native::DocumentSession &session) {
    Values out;const auto origin=canvasOrigin(doc);const auto bounds=documentViewRegion(doc);
    out["Document.canvasMode"]=doc.canvasMode==CanvasMode::Finite?"Finite":"Infinite";
    out["Document.extent.width/height"]=dimensions(doc.extent.width,doc.extent.height);
    out["canvasOrigin(Document)"]=coordinates(origin.x,origin.y);
    out["documentViewRegion(Document)"]=coordinates(bounds.origin.x,bounds.origin.y)+" · "+dimensions(bounds.extent.width,bounds.extent.height);
    out["Document.extent/Document.layers/Document.timeline"]=dimensions(doc.extent.width,doc.extent.height)+" · "+number(doc.layers.size())+" layers · "+number(doc.timeline.frameCount)+" frames";
    out["Document.artboards"]=records(doc.artboards,[](const auto &board){return recorded(board.id)+" · "+recorded(board.name);});
    out["Document.artboards.count"]=number(doc.artboards.size());
#define BOARD(member, expression) out["Artboard." member]=records(doc.artboards,[](const auto &board){return expression;})
    BOARD("id",recorded(board.id));BOARD("name",recorded(board.name));
    BOARD("region.origin.x/y",coordinates(board.region.origin.x,board.region.origin.y));
    BOARD("region.extent.width/height",dimensions(board.region.extent.width,board.region.extent.height));
    BOARD("visible",yes(board.visible));
#undef BOARD
    out["Document.layers/Document.assets"]=number(doc.layers.size())+" layers / "+number(doc.assets.size())+" assets";
    out["Document.layers.count"]=number(doc.layers.size());out["Document.assets.count"]=number(doc.assets.size());
#define KIND(name) out["LayerKind." #name]=number(std::ranges::count_if(doc.layers,[](const auto &layer){return layerKind(layer)==LayerKind::name;}))
    KIND(StaticBitmap);KIND(StaticVector);KIND(DynamicBitmap);KIND(DynamicVector);
#undef KIND
    out["VideoLayer.count"]=number(std::ranges::count_if(doc.layers,[](const auto &layer){return std::holds_alternative<VideoLayer>(layer);}));
    out["LayerRole.ControlNet.count"]=number(std::ranges::count_if(doc.layers,[](const auto &layer){return layerRole(layer)==LayerRole::ControlNet;}));
    out["SemanticSegmentLayer.count"]=number(std::ranges::count_if(doc.layers,[](const auto &layer){return std::holds_alternative<SemanticSegmentLayer>(layer);}));
#define ASSET(name) out[#name ".count"]=number(assetCount<name>(doc))
    ASSET(RasterAsset);ASSET(VectorAsset);ASSET(ChunkedRasterAsset);ASSET(PoseAsset);ASSET(DepthAsset);
    ASSET(LineArtAsset);ASSET(CannyAsset);ASSET(ScribbleAsset);ASSET(MlsdAsset);ASSET(NormalMapAsset);
    ASSET(ShuffleAsset);ASSET(TileAsset);ASSET(ReferenceAsset);ASSET(IpAdapterAsset);ASSET(VideoAsset);
#undef ASSET
    std::size_t keys=0,motions=0,clips=0,chunks=0;
    for (const auto &frame:doc.frames) keys+=frame.keyframes.size();
    for (const auto &layer:doc.layers) motions+=layerProperties(layer).motion.size();
    for (const auto &track:doc.audioTracks) clips+=track.clips.size();
    std::vector<const ChunkedRasterAsset *> chunked;std::vector<const VideoAsset *> videos;std::vector<const VideoLayer *> videoLayers;
    for (const auto &asset:doc.assets) {
        if (const auto *value=std::get_if<ChunkedRasterAsset>(&asset)) {chunked.push_back(value);chunks+=value->chunks.size();}
        if (const auto *value=std::get_if<VideoAsset>(&asset)) videos.push_back(value);
    }
    for (const auto &layer:doc.layers) if (const auto *value=std::get_if<VideoLayer>(&layer)) videoLayers.push_back(value);
    out["Frame.keyframes.count"]=number(keys);out["Document.frames.count"]=number(doc.frames.size());
    out["LayerProperties.motion.count"]=number(motions);out["Document.audioAssets.count"]=number(doc.audioAssets.size());
    out["Document.audioTracks.count"]=number(doc.audioTracks.size());out["AudioTrackLayer.clips.count"]=number(clips);
    out["Document.timeline"]=rate(doc.timeline.frameRate)+" · "+number(doc.timeline.frameCount)+" frames";
    out["Document.timeline.frameRate.numerator/denominator"]=rate(doc.timeline.frameRate);
    out["Document.timeline.frameCount"]=number(doc.timeline.frameCount);
    out["Document.timeline.derivedDuration"]=doc.timeline.frameRate.numerator?
        number(double(doc.timeline.frameCount)*doc.timeline.frameRate.denominator/doc.timeline.frameRate.numerator)+" s":"Unavailable";
    out["EditorCanvas.frame"]=number(session.frame.get());
    out["ChunkedRasterAsset.chunks.count"]=number(chunks);
    out["ChunkedRasterAsset.id"]=records(chunked,[](const auto *asset){return recorded(asset->id);});
    out["Document.infiniteCanvas.origin.x/y"]=doc.canvasMode==CanvasMode::Infinite?coordinates(doc.infiniteCanvas.origin.x,doc.infiniteCanvas.origin.y):"Not applicable";
    out["Document.infiniteCanvas.chunkSize"]=doc.canvasMode==CanvasMode::Infinite?number(doc.infiniteCanvas.chunkSize)+" px":"Not applicable";
    out["RasterChunk.column/row"]=records(chunked,[](const auto *asset){return recorded(asset->id)+"\n"+records(asset->chunks,[](const auto &chunk){return coordinates(chunk.column,chunk.row);});});
    out["RasterChunk.pixels.width/height"]=records(chunked,[](const auto *asset){return recorded(asset->id)+"\n"+records(asset->chunks,[](const auto &chunk){return coordinates(chunk.column,chunk.row)+" · "+dimensions(chunk.pixels.width,chunk.pixels.height);});});
    out["Derived.bitmapPixelModel"]="8-bit channels · ARGB32";
    out["VideoAsset.id"]=records(videos,[](const auto *video){return recorded(video->id);});
    out["VideoAsset.frameRate.numerator/denominator"]=records(videos,[](const auto *video){return recorded(video->id)+" · "+rate(video->frameRate);});
    out["VideoAsset.frames.count"]=records(videos,[](const auto *video){return recorded(video->id)+" · "+number(video->frames.size());});
    out["VideoAsset.frames[].width/height"]=records(videos,[](const auto *video){return recorded(video->id)+"\n"+records(video->frames,[](const auto &frame){return dimensions(frame.width,frame.height);});});
    out["VideoPlayback.sourceInFrame"]=records(videoLayers,[](const auto *video){return recorded(video->properties.id)+" · "+number(video->playback.sourceInFrame);});
    out["VideoPlayback.sourceOutFrame"]=records(videoLayers,[](const auto *video){return recorded(video->properties.id)+" · "+(video->playback.sourceOutFrame?number(*video->playback.sourceOutFrame)+" (exclusive)":"Asset end");});
    out["VideoPlayback.endBehavior"]=records(videoLayers,[](const auto *video){return recorded(video->properties.id)+" · "+(video->playback.endBehavior==VideoEndBehavior::Hold?"Hold":"Transparent");});
    out["LayerProperties.frameRange.firstFrame/lastFrame"]=records(doc.layers,[](const auto &layer){
        const auto &p=layerProperties(layer);return recorded(p.id)+" · "+(p.frameRange?number(p.frameRange->firstFrame)+" — "+number(p.frameRange->lastFrame)+" (inclusive)":"Entire timeline");});
#define AUDIO(member, expression) out["AudioAsset." member]=records(doc.audioAssets,[](const auto &asset){return expression;})
    AUDIO("id",recorded(asset.id));AUDIO("sampleRate",recorded(asset.id)+" · "+number(asset.sampleRate)+" Hz");
    AUDIO("channelCount",recorded(asset.id)+" · "+number(asset.channelCount));AUDIO("samples.count",recorded(asset.id)+" · "+number(asset.samples.size()));
    AUDIO("derivedDuration",recorded(asset.id)+" · "+(asset.sampleRate && asset.channelCount?number(double(asset.samples.size())/asset.sampleRate/asset.channelCount)+" s":"Unavailable"));
#undef AUDIO
#define TRACK(member, format) out["AudioTrackLayer." #member]=records(doc.audioTracks,[](const auto &track){return format(track.member);})
    TRACK(id,recorded);TRACK(name,recorded);TRACK(muted,yes);
#undef TRACK
    out["AudioTrackLayer.gainDb"]=records(doc.audioTracks,[](const auto &track){return number(track.gainDb)+" dB";});
#define CLIP(member, format) out["AudioClip." #member]=records(doc.audioTracks,[](const auto &track){return recorded(track.id)+"\n"+records(track.clips,[](const auto &clip){return format(clip.member);});})
    CLIP(id,recorded);CLIP(name,recorded);CLIP(assetId,recorded);CLIP(startFrame,number);CLIP(durationFrames,number);
    CLIP(sourceOffsetSamples,number);CLIP(enabled,yes);
#undef CLIP
    out["AudioClip.gainDb"]=records(doc.audioTracks,[](const auto &track){return recorded(track.id)+"\n"+records(track.clips,[](const auto &clip){return number(clip.gainDb)+" dB";});});
    return out;
}
}
FilePanelModel::FilePanelModel(std::shared_ptr<native::DocumentSession> document,std::string_view source) : document_(std::move(document)) {
    if (!document_) throw std::invalid_argument("File inspector requires a document session");
    const auto parsed=ii::json::parse(source);
    if (!parsed) throw std::invalid_argument("Invalid file inspector schema");
    std::set<std::string> ids;
    for (const auto &value:vm::array(vm::get(*parsed,"views"))) {
        View view{vm::text(vm::get(value,"id")),vm::text(vm::get(value,"name")),{}};
        if (view.id.empty() || view.name.empty() || !ids.insert(view.id).second) throw std::invalid_argument("Invalid file inspector view");
        for (const auto &group:vm::array(vm::get(value,"sections"))) {
            Section section{vm::text(vm::get(group,"name")),{}};
            if (section.name.empty()) throw std::invalid_argument("Invalid file inspector section");
            for (const auto &item:vm::array(vm::get(group,"rows"))) {
                Field field{vm::text(vm::get(item,"key")),vm::text(vm::get(item,"label"))};
                if (field.key.empty() || field.label.empty()) throw std::invalid_argument("Invalid file inspector field");
                section.fields.push_back(std::move(field));
            }
            view.sections.push_back(std::move(section));
        }
        schema_.push_back(std::move(view));
    }
    if (ids!=std::set<std::string>{"contents","information","storage"}) throw std::invalid_argument("File inspector requires three views");
}
std::shared_ptr<FilePanelModel> FilePanelModel::create(std::shared_ptr<native::DocumentSession> document,std::string_view schema) {
    auto value=std::shared_ptr<FilePanelModel>(new FilePanelModel(std::move(document),schema));value->start();return value;
}
void FilePanelModel::start() {
    const std::weak_ptr<FilePanelModel> weak=shared_from_this();
    auto changed=[weak](const auto &,std::uint64_t){if (auto value=weak.lock()) value->refresh();};
    revisionObserver_=document_->revision.observe(changed);generationObserver_=document_->documentGeneration.observe(changed);
    dirtyObserver_=document_->dirty.observe(changed);
    frameObserver_=document_->frame.observe([weak](const auto &,std::uint64_t){if (auto value=weak.lock()) value->refreshFrame();});
}
ii::core::Result<void> FilePanelModel::select(std::string_view id) {
    if (owner_!=std::this_thread::get_id()) return ii::core::fail(ii::core::ErrorCode::wrong_executor,"File inspector requires its owner thread");
    if (std::ranges::none_of(schema_,[&](const auto &view){return view.id==id;}))
        return ii::core::fail(ii::core::ErrorCode::invalid_argument,"Unknown file inspector view");
    active_=id;refresh();return {};
}
void FilePanelModel::setVisible(bool value) {if (owner_==std::this_thread::get_id() && visible_!=value) {visible_=value;if (visible_) refresh();}}
void FilePanelModel::setFileModel(std::shared_ptr<native::CanvasFileModel> value) {
    if (owner_!=std::this_thread::get_id()) return;
    pathObserver_={};fileErrorObserver_={};file_=std::move(value);
    if (file_) {
        const std::weak_ptr<FilePanelModel> weak=shared_from_this();
        auto changed=[weak](const auto &,std::uint64_t){if (auto self=weak.lock()) self->refresh();};
        pathObserver_=file_->path.observe(changed);fileErrorObserver_=file_->error.observe(changed);
    }
    refresh();
}
void FilePanelModel::refreshFrame() {
    if (!visible_ || active_!="contents") return;
    if (refreshing_) {pending_=true;return;}
    const auto current=rows.get();
    for (const auto &row:current) if (row->key=="EditorCanvas.frame") (void)row->value.set(number(document_->frame.get()));
}
void FilePanelModel::refresh() {
    if (owner_!=std::this_thread::get_id()) return;
    if (refreshing_) {pending_=true;return;}
    refreshing_=true;
    try {
        do {
            pending_=false;
            const auto chosen=active_;
            (void)informationMode.set(chosen=="information");(void)contentsMode.set(chosen=="contents");(void)storageMode.set(chosen=="storage");
            const auto view=std::ranges::find_if(schema_,[&](const auto &item){return item.id==chosen;});
            if (view==schema_.end()) continue;
            (void)viewTitle.set(view->name);
            if (!visible_) continue;
            const auto snapshot=document_->snapshot();
            Values values;
            if (snapshot) {
                const auto fileVersion=file_?file_->version():std::nullopt;
                if (chosen=="information") {
                    values=information(*snapshot,*document_);
                    if (fileVersion) {
                        values["Derived.storageFormat"]=fileVersion->working?"SQLite working file":"Serialized IISC snapshot";
                        values["Alias.size"]=number(fileVersion->identity.size)+" bytes";
                    } else if (!file_ || file_->path.get().empty()) values["Derived.storageFormat"]="In-memory document";
                }
                else if (chosen=="contents") values=contents(*snapshot,*document_);
                else {
                    values["EditorCanvas.documentReady"]="Yes";
                    values["EditorProject.modified"]=yes(document_->dirty.get());
                    values["EditorCanvas.revision"]=number(document_->revision.get());
                    if (fileVersion && file_ && !file_->path.get().empty()) {
                        values["EditorCanvas.filePath"]=file_->path.get();
                        values["EditorCanvas.documentSynchronized"]=yes(!document_->dirty.get());
                        values["DocumentFile.revision"]=fileVersion->working?number(fileVersion->revision):"Not applicable";
                    }
                    if (file_ && !file_->error.get().empty()) values["EditorCanvas.error"]=file_->error.get();
                }
            } else if (chosen=="storage") values["EditorCanvas.documentReady"]="No";
            std::vector<std::shared_ptr<FilePanelRow>> projected;
            std::size_t groupIndex=0;
            for (const auto &section:view->sections) {
                auto heading=std::make_shared<FilePanelRow>();heading->id=chosen+"/"+number(groupIndex);heading->label=section.name;heading->section=true;
                projected.push_back(std::move(heading));std::size_t fieldIndex=0;
                for (const auto &field:section.fields) {
                    auto row=std::make_shared<FilePanelRow>();row->id=chosen+"/"+number(groupIndex)+"/"+number(fieldIndex++);
                    row->key=field.key;row->label=field.label;
                    const auto found=values.find(field.key);
                    const bool noGeneration=snapshot && chosen=="information" && !snapshot->stableDiffusionMetadata &&
                        (field.key.starts_with("StableDiffusion") || field.key.starts_with("ComfyUi"));
                    (void)row->value.set(found==values.end()?(noGeneration?"Not recorded":"Unavailable"):found->second);
                    projected.push_back(std::move(row));
                }
                ++groupIndex;
            }
            (void)rows.set(std::move(projected));
        } while (pending_);
        refreshing_=false;
    } catch (...) {refreshing_=false;throw;}
}
void FilePanelModel::sizeChanged(double w,double h,double inset) {
    if (!std::isfinite(w)||!std::isfinite(h)||!std::isfinite(inset)||w<0||h<0||w>8192||h>8192||inset<0) return;
    const auto content=std::max(1.0,w-inset);
    (void)width.set(std::max(1.0,w));(void)height.set(std::max(1.0,h));(void)contentWidth.set(content);
    (void)valueWidth.set(std::max(1.0,content/2));
}
}
