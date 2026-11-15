#include "NativeGenerationPreferences.h"
#include "App/Generation/ModelPreferences.h"
#include <iiFileProviderNative.h>
#include <algorithm>
#include <array>
#include <cstdlib>
#include <limits>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <utility>

namespace dreamscapes::preferences {
namespace {
using ii::core::ErrorCode;
constexpr std::array<std::string_view,4> categories{"Image","Video","Audio","Agents"};
struct Loaded final {
    ModelPreferences value;
    std::optional<std::string> bytes;
    std::string error;
};
Loaded loadFile(const std::filesystem::path &path) {
    Loaded result;
    try {
        std::error_code error;
        const auto exists=std::filesystem::exists(path,error);
        if (error) {result.error="Cannot inspect generation preferences: "+error.message();return result;}
        if (!exists) return result;
        result.bytes=iiFileProvider::native::File::read(path,1024*1024);
        if (!parseModelPreferences(*result.bytes,result.value,result.error)) result.bytes.reset();
    } catch (const std::exception &failure) {result.error="Cannot read generation preferences: "+std::string(failure.what());}
    return result;
}
std::string saveFile(const std::filesystem::path &path,const std::optional<std::string> &expected,
                     const std::string &replacement) {
    try {
        if (!path.parent_path().empty()) iiFileProvider::native::File::createDirectories(path.parent_path());
        if (expected) iiFileProvider::native::File::update(path,*expected,replacement);
        else iiFileProvider::native::File::create(path,replacement);
        return {};
    } catch (const iiFileProvider::native::FileError &failure) {
        if (failure.code()==iiFileProvider::native::FileCode::Conflict ||
            failure.code()==iiFileProvider::native::FileCode::AlreadyExists)
            return "Generation preferences changed outside Dreamscapes. Restart to reload before saving.";
        return "Cannot save generation preferences: "+std::string(failure.what());
    } catch (const std::exception &failure) {return "Cannot save generation preferences: "+std::string(failure.what());}
}
bool validPrompt(std::string_view text) {
    std::size_t units=0;
    for (std::size_t i=0;i<text.size();) {
        const auto first=static_cast<unsigned char>(text[i]);
        std::uint32_t code=0; std::size_t count=0;
        if (first<0x80) {code=first;count=1;}
        else if (first>=0xc2 && first<=0xdf) {code=first&0x1f;count=2;}
        else if (first>=0xe0 && first<=0xef) {code=first&0x0f;count=3;}
        else if (first>=0xf0 && first<=0xf4) {code=first&0x07;count=4;}
        else return false;
        if (i+count>text.size()) return false;
        for (std::size_t j=1;j<count;++j) {
            const auto next=static_cast<unsigned char>(text[i+j]);
            if ((next&0xc0)!=0x80) return false;
            code=(code<<6)|(next&0x3f);
        }
        if (!code || (count==3 && code<0x800) || (count==4 && code<0x10000) ||
            (code>=0xd800 && code<=0xdfff) || code>0x10ffff) return false;
        units+=code>0xffff?2:1;
        if (units>32000) return false;
        i+=count;
    }
    return true;
}
std::size_t quotedBytes(std::string_view value) {
    std::size_t size=value.size();
    for (const auto byte:value) if (byte=='"' || byte=='\\') ++size;
    return size;
}
}
std::filesystem::path generationPreferencesPath() {
#if defined(_WIN32)
    if (const auto *base=::_wgetenv(L"LOCALAPPDATA"); base && *base)
        return std::filesystem::path(base)/"Dreamscapes"/"generation-models.conf";
#elif defined(__APPLE__)
    if (const auto *home=std::getenv("HOME"); home && *home)
        return std::filesystem::path(home)/"Library"/"Preferences"/"Dreamscapes"/"generation-models.conf";
#else
    if (const auto *base=std::getenv("XDG_CONFIG_HOME"); base && *base)
        return std::filesystem::path(base)/"Dreamscapes"/"generation-models.conf";
    if (const auto *home=std::getenv("HOME"); home && *home)
        return std::filesystem::path(home)/".config"/"Dreamscapes"/"generation-models.conf";
#endif
    return {};
}
struct GenerationPreferences::Bridge final {
    enum class Kind {load,save};
    struct Request {std::uint64_t version; ModelPreferences value;};
    struct Completion {Kind kind; std::uint64_t version; Loaded loaded;};
    std::mutex mutex;
    std::filesystem::path path;
    ii::runtime::EventLoop::Sender sender;
    ii::runtime::TaskToken wake;
    ModelPreferences desired,confirmed;
    std::optional<std::string> expected;
    std::optional<Request> pending;
    std::optional<Completion> completion;
    std::uint64_t version=0;
    std::size_t serializedSize=0;
    bool ready=false,running=false,blocked=false,notify=true;
};
GenerationPreferences::GenerationPreferences(std::shared_ptr<WorkspaceModel> workspace,
    ii::runtime::EventLoop::Sender sender,Submit submit,std::filesystem::path path)
    : workspace_(std::move(workspace)),sender_(std::move(sender)),submit_(std::move(submit)),path_(std::move(path)) {
    if (!workspace_ || !submit_ || path_.empty()) throw std::invalid_argument("Generation preferences require a path and worker");
}
std::shared_ptr<GenerationPreferences> GenerationPreferences::create(std::shared_ptr<WorkspaceModel> workspace,
    ii::runtime::EventLoop &loop,Submit submit,std::filesystem::path path) {
    auto value=std::shared_ptr<GenerationPreferences>(new GenerationPreferences(std::move(workspace),loop.sender(),std::move(submit),std::move(path)));
    value->start(); return value;
}
void GenerationPreferences::start() {
    const std::weak_ptr<GenerationPreferences> weak=shared_from_this();
    auto registered=sender_.on_signal([weak] {if (auto self=weak.lock()) self->deliver();});
    if (!registered) throw std::runtime_error("Cannot register preferences completion");
    wake_=registered.token;
    bridge_=std::make_shared<Bridge>(); bridge_->path=path_; bridge_->sender=sender_; bridge_->wake=wake_;
    (void)reload();
}
GenerationPreferences::~GenerationPreferences() {
    if (active_ && owner_==std::this_thread::get_id()) try {close();} catch (...) {}
}
void GenerationPreferences::statusAll(const std::string &message) {
    for (const auto category:categories) (void)workspace_->setCategoryStatus(std::string(category),message);
}
ii::core::Result<void> GenerationPreferences::reload() {
    if (owner_!=std::this_thread::get_id()) return ii::core::fail(ErrorCode::wrong_executor,"Preferences require their owner thread");
    if (!active_) return ii::core::fail(ErrorCode::closed,"Generation preferences are closed");
    {
        std::lock_guard lock(bridge_->mutex);
        if (bridge_->running) return ii::core::fail(ErrorCode::busy,"Generation preferences are busy");
        bridge_->running=true; bridge_->ready=false; bridge_->pending.reset(); bridge_->completion.reset();
    }
    for (const auto category:categories) (void)workspace_->setCategoryWriteHandler(std::string(category),{});
    (void)error.set({}); statusAll("Loading generation preferences…");
    auto bridge=bridge_; bool accepted=false;
    try {accepted=submit_([bridge] {
        auto loaded=loadFile(bridge->path); bool notify=false;
        {
            std::lock_guard lock(bridge->mutex);
            bridge->running=false;
            bridge->completion=Bridge::Completion{Bridge::Kind::load,bridge->version,std::move(loaded)};
            notify=bridge->notify;
        }
        if (notify) (void)bridge->sender.signal(bridge->wake,ii::runtime::Clock::now());
    });} catch (...) {accepted=false;}
    if (!accepted) {
        {std::lock_guard lock(bridge_->mutex);bridge_->running=false;}
        (void)error.set("Generation preferences worker is unavailable"); statusAll(error.get());
        return ii::core::fail(ErrorCode::failed,error.get());
    }
    return {};
}
ii::core::Result<void> GenerationPreferences::edit(std::string category,std::string key,std::string input) {
    if (owner_!=std::this_thread::get_id()) return ii::core::fail(ErrorCode::wrong_executor,"Preferences require their owner thread");
    if (!active_) return ii::core::fail(ErrorCode::closed,"Generation preferences are closed");
    const auto it=std::ranges::find(categories,category);
    if (it==categories.end() || (key!="prompt" && key!="negativePrompt" && key!="model") || !validPrompt(input))
        return ii::core::fail(ErrorCode::invalid_argument,"Choose a valid setting of at most 32,000 characters");
    const auto index=std::size_t(it-categories.begin());
    if (key=="model" && index>=2) return ii::core::fail(ErrorCode::invalid_argument,"This category has no default model");
    ModelPreferences previous; std::optional<Bridge::Request> previousPending;
    std::uint64_t previousVersion=0; std::size_t previousSize=0;
    bool launch=false;
    {
        std::lock_guard lock(bridge_->mutex);
        if (!bridge_->ready || bridge_->blocked) return ii::core::fail(ErrorCode::closed,"Reload generation preferences before editing");
        auto &field=key=="model"?(index==0?bridge_->desired.imageModel:bridge_->desired.videoModel):
                    key=="prompt"?bridge_->desired.prompts[index].prompt:bridge_->desired.prompts[index].negativePrompt;
        if (field==input) return {};
        if (bridge_->version==std::numeric_limits<std::uint64_t>::max()) return ii::core::fail(ErrorCode::out_of_range,"Preferences revision is exhausted");
        const auto nextSize=bridge_->serializedSize-quotedBytes(field)+quotedBytes(input);
        if (nextSize>1024*1024) return ii::core::fail(ErrorCode::out_of_range,"Generation preferences exceed the file size limit");
        previous=bridge_->desired; previousPending=bridge_->pending; previousVersion=bridge_->version; previousSize=bridge_->serializedSize;
        field=std::move(input); ++bridge_->version;
        bridge_->serializedSize=nextSize;
        bridge_->pending=Bridge::Request{bridge_->version,bridge_->desired};
        launch=!bridge_->running; if (launch) bridge_->running=true;
    }
    if (launch) {
        auto bridge=bridge_; bool accepted=false;
        try {accepted=submit_([bridge] {
            for (;;) {
                Bridge::Request request;
                std::optional<std::string> expected;
                {
                    std::lock_guard lock(bridge->mutex);
                    if (!bridge->pending) {bridge->running=false;return;}
                    request=std::move(*bridge->pending); bridge->pending.reset(); expected=bridge->expected;
                }
                Loaded outcome;
                try {
                    auto bytes=serializeModelPreferences(request.value);
                    outcome.error=saveFile(bridge->path,expected,bytes);
                    if (outcome.error.empty()) {outcome.value=request.value;outcome.bytes=std::move(bytes);}
                } catch (const std::exception &failure) {outcome.error="Cannot encode generation preferences: "+std::string(failure.what());}
                bool notify=false,finished=false;
                {
                    std::lock_guard lock(bridge->mutex);
                    if (outcome.error.empty()) {bridge->expected=outcome.bytes;bridge->confirmed=outcome.value;}
                    else {bridge->blocked=true;bridge->pending.reset();}
                    finished=bridge->blocked || !bridge->pending;
                    if (finished) bridge->running=false;
                    bridge->completion=Bridge::Completion{Bridge::Kind::save,request.version,std::move(outcome)};
                    notify=bridge->notify;
                }
                if (notify) (void)bridge->sender.signal(bridge->wake,ii::runtime::Clock::now());
                if (finished) return;
            }
        });} catch (...) {accepted=false;}
        if (!accepted) {
            std::lock_guard lock(bridge_->mutex);
            bridge_->desired=std::move(previous); bridge_->pending=std::move(previousPending);
            bridge_->version=previousVersion; bridge_->serializedSize=previousSize; bridge_->running=false;
            return ii::core::fail(ErrorCode::failed,"Generation preferences worker is unavailable");
        }
    }
    (void)saving.set(true); (void)error.set({}); statusAll("Saving generation preferences…"); return {};
}
void GenerationPreferences::deliver() {
    if (!active_) return;
    std::optional<Bridge::Completion> completion; bool running=false; std::uint64_t version=0;
    {
        std::lock_guard lock(bridge_->mutex);
        completion=std::move(bridge_->completion); bridge_->completion.reset();
        running=bridge_->running; version=bridge_->version;
    }
    if (!completion) return;
    if (!completion->loaded.error.empty()) {
        (void)saving.set(false); (void)error.set(completion->loaded.error); statusAll(error.get()); return;
    }
    if (completion->kind==Bridge::Kind::load) {
        const auto values=completion->loaded.value;
        {
            std::lock_guard lock(bridge_->mutex);
            bridge_->desired=values; bridge_->confirmed=values; bridge_->expected=std::move(completion->loaded.bytes);
            bridge_->serializedSize=serializeModelPreferences(values).size();
            bridge_->ready=true; bridge_->blocked=false;
        }
        const std::weak_ptr<GenerationPreferences> weak=shared_from_this();
        for (std::size_t index=0;index<categories.size();++index) {
            const auto category=std::string(categories[index]);
            std::vector<FieldSpec> fields;
            if (index<2) fields.push_back({"model","Default model",index==0?values.imageModel:values.videoModel,true,true});
            fields.push_back({"prompt","Default prompt",values.prompts[index].prompt,true});
            fields.push_back({"negativePrompt","Default negative prompt",values.prompts[index].negativePrompt,true});
            (void)workspace_->setSnapshot(category,std::move(fields));
            (void)workspace_->setCategoryWriteHandler(category,[weak](auto id,auto key,auto value) -> ii::core::Result<void> {
                if (auto self=weak.lock()) return self->edit(std::move(id),std::move(key),std::move(value));
                return ii::core::fail(ErrorCode::closed,"Generation preferences are closed");
            });
        }
        (void)error.set({}); statusAll({});
        if (auto handler=defaults_) handler(values);
    } else if (!running && completion->version==version) {
        const auto current=[&] {
            std::lock_guard lock(bridge_->mutex);
            return active_ && !bridge_->running && !bridge_->blocked && bridge_->version==version;
        };
        (void)saving.set(false);
        // Observers may enqueue another edit while the previous save is being
        // published. Do not clear its status or publish stale defaults.
        if (!current()) return;
        (void)error.set({});
        if (!current()) return;
        statusAll({});
        if (!current()) {
            if (active_) {(void)saving.set(true);statusAll("Saving generation preferences…");}
            return;
        }
        if (auto handler=defaults_) handler(completion->loaded.value);
    }
}
void GenerationPreferences::setDefaultsHandler(std::function<void(const ModelPreferences &)> handler) {
    if (owner_!=std::this_thread::get_id() || !active_) return;
    defaults_=std::move(handler);
    std::optional<ModelPreferences> values;
    {std::lock_guard lock(bridge_->mutex);if (bridge_->ready) values=bridge_->confirmed;}
    if (values) if (auto callback=defaults_) callback(*values);
}
void GenerationPreferences::close() {
    if (owner_!=std::this_thread::get_id()) throw std::logic_error("Preferences require their owner thread");
    if (!active_) return;
    active_=false;
    for (const auto category:categories) (void)workspace_->setCategoryWriteHandler(std::string(category),{});
    {std::lock_guard lock(bridge_->mutex);bridge_->notify=false;}
    (void)sender_.cancel(wake_);
    submit_={}; defaults_={};
}
}
