#include "NativeAudioPanel.h"
#include <Audio/AudioCodec.h>
#include <LVUI>
#include <iiFileProvider>
#include <iiSDKCore>
#include <algorithm>
#include <charconv>
#include <cctype>
#include <cmath>
#include <limits>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <utility>

namespace dreamscapes::editor {
namespace {
using ii::core::ErrorCode;
std::string number(double value) {
    char buffer[64];const auto result=std::to_chars(buffer,buffer+sizeof(buffer),value,std::chars_format::general,6);
    return result.ec==std::errc{}?std::string(buffer,result.ptr):std::string{};
}
std::string timeNumber(double value) {
    char buffer[64];const auto result=std::to_chars(buffer,buffer+sizeof(buffer),value,std::chars_format::general,12);
    return result.ec==std::errc{}?std::string(buffer,result.ptr):std::string{};
}
std::string_view trimmed(std::string_view value) {
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front()))) value.remove_prefix(1);
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back()))) value.remove_suffix(1);
    return value;
}
std::optional<double> timeValue(std::string_view value) {
    value=trimmed(value);if (!value.empty() && (value.back()=='s' || value.back()=='S')) value=trimmed(value.substr(0,value.size()-1));
    if (value.empty()) return {};
    double seconds=0;unsigned parts=0;
    while (!value.empty()) {
        const auto colon=value.find(':');const auto part=trimmed(value.substr(0,colon));double number=0;
        const auto parsed=std::from_chars(part.data(),part.data()+part.size(),number);
        if (part.empty() || parsed.ec!=std::errc{} || parsed.ptr!=part.data()+part.size() ||
            !std::isfinite(number) || number<0 || (parts && number>=60) || ++parts>3) return {};
        seconds=seconds*60+number;if (!std::isfinite(seconds)) return {};
        if (colon==std::string_view::npos) break;
        value.remove_prefix(colon+1);if (value.empty()) return {};
    }
    return seconds;
}
std::string displayName(std::string value,std::string_view fallback) {
    const bool control=std::ranges::any_of(value,[](unsigned char c){return c<0x20 || c==0x7f;});
    return value.empty() || value.size()>4096 || control || !ii::core::valid_utf8(value)?std::string(fallback):value;
}
bool validPath(const std::filesystem::path &path) {
    if (!path.is_absolute() || path.filename().empty() || path.lexically_normal()!=path) return false;
    auto converted=ii::core::path_to_utf8(path.extension());if (!converted) return false;
    auto suffix=std::move(*converted);
    std::ranges::transform(suffix,suffix.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});
    return suffix==".wav";
}
}
struct AudioPanelModel::Bridge final {
    std::mutex mutex;
    std::optional<ii::core::Result<PreparedAudioEdit>> completion;
    ii::runtime::EventLoop::Sender sender;
    ii::runtime::TaskToken wake;
    bool closed=false;
};
AudioPanelModel::AudioPanelModel(std::shared_ptr<native::DocumentSession> document,ii::runtime::EventLoop::Sender sender,
    std::shared_ptr<iiSharedCanvas::FrameRenderExecutor> executor):document_(std::move(document)),sender_(std::move(sender)),executor_(std::move(executor)) {
    if (!document_ || !executor_) throw std::invalid_argument("Audio panel requires a document and worker");
    const auto defaultWidth=lvui::SliderConfiguration{}.width;
    (void)width.set(defaultWidth);(void)contentWidth.set(defaultWidth);(void)fieldWidth.set(defaultWidth/2);
}
std::shared_ptr<AudioPanelModel> AudioPanelModel::create(std::shared_ptr<native::DocumentSession> document,
    ii::runtime::EventLoop &loop,std::shared_ptr<iiSharedCanvas::FrameRenderExecutor> executor) {
    auto value=std::shared_ptr<AudioPanelModel>(new AudioPanelModel(std::move(document),loop.sender(),std::move(executor)));
    value->start();return value;
}
void AudioPanelModel::start() {
    const std::weak_ptr<AudioPanelModel> weak=shared_from_this();
    auto admitted=sender_.on_signal([weak]{if (auto self=weak.lock()) self->deliver();});
    if (!admitted) throw std::runtime_error("Cannot register audio completion");
    wake_=admitted.token;bridge_=std::make_shared<Bridge>();bridge_->sender=sender_;bridge_->wake=wake_;
    generationObserver_=document_->documentGeneration.observe([weak](auto,std::uint64_t){if (auto self=weak.lock()) {
        if (self->cancellation_) self->cancellation_->store(true,std::memory_order_relaxed);
        self->selected_.clear();self->draftDirty_=false;self->conflict_=false;self->project(true);
    }});
    revisionObserver_=document_->revision.observe([weak](auto,std::uint64_t){if (auto self=weak.lock()) {
        if (self->cancellation_) self->cancellation_->store(true,std::memory_order_relaxed);
        self->project();
    }});
    project(true);
}
AudioPanelModel::~AudioPanelModel() {if (active_ && owner_==std::this_thread::get_id()) try {close();} catch (...) {}}
ii::core::Result<void> AudioPanelModel::writable() const {
    if (owner_!=std::this_thread::get_id()) return ii::core::fail(ErrorCode::wrong_executor,"Audio panel requires its owner thread");
    if (!active_) return ii::core::fail(ErrorCode::closed,"Audio panel is closed");
    return {};
}
void AudioPanelModel::setVisible(bool value) {if (!writable()) return;visible_=value;project();}
void AudioPanelModel::showImport() {if (!writable()) return;(void)importMode.set(true);(void)trimMode.set(false);(void)mixMode.set(false);}
void AudioPanelModel::showTrim() {if (!writable()) return;(void)importMode.set(false);(void)trimMode.set(true);(void)mixMode.set(false);}
void AudioPanelModel::showMix() {if (!writable()) return;(void)importMode.set(false);(void)trimMode.set(false);(void)mixMode.set(true);}
void AudioPanelModel::sizeChanged(double w,double h,double inset,double maximumField) {
    if (!std::isfinite(w)||!std::isfinite(h)||!std::isfinite(inset)||!std::isfinite(maximumField)||
        w<0||h<0||w>8192||h>8192||inset<0||maximumField<=0) return;
    (void)width.set(std::max(1.0,w));(void)height.set(std::max(1.0,h));
    (void)contentWidth.set(std::max(lvui::SliderConfiguration{}.range_inset*2,w-inset));
    (void)fieldWidth.set(std::max(1.0,std::min(maximumField,contentWidth.get()/2)));
}
void AudioPanelModel::setPickHandler(PickHandler value) {if (!writable()) return;pick_=std::move(value);updateAvailability();}
void AudioPanelModel::updateAvailability() {
    const bool ready=active_ && visible_ && document_->document() && !busy.get();
    (void)canCreate.set(ready);(void)editable.set(ready && !selected_.empty());
    (void)canChoose.set(ready && !selected_.empty() && bool(pick_));
    (void)clipEditable.set(ready && !selected_.empty() && !selectedClip_.empty() && bool(baselineClip_));
    (void)canApply.set(ready && !selected_.empty() && draftValid_ && draftDirty_ && !conflict_);
    (void)canTrim.set(ready && !selectedClip_.empty() && trimDirty_ && !trimConflict_ && bool(trimFrames()));
}
std::optional<std::pair<std::int64_t,std::int64_t>> AudioPanelModel::trimFrames() const {
    const auto *document=document_->document();if (!document || !baselineClip_) return {};
    const auto fps=document->timeline.frameRate;if (!fps.numerator || !fps.denominator) return {};
    const auto text=std::string_view(trimRange.get());auto separator=text.find("—");std::size_t length=3;
    if (separator==std::string_view::npos) separator=text.find("–");
    if (separator==std::string_view::npos) {separator=text.find('-');length=1;}
    if (separator==std::string_view::npos) return {};
    const auto first=timeValue(text.substr(0,separator)),end=timeValue(text.substr(separator+length));
    if (!first || !end) return {};
    const auto toFrames=[&](double seconds)->std::optional<std::int64_t> {
        const long double frames=static_cast<long double>(seconds)*fps.numerator/fps.denominator;
        if (frames<0 || frames>std::numeric_limits<iiSharedCanvas::FrameIndex>::max()) return {};
        return static_cast<std::int64_t>(std::llround(frames));
    };
    const auto start=toFrames(*first),stop=toFrames(*end);if (!start || !stop) return {};
    const auto oldStart=std::int64_t(baselineClip_->startFrame),oldEnd=oldStart+baselineClip_->durationFrames;
    if (*start<oldStart || *stop>oldEnd || *start>=*stop || (*start==oldStart && *stop==oldEnd)) return {};
    return std::pair{*start,*stop};
}
void AudioPanelModel::projectTrim(const iiSharedCanvas::Document *document,bool force) {
    const auto *track=document && !selected_.empty()?iiSharedCanvas::findAudioTrack(*document,selected_):nullptr;
    std::vector<std::string> ids{std::string{}},names{"Select clip"};
    if (track) for (const auto &clip:track->clips) {ids.push_back(clip.id);names.push_back(displayName(clip.name.empty()?clip.id:clip.name,"Audio clip"));}
    if (std::ranges::find(ids,selectedClip_)==ids.end()) {selectedClip_.clear();force=true;}
    clipIds_=std::move(ids);(void)clipNames.set(std::move(names));
    const auto selected=std::ranges::find(clipIds_,selectedClip_);
    (void)selectedClip.set(selected==clipIds_.end()?0:static_cast<int>(selected-clipIds_.begin()));
    const auto *clip=track && !selectedClip_.empty()?iiSharedCanvas::findAudioClip(*track,selectedClip_):nullptr;
    if (!clip) {baselineClip_.reset();trimDirty_=false;trimConflict_=false;(void)trimRange.set({});return;}
    if (force || !trimDirty_) {
        baselineClip_=*clip;trimDirty_=false;trimConflict_=false;
        const auto fps=document->timeline.frameRate;
        if (fps.numerator && fps.denominator) {
            const auto scale=double(fps.denominator)/fps.numerator;
            (void)trimRange.set(timeNumber(clip->startFrame*scale)+" – "+timeNumber((std::uint64_t(clip->startFrame)+clip->durationFrames)*scale));
        } else (void)trimRange.set({});
    } else if (!baselineClip_ || *baselineClip_!=*clip) {trimConflict_=true;(void)status.set("The selected audio clip changed; reset the trim draft");}
}
void AudioPanelModel::project(bool force) {
    if (projecting_) {projectionPending_=true;forcePending_|=force;return;}
    projecting_=true;
    try {
    do {
    projectionPending_=false;
    const auto snapshot=document_->snapshot();const auto *document=snapshot.get();
    const auto generation=document_->documentGeneration.get(),revision=document_->revision.get();
    std::vector<std::string> ids{std::string{}},names{"Select track"};
    if (document) for (const auto &track:document->audioTracks) {
        ids.push_back(track.id);names.push_back(displayName(track.name.empty()?track.id:track.name,"Audio"));
    }
    const auto position=std::ranges::find(ids,selected_);
    if (position==ids.end()) {selected_.clear();force=true;}
    trackIds_=std::move(ids);(void)trackNames.set(std::move(names));
    if (projectionPending_ || generation!=document_->documentGeneration.get() || revision!=document_->revision.get()) {
        projectionPending_=true;force|=std::exchange(forcePending_,false);continue;
    }
    const auto selected=std::ranges::find(trackIds_,selected_);
    (void)selectedTrack.set(selected==trackIds_.end()?0:static_cast<int>(selected-trackIds_.begin()));
    const auto *track=document && !selected_.empty()?iiSharedCanvas::findAudioTrack(*document,selected_):nullptr;
    const bool forceTrack=std::exchange(trackForcePending_,false);
    if (!track) {
        draftDirty_=false;draftValid_=true;conflict_=false;baselineGain_=draftGain_=0;baselineMuted_=false;
        (void)muted.set(false);(void)gainValue.set(0);(void)gainText.set({});(void)summary.set(document?"Select an audio track":"Open a canvas first");
    } else {
        const bool externallyChanged=track->gainDb!=baselineGain_ || track->muted!=baselineMuted_;
        if (force || forceTrack || !draftDirty_) {
            baselineGain_=draftGain_=track->gainDb;baselineMuted_=track->muted;
            draftDirty_=false;draftValid_=true;conflict_=false;(void)muted.set(track->muted);
            (void)gainValue.set(track->gainDb);(void)gainText.set(number(track->gainDb));
        } else if (externallyChanged) {conflict_=true;(void)status.set("The selected audio track changed; reset the draft");}
        std::string info=std::to_string(track->clips.size())+(track->clips.size()==1?" clip":" clips");
        if (!track->clips.empty()) {
            const auto &clip=track->clips.front();
            info+=" · "+displayName(clip.name.empty()?clip.id:clip.name,"Audio clip");
            if (const auto *asset=iiSharedCanvas::findAudioAsset(*document,clip.assetId))
                info+=" · "+std::to_string(asset->sampleRate)+" Hz · "+(asset->channelCount==1?"mono":"stereo");
        }
        (void)summary.set(std::move(info));
    }
    const bool forceTrim=std::exchange(trimForcePending_,false);
    projectTrim(document,force || forceTrim);updateAvailability();
    if (generation!=document_->documentGeneration.get() || revision!=document_->revision.get()) projectionPending_=true;
    force=std::exchange(forcePending_,false);
    } while (projectionPending_);
    projecting_=false;
    } catch (...) {projecting_=false;throw;}
}
ii::core::Result<void> AudioPanelModel::chooseFile() {
    if (auto valid=writable();!valid) return valid;
    if (!canChoose.get()) return ii::core::fail(ErrorCode::unsupported,"Audio file picker is unavailable");
    auto handler=pick_;auto result=handler();if (!result) (void)status.set(result.error().message);return result;
}
ii::core::Result<void> AudioPanelModel::selectTrack(int index) {
    if (auto valid=writable();!valid) return valid;
    if (busy.get()) return ii::core::fail(ErrorCode::busy,"An audio operation is in progress");
    if (index<0 || static_cast<std::size_t>(index)>=trackIds_.size()) return ii::core::fail(ErrorCode::out_of_range,"Unknown audio track");
    selected_=trackIds_[static_cast<std::size_t>(index)];selectedClip_.clear();(void)status.set({});project(true);return {};
}
ii::core::Result<void> AudioPanelModel::selectClip(int index) {
    if (auto valid=writable();!valid) return valid;
    if (busy.get()) return ii::core::fail(ErrorCode::busy,"An audio operation is in progress");
    if (index<0 || static_cast<std::size_t>(index)>=clipIds_.size()) return ii::core::fail(ErrorCode::out_of_range,"Unknown audio clip");
    selectedClip_=clipIds_[static_cast<std::size_t>(index)];(void)status.set({});trimForcePending_=true;project();return {};
}
ii::core::Result<void> AudioPanelModel::run(Job kind,Operation operation,bool requireVisible) {
    if (auto valid=writable();!valid) return valid;
    if ((requireVisible && !visible_) || !document_->document() || busy.get() || !operation)
        return ii::core::fail(ErrorCode::busy,"Audio operation is unavailable");
    std::shared_ptr<std::atomic_bool> cancellation;
    try {cancellation=std::make_shared<std::atomic_bool>(false);}
    catch (...) {return ii::core::fail(ErrorCode::failed,"Cannot schedule audio operation",std::current_exception());}
    cancellation_=cancellation;job_=kind;jobSelection_=selected_;
    auto bridge=bridge_;(void)busy.set(true);updateAvailability();(void)status.set("Preparing audio…");
    if (!active_ || !executor_ || cancellation->load(std::memory_order_relaxed)) {
        cancellation_.reset();(void)busy.set(false);updateAvailability();
        return ii::core::fail(ErrorCode::cancelled,"The canvas changed while preparing the audio edit");
    }
    try {
        executor_->post([operation=std::move(operation),cancellation,bridge] {
            ii::core::Result<PreparedAudioEdit> result=ii::core::fail(ErrorCode::failed,"Cannot prepare audio");
            try {
                if (cancellation->load(std::memory_order_relaxed)) result=ii::core::fail(ErrorCode::cancelled,"The canvas changed while preparing the audio edit");
                else result=operation(cancellation);
                if (cancellation->load(std::memory_order_relaxed)) result=ii::core::fail(ErrorCode::cancelled,"The canvas changed while preparing the audio edit");
            } catch (...) {result=ii::core::fail(ErrorCode::failed,"Cannot prepare audio",std::current_exception());}
            {std::lock_guard lock(bridge->mutex);if (bridge->closed) return;bridge->completion.emplace(std::move(result));}
            (void)bridge->sender.signal(bridge->wake,ii::runtime::Clock::now());
        });
    } catch (...) {
        cancellation_.reset();(void)busy.set(false);updateAvailability();(void)status.set("Audio worker is unavailable");
        return ii::core::fail(ErrorCode::failed,status.get(),std::current_exception());
    }
    return {};
}
ii::core::Result<void> AudioPanelModel::createTrack() {
    if (auto valid=writable();!valid) return valid;
    if (!canCreate.get()) return ii::core::fail(ErrorCode::closed,"Open a canvas first");
    auto snapshot=document_->snapshot();const auto generation=document_->documentGeneration.get(),revision=document_->revision.get();
    return run(Job::create,[snapshot=std::move(snapshot),generation,revision](auto cancelled) {
        return prepareAudioTrackCreate(snapshot,generation,revision,[cancelled]{return cancelled->load(std::memory_order_relaxed);});
    });
}
ii::core::Result<void> AudioPanelModel::importFile(std::filesystem::path path) {
    if (auto valid=writable();!valid) return valid;
    if (!editable.get()) return ii::core::fail(ErrorCode::closed,"Select an audio track first");
    if (!validPath(path)) return ii::core::fail(ErrorCode::invalid_argument,"Choose an absolute PCM16 WAV file path");
    auto name=ii::core::path_to_utf8(path.filename());
    if (!name || name->empty()) return ii::core::fail(ErrorCode::invalid_argument,"Audio filename is invalid");
    auto snapshot=document_->snapshot();const auto generation=document_->documentGeneration.get(),revision=document_->revision.get();
    const auto frame=document_->frame.get();const auto track=selected_;
    return run(Job::import,[path=std::move(path),name=std::move(*name),snapshot=std::move(snapshot),generation,revision,frame,track](auto cancelled) {
        try {
            const auto identity=iiFileProvider::ObjectSource::inspect(path);
            const iiSharedCanvas::MediaLimits limits;
            iiSharedCanvas::AudioImportResult decoded;
            {
                const auto bytes=iiFileProvider::native::File::read(path,limits.maxInputBytes);
                if (cancelled->load(std::memory_order_relaxed)) return ii::core::Result<PreparedAudioEdit>(ii::core::fail(ErrorCode::cancelled,"The canvas changed while preparing the audio edit"));
                const auto input=std::span(reinterpret_cast<const std::uint8_t *>(bytes.data()),bytes.size());
                decoded=iiSharedCanvas::decodeAudioWav(input);
            }
            if (!decoded.ok()) return ii::core::Result<PreparedAudioEdit>(ii::core::fail(ErrorCode::invalid_argument,decoded.result.message));
            if (iiFileProvider::ObjectSource::inspect(path)!=identity)
                return ii::core::Result<PreparedAudioEdit>(ii::core::fail(ErrorCode::busy,"The WAV file changed while it was being read"));
            if (cancelled->load(std::memory_order_relaxed)) return ii::core::Result<PreparedAudioEdit>(ii::core::fail(ErrorCode::cancelled,"The canvas changed while preparing the audio edit"));
            return prepareAudioImport(snapshot,generation,revision,track,frame,std::move(decoded.asset),name,
                                      [cancelled]{return cancelled->load(std::memory_order_relaxed);});
        } catch (...) {return ii::core::Result<PreparedAudioEdit>(ii::core::fail(ErrorCode::failed,"Cannot read the WAV file",std::current_exception()));}
    });
}
ii::core::Result<void> AudioPanelModel::setMutedDraft(bool value) {
    if (auto valid=writable();!valid) return valid;
    if (!editable.get()) return ii::core::fail(ErrorCode::closed,"Select an audio track first");
    (void)muted.set(value);draftDirty_=!draftValid_ || value!=baselineMuted_ || draftGain_!=baselineGain_;updateAvailability();return {};
}
ii::core::Result<void> AudioPanelModel::editGain(std::string value) {
    if (auto valid=writable();!valid) return valid;
    if (!editable.get()) return ii::core::fail(ErrorCode::closed,"Select an audio track first");
    (void)gainText.set(value);double parsed=0;const auto result=std::from_chars(value.data(),value.data()+value.size(),parsed);
    draftValid_=result.ec==std::errc{} && result.ptr==value.data()+value.size() && std::isfinite(parsed) && parsed>=minimumGain && parsed<=maximumGain;
    if (draftValid_) {draftGain_=parsed;(void)gainValue.set(parsed);}
    draftDirty_=!draftValid_ || muted.get()!=baselineMuted_ || draftGain_!=baselineGain_;
    updateAvailability();return {};
}
ii::core::Result<void> AudioPanelModel::setGainDraft(double value) {
    if (auto valid=writable();!valid) return valid;
    if (!editable.get()) return ii::core::fail(ErrorCode::closed,"Select an audio track first");
    if (!std::isfinite(value) || value<minimumGain || value>maximumGain)
        return ii::core::fail(ErrorCode::out_of_range,"Audio gain is outside its supported range");
    draftGain_=value;draftValid_=true;draftDirty_=muted.get()!=baselineMuted_ || draftGain_!=baselineGain_;
    (void)gainValue.set(value);(void)gainText.set(number(value));updateAvailability();return {};
}
ii::core::Result<void> AudioPanelModel::applyTrack() {
    if (auto valid=writable();!valid) return valid;
    if (!canApply.get()) return ii::core::fail(ErrorCode::invalid_argument,"Enter valid track settings");
    auto snapshot=document_->snapshot();const auto generation=document_->documentGeneration.get(),revision=document_->revision.get();
    const auto id=selected_;const auto nextMuted=muted.get();const auto nextGain=draftGain_;
    return run(Job::patch,[snapshot=std::move(snapshot),generation,revision,id,nextMuted,nextGain](auto cancelled) {
        return prepareAudioTrackPatch(snapshot,generation,revision,id,nextMuted,nextGain,
                                      [cancelled]{return cancelled->load(std::memory_order_relaxed);});
    });
}
ii::core::Result<void> AudioPanelModel::resetTrack() {
    if (auto valid=writable();!valid) return valid;
    if (busy.get()) return ii::core::fail(ErrorCode::busy,"An audio operation is in progress");
    (void)status.set({});trackForcePending_=true;project();return {};
}
ii::core::Result<void> AudioPanelModel::editTrim(std::string value) {
    if (auto valid=writable();!valid) return valid;
    if (!clipEditable.get()) return ii::core::fail(ErrorCode::closed,"Select an audio clip first");
    if (value.size()>128) return ii::core::fail(ErrorCode::out_of_range,"Audio trim range is too long");
    (void)trimRange.set(std::move(value));trimDirty_=true;updateAvailability();return {};
}
ii::core::Result<void> AudioPanelModel::applyTrim() {
    if (auto valid=writable();!valid) return valid;
    const auto frames=trimFrames();if (!canTrim.get() || !frames) return ii::core::fail(ErrorCode::invalid_argument,"Enter a range inside the selected clip");
    auto snapshot=document_->snapshot();const auto generation=document_->documentGeneration.get(),revision=document_->revision.get();
    const auto track=selected_,clip=selectedClip_;const auto [first,end]=*frames;
    return run(Job::trim,[snapshot=std::move(snapshot),generation,revision,track,clip,first,end](auto cancelled) {
        return prepareAudioClipTrim(snapshot,generation,revision,track,clip,first,end,
                                    [cancelled]{return cancelled->load(std::memory_order_relaxed);});
    });
}
ii::core::Result<void> AudioPanelModel::resetTrim() {
    if (auto valid=writable();!valid) return valid;
    if (busy.get()) return ii::core::fail(ErrorCode::busy,"An audio operation is in progress");
    (void)status.set({});trimForcePending_=true;project();return {};
}
ii::core::Result<void> AudioPanelModel::editTimelineClip(std::string track,std::string clip,
    ii::gui::EditorTimelineGesture kind,std::int64_t first,std::int64_t end) {
    if (auto valid=writable();!valid) return valid;
    using ii::gui::EditorTimelineGesture;
    if (kind!=EditorTimelineGesture::move_clip && kind!=EditorTimelineGesture::trim_start && kind!=EditorTimelineGesture::trim_end)
        return ii::core::fail(ErrorCode::invalid_argument,"Unknown audio timeline operation");
    auto snapshot=document_->snapshot();const auto generation=document_->documentGeneration.get(),revision=document_->revision.get();
    return run(Job::timeline,[snapshot=std::move(snapshot),generation,revision,track=std::move(track),clip=std::move(clip),kind,first,end](auto cancelled) {
        const auto stopped=[cancelled]{return cancelled->load(std::memory_order_relaxed);};
        if (kind==EditorTimelineGesture::move_clip)
            return prepareAudioClipMove(snapshot,generation,revision,track,clip,first,stopped);
        return prepareAudioClipTrim(snapshot,generation,revision,track,clip,first,end,stopped);
    },false);
}
void AudioPanelModel::reportError(std::string message) {if (writable()) (void)status.set(std::move(message));}
void AudioPanelModel::deliver() {
    if (!active_) return;
    std::optional<ii::core::Result<PreparedAudioEdit>> completion;
    {std::lock_guard lock(bridge_->mutex);completion=std::move(bridge_->completion);bridge_->completion.reset();}
    if (!completion) return;
    cancellation_.reset();
    if (*completion) {
        auto created=(**completion).createdTrack;
        if (job_==Job::patch) {draftDirty_=false;conflict_=false;}
        if (job_==Job::trim) {trimDirty_=false;trimConflict_=false;}
        auto result=document_->commitAudioEdit(std::move(**completion));
        if (result) {
            if (job_==Job::create && selected_==jobSelection_) {selected_=std::move(created);project(true);}
            (void)status.set(job_==Job::import?"Audio imported":job_==Job::create?"Track created":job_==Job::trim?"Clip trimmed":job_==Job::timeline?"Audio clip updated":"Track updated");
        } else (void)status.set(result.error().message);
    } else (void)status.set(completion->error().message);
    (void)busy.set(false);project();
}
void AudioPanelModel::close() {
    if (owner_!=std::this_thread::get_id()) throw std::logic_error("Audio panel requires its owner thread");
    if (!active_) return;active_=false;visible_=false;generationObserver_={};revisionObserver_={};pick_={};
    if (cancellation_) cancellation_->store(true,std::memory_order_relaxed);cancellation_.reset();
    if (bridge_) {std::lock_guard lock(bridge_->mutex);bridge_->closed=true;bridge_->completion.reset();}
    (void)sender_.cancel(wake_);executor_.reset();(void)busy.set(false);updateAvailability();
}
}
