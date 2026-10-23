#include "EditorImageImport.h"
#include "EditorProjectCodec.h"
#include "EditorStillTiming.h"
#include <iiSocietyHelper.h>
#include <iiSharedCanvas/Validation/Validation.h>
#include <iiSharedCanvas/Media/MediaIo.h>
#include <QFileInfo>
#include <QDateTime>
#include <QSet>
#include <algorithm>
#include <charconv>
#include <limits>
#include <set>
#include <stdexcept>

namespace dreamscapes {
using namespace iiSharedCanvas;
namespace {
void remapMasks(LayerAppearance &appearance,const std::string &prefix,CanvasOrigin origin) {
    const auto remap=[&](LayerMask &mask) {
        mask.assetId=prefix+mask.assetId;
        if(!mask.linkedToLayer){mask.transform.translationX-=origin.x;mask.transform.translationY-=origin.y;}
    };
    for(auto *mask:{&appearance.rasterMask,&appearance.vectorMask,&appearance.filterMask})if(*mask)remap(**mask);
    for(auto &entry:appearance.maskStack){entry.id=prefix+entry.id;remap(entry.mask);}
}
std::set<int> usedPrefixes(const Document &document) {
    std::set<int> used;
    const auto add=[&](const std::string &id) {
        constexpr std::string_view prefix="attachment.";
        if(!id.starts_with(prefix))return;
        const auto end=id.find(':',prefix.size());if(end==std::string::npos)return;
        int number=0;const auto parsed=std::from_chars(id.data()+prefix.size(),id.data()+end,number);
        if(parsed.ec==std::errc{}&&parsed.ptr==id.data()+end&&number>=0)used.insert(number);
    };
    for(const auto &asset:document.assets)add(assetId(asset));
    for(const auto &board:document.artboards)add(board.id);
    for(const auto &layer:document.layers)add(layerProperties(layer).id);
    for(const auto &asset:document.audioAssets)add(asset.id);
    for(const auto &track:document.audioTracks){add(track.id);for(const auto &clip:track.clips)add(clip.id);}
    return used;
}
struct Original {
    QString path,canonical;
    qint64 size=0,modified=0;
    bool native=false;
};
bool unchanged(const Original &original) {
    const QFileInfo current(original.path);
    return current.isFile()&&!current.isSymLink()&&!current.isJunction()
        &&current.canonicalFilePath()==original.canonical&&current.size()==original.size
        &&current.lastModified().toMSecsSinceEpoch()==original.modified;
}
}
void appendImportedArtboards(Document &target, Document source, const std::string &name, int number, std::int64_t &x) {
    if (source.canvasMode != CanvasMode::Finite) throw std::runtime_error("Attach finite image canvases; open infinite documents directly.");
    const auto view = documentViewRegion(source);
    if (view.extent.width <= 0 || x + view.extent.width > std::numeric_limits<std::int32_t>::max())
        throw std::runtime_error("The horizontal artboard workspace exceeds its geometry limit.");
    if (number == 0) { target.extent = source.extent; target.timeline = source.timeline; target.stableDiffusionMetadata = source.stableDiffusionMetadata; }
    else if (std::uint64_t(target.timeline.frameRate.numerator) * source.timeline.frameRate.denominator != std::uint64_t(source.timeline.frameRate.numerator) * target.timeline.frameRate.denominator) throw std::runtime_error("Attached canvases must use the same timeline frame rate.");
    target.timeline.frameCount = std::max(target.timeline.frameCount, source.timeline.frameCount);
    const auto prefix = "attachment." + std::to_string(number) + ":";
    const auto looseBoard = prefix + "canvas";
    const bool hasLoose = source.artboards.empty() || std::ranges::any_of(source.layers, [](const Layer &layer) { return !layerProperties(layer).artboardId; });
    if (hasLoose) target.artboards.push_back({looseBoard, name, {{int(x), 0}, view.extent}, 0});
    for (auto &board : source.artboards) {
        board.id = prefix + board.id;
        board.region.origin.x = int(std::int64_t(board.region.origin.x) + x - view.origin.x);
        board.region.origin.y = int(std::int64_t(board.region.origin.y) - view.origin.y);
        target.artboards.push_back(std::move(board));
    }
    for (auto &asset : source.assets) { std::visit([&](auto &value) { value.id = prefix + value.id; }, asset); target.assets.push_back(std::move(asset)); }
    for (auto &layer : source.layers) {
        auto &p = layerProperties(layer); p.id = prefix + p.id;
        remapMasks(p.appearance,prefix,p.artboardId ? CanvasOrigin{} : view.origin);
        if (p.artboardId) p.artboardId = prefix + *p.artboardId;
        else { p.artboardId = looseBoard; p.transform.translationX -= view.origin.x; p.transform.translationY -= view.origin.y; }
        auto sourceValue = layerSource(layer);
        if (auto *fixed = std::get_if<StaticSource>(&sourceValue)) fixed->assetId = prefix + fixed->assetId;
        setLayerSource(layer, std::move(sourceValue)); target.layers.push_back(std::move(layer));
    }
    for (auto &frame : source.frames) {
        for (auto &key : frame.keyframes) { key.layerId = prefix + key.layerId; key.assetId = prefix + key.assetId; }
        auto existing = std::ranges::find(target.frames, frame.index, &Frame::index);
        if (existing == target.frames.end()) target.frames.push_back(std::move(frame));
        else existing->keyframes.insert(existing->keyframes.end(), frame.keyframes.begin(), frame.keyframes.end());
    }
    std::ranges::sort(target.frames, {}, &Frame::index);
    for (auto &asset : source.audioAssets) { asset.id = prefix + asset.id; target.audioAssets.push_back(std::move(asset)); }
    for (auto &track : source.audioTracks) {
        track.id = prefix + track.id;
        for (auto &clip : track.clips) { clip.id = prefix + clip.id; clip.assetId = prefix + clip.assetId; }
        target.audioTracks.push_back(std::move(track));
    }
    x += view.extent.width + 64;
    recordDocumentChange(target);
}
ImageImportResult prepareImageImport(const ImageImportRequest &request) {
    ImageImportResult result;
    const auto cancelled=[&]{return request.cancelled&&request.cancelled->load();};
    const auto abandon=[&] {
        result.document={};result.pageSources.clear();result.cancelled=true;
        return std::move(result);
    };
    try {
        if(request.sources.empty()||request.sources.size()>MaximumProjectPages)throw std::runtime_error("Select between 1 and 1000 images.");
        if(cancelled())return abandon();
        if(request.base) {
            result.document=*request.base;
            if(request.artboardVisibility.size()!=result.document.artboards.size()
                ||request.layerVisibility.size()!=result.document.layers.size())throw std::runtime_error("The captured artboard state is invalid.");
            for(std::size_t i=0;i<result.document.artboards.size();++i)result.document.artboards[i].visible=request.artboardVisibility[i];
            for(std::size_t i=0;i<result.document.layers.size();++i)layerProperties(result.document.layers[i]).visible=request.layerVisibility[i];
            if(result.document.canvasMode!=CanvasMode::Finite)throw std::runtime_error("Attach artboards to a finite project.");
            if(result.document.artboards.empty()) {
                const auto region=documentViewRegion(result.document);
                result.document.artboards.push_back({"project.canvas",request.baseName,region,0});
                for(auto &layer:result.document.layers) {
                    auto &p=layerProperties(layer);p.artboardId="project.canvas";
                    p.transform.translationX-=region.origin.x;p.transform.translationY-=region.origin.y;
                    remapMasks(p.appearance,{},region.origin);
                }
            }
        }
        result.firstNew=int(result.document.artboards.size());
        std::uint64_t pixels=0;
        for(const auto &board:result.document.artboards) {
            const auto count=std::uint64_t(board.region.extent.width)*board.region.extent.height;
            if(count>MediaLimits{}.maxPixelsPerFrame-pixels)throw std::runtime_error("Selected images exceed the project import limit.");
            pixels+=count;
        }
        const auto view=documentViewRegion(result.document);
        std::int64_t x=request.base?std::int64_t(view.origin.x)+view.extent.width+64:0;
        int number=result.firstNew;auto used=usedPrefixes(result.document);QSet<QString> paths;std::vector<Original> originals;
        iiSocietyHelper::FileSystem files(iiSocietyHelper::FileSystem::Initialization::Deferred);
        for(std::size_t input=0;input<request.sources.size();++input) {
            if(cancelled())return abandon();
            const auto path=QString::fromStdU16String(request.sources[input].u16string());const QFileInfo info(path);
            const auto canonical=info.canonicalFilePath();if(!canonical.isEmpty()&&paths.contains(canonical))continue;
            Original original{path,canonical,info.size(),info.lastModified().toMSecsSinceEpoch(),info.suffix().compare("iisc",Qt::CaseInsensitive)==0};
            auto imported=files.readCanvas(path);
            if(!imported.ok())throw std::runtime_error(imported.result.message);
            if(!unchanged(original))throw std::runtime_error("A selected original changed while preparing. Select it again.");
            normalizeImplicitStill(imported.document);
            const auto extent=documentViewRegion(imported.document).extent;
            if(extent.width<=0||extent.height<=0)throw std::runtime_error("The imported canvas has no valid extent.");
            const auto count=std::uint64_t(extent.width)*extent.height;
            if(count>MediaLimits{}.maxPixelsPerFrame-pixels)throw std::runtime_error("Selected images exceed the project import limit.");
            pixels+=count;
            while(used.contains(number)) {
                if(number==std::numeric_limits<int>::max())throw std::runtime_error("The attachment identity limit was reached.");
                ++number;
            }
            const auto first=result.document.artboards.size();
            appendImportedArtboards(result.document,std::move(imported.document),info.fileName().toStdString(),number,x);used.insert(number);
            if(number<std::numeric_limits<int>::max())++number;
            if(result.document.artboards.size()>MaximumProjectPages)throw std::runtime_error("The project supports at most 1000 artboards.");
            result.pageSources.insert(result.pageSources.end(),result.document.artboards.size()-first,input);
            paths.insert(canonical);originals.push_back(std::move(original));
        }
        const auto validation=validate(result.document);
        if(!validation.ok())throw std::runtime_error(validation.issues.front().message);
        // Invalid, incompatible and oversized batches never enter managed storage.
        // Each existing SDK import remains an individually durable managed file.
        for(const auto &original:originals)if(!unchanged(original))throw std::runtime_error("A selected original changed while preparing. Select it again.");
        const auto root=QString::fromStdU16String(request.storageRoot.u16string());bool storageOpened=false;
        for(const auto &original:originals) {
            if(cancelled())return abandon();
            if(!original.native) {
                if(root.isEmpty())storageOpened=files.openForSource(original.path);
                else if(!storageOpened)storageOpened=files.open(root);
                if(!storageOpened||files.importCanvas(original.path).value("path").toString().isEmpty())
                    throw std::runtime_error(files.errorString().toStdString());
            }
            if(!unchanged(original))throw std::runtime_error("A selected original changed while preparing. Select it again.");
        }
        result.cancelled=cancelled();
    } catch(const std::exception &error) {result.error=error.what();}
    catch(...) {result.error="The originals could not be prepared.";}
    if(!result.ok()){result.document={};result.pageSources.clear();}
    return result;
}
}
