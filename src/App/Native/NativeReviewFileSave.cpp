#include "NativeReviewFileSave.h"
#include <iiFileProvider>
#include <algorithm>
#include <array>
#include <chrono>
#include <limits>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <utility>

namespace dreamscapes::native {
namespace {
namespace fs=std::filesystem;
using ii::core::ErrorCode;
std::string utf8(const fs::path &path) {
    const auto text=path.u8string();return {text.begin(),text.end()};
}
fs::path pathFrom(std::string_view input) {
    std::u8string bytes;bytes.reserve(input.size());
    for (unsigned char byte:input) bytes.push_back(static_cast<char8_t>(byte));
    return fs::path(bytes);
}
bool validPath(const fs::path &path) {
    const auto value=utf8(path);
    return path.is_absolute() && !path.filename().empty() && path.lexically_normal()==path &&
        value.find('\0')==std::string::npos;
}
std::string extension(const fs::path &path) {return vm::lower(utf8(path.extension()));}
bool supported(const fs::path &path) {
    static constexpr std::array<std::string_view,16> suffixes{".png",".jpg",".jpeg",".webp",".bmp",".gif",".tif",".tiff",
        ".avif",".heic",".heif",".mp4",".mov",".m4v",".webm",".mkv"};
    const auto suffix=extension(path);
    return validPath(path) && std::ranges::find(suffixes,suffix)!=suffixes.end();
}
std::string revision(const fs::path &path) {
    return std::to_string(std::chrono::duration_cast<std::chrono::microseconds>(
        fs::last_write_time(path).time_since_epoch()).count())+"-"+std::to_string(fs::file_size(path));
}
ii::core::Result<void> copyOriginal(const fs::path &source,const fs::path &destination,
                                   const std::string &expected,const std::shared_ptr<std::atomic_bool> &canceled) {
    try {
        if (canceled->load()) return ii::core::fail(ErrorCode::closed,"Save was canceled");
        const auto identity=iiFileProvider::ObjectSource::inspect(source);
        if (!expected.empty() && revision(source)!=expected)
            return ii::core::fail(ErrorCode::busy,"The result changed; refresh the history before saving");
        iiFileProvider::native::TemporaryDirectory temporary(destination.parent_path());
        if (!temporary.isValid()) return ii::core::fail(ErrorCode::failed,"Cannot prepare the destination directory");
        iiFileProvider::ObjectSource snapshot(source,temporary.path(),{},identity);
        if (!expected.empty() && revision(source)!=expected)
            return ii::core::fail(ErrorCode::busy,"The result changed while it was being saved");
        if (canceled->load()) return ii::core::fail(ErrorCode::closed,"Save was canceled");
        iiFileProvider::native::Device output(false);
        output.open(snapshot.path(),{.write=true,.existing=true,.noFollow=true});
        output.flush(true);output.close();
        if (canceled->load()) return ii::core::fail(ErrorCode::closed,"Save was canceled");
        const auto result=iiFileProvider::native::publishFile(snapshot.path(),destination,false);
        if (!result.succeeded) return ii::core::fail(ErrorCode::failed,
            result.message.empty()?"Cannot save the result; the destination may already exist":result.message);
        return {};
    } catch (const std::exception &error) {
        return ii::core::fail(ErrorCode::failed,std::string("Cannot save the result: ")+error.what());
    } catch (...) {return ii::core::fail(ErrorCode::failed,"Cannot save the result",std::current_exception());}
}
}
struct ReviewFileSave::Request final {fs::path source;std::string revision;};
struct ReviewFileSave::Bridge final {
    struct Completion final {std::uint64_t id;ii::core::Result<void> result;};
    std::mutex mutex;
    std::optional<Completion> completion;
    ii::runtime::EventLoop::Sender sender;
    ii::runtime::TaskToken wake;
    bool closed=false;
};
ReviewFileSave::ReviewFileSave(ii::runtime::EventLoop::Sender sender,CanvasFileModel::Submit submit)
    :sender_(std::move(sender)),submit_(std::move(submit)) {
    if (!submit_) throw std::invalid_argument("Review file save requires a worker");
}
std::shared_ptr<ReviewFileSave> ReviewFileSave::create(ii::runtime::EventLoop &loop,CanvasFileModel::Submit submit) {
    auto model=std::shared_ptr<ReviewFileSave>(new ReviewFileSave(loop.sender(),std::move(submit)));
    model->start();return model;
}
void ReviewFileSave::start() {
    const std::weak_ptr<ReviewFileSave> weak=shared_from_this();
    auto admitted=sender_.on_signal([weak] {if (auto self=weak.lock()) self->deliver();});
    if (!admitted) throw std::runtime_error("Cannot register review save completion");
    wake_=admitted.token;bridge_=std::make_shared<Bridge>();bridge_->sender=sender_;bridge_->wake=wake_;
}
ReviewFileSave::~ReviewFileSave() {if (active_ && owner_==std::this_thread::get_id()) try {close();} catch (...) {}}
void ReviewFileSave::setPicker(Picker value) {
    if (owner_!=std::this_thread::get_id() || !active_) return;
    picker_=std::move(value);
    if (dialogPending_) {dialogPending_=false;current_=0;finish({});}
}
bool ReviewFileSave::canSave(const vm::Value &entry) const {
    if (owner_!=std::this_thread::get_id() || !active_ || !picker_ || busy.get()) return false;
    const auto value=vm::text(vm::get(entry,"path"));
    return !value.empty() && supported(pathFrom(value));
}
ii::core::Result<void> ReviewFileSave::save(const vm::Value &entry) {
    if (owner_!=std::this_thread::get_id()) return ii::core::fail(ErrorCode::wrong_executor,"Review save requires its owner thread");
    if (!active_) return ii::core::fail(ErrorCode::closed,"Review save is closed");
    if (!canSave(entry)) return ii::core::fail(ErrorCode::unsupported,"This result has no available local original");
    if (next_==std::numeric_limits<std::uint64_t>::max()) return ii::core::fail(ErrorCode::out_of_range,"Review save identity exhausted");
    Request request{pathFrom(vm::text(vm::get(entry,"path"))),vm::text(vm::get(entry,"sourceRevision"))};
    ii::gui::FileDialogOptions options;
    options.mode=ii::gui::FileDialogMode::save;options.title="Save result";options.filename=utf8(request.source.filename());
    options.filters={{"Original format",{extension(request.source).substr(1)}}};
    const auto id=++next_;current_=id;dialogPending_=true;
    (void)busy.set(true);
    if (!active_ || current_!=id) return {};
    (void)status.set({});
    if (!active_ || current_!=id) return {};
    const std::weak_ptr<ReviewFileSave> weak=shared_from_this();auto picker=picker_;
    ii::core::Result<void> started;
    try {started=picker(std::move(options),[weak,id,request=std::move(request)](auto selected) mutable {
        if (auto self=weak.lock()) self->picked(id,std::move(request),std::move(selected));
    });} catch (...) {started=ii::core::fail(ErrorCode::failed,"Cannot open the save dialog",std::current_exception());}
    if (!started && active_ && current_==id && dialogPending_) {
        const auto message=started.error().message;finish(message);return started;
    }
    return {};
}
void ReviewFileSave::picked(std::uint64_t id,Request request,ii::core::Result<ii::gui::FileDialogSelection> selected) {
    if (owner_!=std::this_thread::get_id() || !active_ || current_!=id || !dialogPending_) return;
    dialogPending_=false;
    if (!selected) {finish(selected.error().message);return;}
    if (selected->canceled) {finish({});return;}
    if (selected->paths.size()!=1) {finish("Choose one destination file");return;}
    auto destination=pathFrom(selected->paths.front());
    if (destination.extension().empty()) destination+=request.source.extension();
    if (!validPath(destination) || extension(destination)!=extension(request.source)) {
        finish("The destination must use the original file extension");return;
    }
    auto bridge=bridge_;canceled_=std::make_shared<std::atomic_bool>(false);auto canceled=canceled_;
    (void)status.set("Saving result…");
    if (!active_ || current_!=id || canceled->load()) return;
    bool accepted=false;
    try {accepted=submit_([bridge,canceled,id,request=std::move(request),destination=std::move(destination)] {
        auto result=copyOriginal(request.source,destination,request.revision,canceled);
        {std::lock_guard lock(bridge->mutex);if (bridge->closed) return;bridge->completion.emplace(Bridge::Completion{id,std::move(result)});}
        (void)bridge->sender.signal(bridge->wake,ii::runtime::Clock::now());
    });} catch (...) {accepted=false;}
    if (!accepted && active_ && current_==id) finish("Review save worker is unavailable");
}
void ReviewFileSave::deliver() {
    if (!active_) return;
    std::optional<Bridge::Completion> completion;
    {std::lock_guard lock(bridge_->mutex);completion=std::move(bridge_->completion);bridge_->completion.reset();}
    if (!completion || completion->id!=current_) return;
    finish(completion->result?"Result saved":completion->result.error().message);
}
void ReviewFileSave::finish(std::string message) {
    if (!active_) return;
    current_=0;dialogPending_=false;canceled_.reset();
    (void)status.set(std::move(message));
    if (active_) (void)busy.set(false);
}
void ReviewFileSave::close() {
    if (owner_!=std::this_thread::get_id()) throw std::logic_error("Review save requires its owner thread");
    if (!active_) return;
    active_=false;current_=0;dialogPending_=false;picker_={};submit_={};
    if (canceled_) canceled_->store(true);
    if (bridge_) {
        std::lock_guard lock(bridge_->mutex);bridge_->closed=true;bridge_->completion.reset();
    }
    (void)sender_.cancel(wake_);(void)busy.set(false);
}
}
