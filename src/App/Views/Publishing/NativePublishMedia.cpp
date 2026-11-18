#include "NativePublishMedia.h"
#include <Media/MediaIo.h>
#include <Render/FrameRenderer.h>
#include <Validation/Validation.h>
#include <iiFileProvider>
#include <iiVisualCodec>
#include <algorithm>
#include <limits>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <utility>

namespace dreamscapes::publishing {
using ii::core::ErrorCode;
struct PublicationStaging final {
    explicit PublicationStaging(const std::filesystem::path &root):directory(root) {}
    iiFileProvider::native::TemporaryDirectory directory;
};
namespace {
std::vector<iiSharedCanvas::CanvasPublicationMetadata> canvasMetadata(const iiSharedCanvas::Document *document) {
    return document && document->publicationMetadata?document->publicationMetadata->canvases:
        std::vector<iiSharedCanvas::CanvasPublicationMetadata>{};
}
}
ii::core::Result<PreparedPublicationMedia> preparePublicationMedia(
    std::shared_ptr<const iiSharedCanvas::Document> document,iiSharedCanvas::FrameIndex frame,
    const std::filesystem::path &stagingRoot,std::function<bool()> cancelled,PublicationMediaLimits limits) {
    using namespace iiSharedCanvas;
    if (!document) return ii::core::fail(ErrorCode::closed,"Open a canvas first");
    if (frame>=document->timeline.frameCount) return ii::core::fail(ErrorCode::out_of_range,"Publication frame is outside the canvas");
    if (!limits.maximumCanvases || !limits.maximumTotalPixels || !limits.maximumTotalBytes)
        return ii::core::fail(ErrorCode::invalid_argument,"Invalid publication media limits");
    try {
        if (cancelled && cancelled()) return ii::core::fail(ErrorCode::cancelled,"Publication preparation was cancelled");
        const auto checked=validate(*document);
        if (!checked.ok()) return ii::core::fail(ErrorCode::invalid_argument,checked.issues.front().message);
        auto canvases=canvasMetadata(document.get());
        if (canvases.empty()) {
            if (document->artboards.empty()) canvases.push_back({});
            else for (const auto &board:document->artboards) if (board.visible) canvases.push_back({board.id,{},{}});
        }
        if (canvases.empty()) return ii::core::fail(ErrorCode::unsupported,"No visible canvas is available for publication");
        if (canvases.size()>limits.maximumCanvases) return ii::core::fail(ErrorCode::out_of_range,"Publication contains too many canvases");
        if (document->canvasMode==CanvasMode::Infinite && std::ranges::any_of(canvases,[](const auto &entry){return entry.artboardId.empty();}))
            return ii::core::fail(ErrorCode::unsupported,"Choose finite artboards before preparing an infinite canvas");
        std::error_code error;const auto root=stagingRoot.empty()?std::filesystem::temp_directory_path(error):stagingRoot;
        if (error || root.empty()) return ii::core::fail(ErrorCode::failed,"Publication staging directory is unavailable");
        auto staging=std::make_shared<PublicationStaging>(root);
        if (!staging->directory.isValid()) return ii::core::fail(ErrorCode::failed,"Cannot create publication staging");
        PreparedPublicationMedia output;output.staging=staging;output.frame=frame;
        if (document->publicationMetadata) {
            output.source.post.title=document->publicationMetadata->title;
            output.source.post.text=document->publicationMetadata->description;
        }
        output.source.post.media.reserve(canvases.size());output.source.captions.reserve(canvases.size());
        std::uint64_t totalPixels=0,totalBytes=0;
        for (std::size_t index=0;index<canvases.size();++index) {
            if (cancelled && cancelled()) return ii::core::fail(ErrorCode::cancelled,"Publication preparation was cancelled");
            const auto &canvas=canvases[index];
            auto extent=document->extent;
            if (!canvas.artboardId.empty()) {
                const auto board=std::ranges::find_if(document->artboards,[&](const auto &entry){return entry.id==canvas.artboardId;});
                if (board==document->artboards.end()) return ii::core::fail(ErrorCode::invalid_argument,"Publication artboard is unavailable");
                extent=board->region.extent;
            }
            if (extent.width<=0 || extent.height<=0) return ii::core::fail(ErrorCode::invalid_argument,"Invalid publication canvas dimensions");
            const auto requestedPixels=std::uint64_t(extent.width)*extent.height;
            const ii::visual::image::Limits codecLimits;
            if (static_cast<std::uint64_t>(extent.width)>codecLimits.dimension ||
                static_cast<std::uint64_t>(extent.height)>codecLimits.dimension ||
                requestedPixels>codecLimits.pixel_bytes/4 || requestedPixels>limits.maximumTotalPixels-totalPixels ||
                requestedPixels>std::numeric_limits<std::size_t>::max()/4)
                return ii::core::fail(ErrorCode::out_of_range,"Publication images exceed the pixel limit");
            auto rendered=canvas.artboardId.empty()?renderFrameRegion(*document,frame,canvasRegion(*document),extent):
                renderArtboard(*document,frame,canvas.artboardId);
            if (!rendered.ok()) return ii::core::fail(ErrorCode::failed,rendered.message.empty()?"Cannot render a publication canvas":rendered.message);
            const auto &pixels=rendered.pixels;
            if (pixels.width!=extent.width || pixels.height!=extent.height || std::uint64_t(pixels.width)*pixels.height!=pixels.pixels.size())
                return ii::core::fail(ErrorCode::invalid_argument,"Publication renderer returned invalid pixels");
            const auto count=std::uint64_t(pixels.width)*pixels.height;
            if (count>limits.maximumTotalPixels-totalPixels || count>std::numeric_limits<std::size_t>::max()/4)
                return ii::core::fail(ErrorCode::out_of_range,"Publication images exceed the pixel limit");
            totalPixels+=count;
            std::vector<std::uint8_t> rgba;rgba.reserve(static_cast<std::size_t>(count)*4);
            for (const auto pixel:pixels.pixels) {
                rgba.push_back(static_cast<std::uint8_t>(pixel>>16));rgba.push_back(static_cast<std::uint8_t>(pixel>>8));
                rgba.push_back(static_cast<std::uint8_t>(pixel));rgba.push_back(static_cast<std::uint8_t>(pixel>>24));
            }
            auto encoded=ii::visual::image::encode_png({static_cast<std::uint32_t>(pixels.width),
                static_cast<std::uint32_t>(pixels.height),ii::visual::image::PixelFormat::rgba8,0,rgba});
            if (!encoded) return ii::core::fail(ErrorCode::failed,encoded.error().message.empty()?"Cannot encode publication media":encoded.error().message);
            if (encoded->size()>limits.maximumTotalBytes-totalBytes)
                return ii::core::fail(ErrorCode::out_of_range,"Publication images exceed the staging byte limit");
            totalBytes+=encoded->size();
            if (cancelled && cancelled()) return ii::core::fail(ErrorCode::cancelled,"Publication preparation was cancelled");
            const auto path=staging->directory.path()/("canvas-"+std::to_string(index+1)+".png");
            iiFileProvider::native::File::create(path,std::string_view(reinterpret_cast<const char *>(encoded->data()),encoded->size()));
            iiSocialIntegration::Media media;media.kind=iiSocialIntegration::MediaKind::Image;media.localFile=path;
            media.mimeType="image/png";media.altText=canvas.alternativeText;
            output.source.post.media.push_back(std::move(media));output.source.captions.push_back(canvas.caption);
        }
        return output;
    } catch (...) {return ii::core::fail(ErrorCode::failed,"Cannot prepare publication media",std::current_exception());}
}

struct PublicationMediaModel::Bridge final {
    struct Completion final {std::uint64_t id;ii::core::Result<std::shared_ptr<const PreparedPublicationMedia>> result;};
    std::mutex mutex;std::optional<Completion> completion;
    ii::runtime::EventLoop::Sender sender;ii::runtime::TaskToken wake;bool closed=false;
};
PublicationMediaModel::PublicationMediaModel(std::shared_ptr<native::DocumentSession> document,
    ii::runtime::EventLoop::Sender sender,std::shared_ptr<iiSharedCanvas::FrameRenderExecutor> executor,
    std::filesystem::path root,PublicationMediaLimits limits):document_(std::move(document)),sender_(std::move(sender)),
    executor_(std::move(executor)),stagingRoot_(std::move(root)),limits_(limits) {
    if (!document_ || !executor_) throw std::invalid_argument("Publication media requires a document and worker");
}
std::shared_ptr<PublicationMediaModel> PublicationMediaModel::create(std::shared_ptr<native::DocumentSession> document,
    ii::runtime::EventLoop &loop,std::shared_ptr<iiSharedCanvas::FrameRenderExecutor> executor,
    std::filesystem::path root,PublicationMediaLimits limits) {
    auto value=std::shared_ptr<PublicationMediaModel>(new PublicationMediaModel(std::move(document),loop.sender(),
        std::move(executor),std::move(root),limits));value->start();return value;
}
void PublicationMediaModel::start() {
    const std::weak_ptr<PublicationMediaModel> weak=shared_from_this();
    auto admitted=sender_.on_signal([weak]{if (auto self=weak.lock()) self->deliver();});
    if (!admitted) throw std::runtime_error("Cannot register publication media completion");
    wake_=admitted.token;bridge_=std::make_shared<Bridge>();bridge_->sender=sender_;bridge_->wake=wake_;
    canvases_=canvasMetadata(document_->document());
    auto changed=[weak](const auto &,std::uint64_t){if (auto self=weak.lock()) self->request();};
    generation_=document_->documentGeneration.observe(changed);visual_=document_->visualRevision.observe(changed);
    frame_=document_->frame.observe(changed);
    revision_=document_->revision.observe([weak](const auto &,std::uint64_t){if (auto self=weak.lock()) self->metadataChanged();});
}
PublicationMediaModel::~PublicationMediaModel() {if (writable()) try {close();} catch (...) {}}
bool PublicationMediaModel::writable() const noexcept {return active_ && owner_==std::this_thread::get_id();}
void PublicationMediaModel::retire(std::shared_ptr<const PreparedPublicationMedia> old) {
    if (!old || old.use_count()!=1 || !executor_) return;
    try {executor_->post([old=std::move(old)] {});} catch (...) {}
}
void PublicationMediaModel::metadataChanged() {
    if (!writable()) return;
    auto next=canvasMetadata(document_->document());if (next==canvases_) return;
    canvases_=std::move(next);request();
}
void PublicationMediaModel::setVisible(bool value) {
    if (!writable() || visible_==value) return;visible_=value;request();
}
void PublicationMediaModel::request() {
    if (!writable()) return;
    if (desired_==std::numeric_limits<std::uint64_t>::max()) {(void)status.set("Publication request limit reached");return;}
    const auto request=++desired_;if (cancellation_) cancellation_->store(true,std::memory_order_relaxed);
    auto old=prepared.get();(void)prepared.set({});retire(std::move(old));
    if (!writable() || request!=desired_) return;
    if (!visible_) {(void)busy.set(false);(void)status.set({});return;}
    if (!document_->document()) {(void)busy.set(false);(void)status.set("Open a canvas first");return;}
    (void)busy.set(true);(void)status.set("Preparing current-frame media…");
    if (!inFlight_) schedule();
}
void PublicationMediaModel::schedule() {
    if (!writable() || !visible_ || inFlight_ || !document_->document()) return;
    auto source=document_->snapshot();const auto frame=document_->frame.get();const auto id=desired_;
    auto bridge=bridge_;const auto root=stagingRoot_;const auto limits=limits_;
    std::shared_ptr<std::atomic_bool> cancellation;
    try {cancellation=std::make_shared<std::atomic_bool>(false);}
    catch (...) {(void)busy.set(false);(void)status.set("Cannot schedule publication media");return;}
    cancellation_=cancellation;inFlight_=id;
    try {
        executor_->post([source=std::move(source),frame,id,root,limits,bridge,cancellation] {
            ii::core::Result<std::shared_ptr<const PreparedPublicationMedia>> result=ii::core::fail(ErrorCode::failed,"Cannot prepare publication media");
            try {
                auto value=preparePublicationMedia(source,static_cast<iiSharedCanvas::FrameIndex>(frame),root,
                    [cancellation]{return cancellation->load(std::memory_order_relaxed);},limits);
                if (!value) result=std::unexpected(value.error());
                else result=std::make_shared<const PreparedPublicationMedia>(std::move(*value));
            } catch (...) {result=ii::core::fail(ErrorCode::failed,"Cannot prepare publication media",std::current_exception());}
            {std::lock_guard lock(bridge->mutex);if (bridge->closed) return;bridge->completion.emplace(Bridge::Completion{id,std::move(result)});}
            (void)bridge->sender.signal(bridge->wake,ii::runtime::Clock::now());
        });
    } catch (...) {inFlight_=0;cancellation_.reset();(void)busy.set(false);(void)status.set("Publication worker is unavailable");}
}
void PublicationMediaModel::deliver() {
    if (!writable()) return;
    std::optional<Bridge::Completion> completion;
    {std::lock_guard lock(bridge_->mutex);completion=std::move(bridge_->completion);bridge_->completion.reset();}
    if (!completion || completion->id!=inFlight_) return;
    inFlight_=0;cancellation_.reset();
    if (completion->id==desired_ && visible_) {
        if (completion->result) {
            (void)prepared.set(std::move(*completion->result));
            if (writable() && completion->id==desired_ && visible_) (void)status.set("Current-frame media ready");
        } else (void)status.set(completion->result.error().message);
        if (writable() && completion->id==desired_) (void)busy.set(false);
    } else if (completion->result) retire(std::move(*completion->result));
    if (writable() && visible_ && desired_!=completion->id) schedule();
}
void PublicationMediaModel::close() {
    if (owner_!=std::this_thread::get_id()) throw std::logic_error("Publication media requires its owner thread");
    if (!active_) return;active_=false;visible_=false;generation_={};visual_={};frame_={};revision_={};
    if (cancellation_) cancellation_->store(true,std::memory_order_relaxed);cancellation_.reset();
    auto old=prepared.get();(void)prepared.set({});retire(std::move(old));
    if (bridge_) {std::lock_guard lock(bridge_->mutex);bridge_->closed=true;bridge_->completion.reset();}
    (void)sender_.cancel(wake_);executor_.reset();(void)busy.set(false);
}
}
