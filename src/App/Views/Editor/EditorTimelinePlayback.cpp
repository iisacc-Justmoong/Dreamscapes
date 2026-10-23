#include "EditorTimelineModel.h"
#include "EditorAudioPlayback.h"
#include "EditorMedia.h"
#include <algorithm>
#include <cmath>
#include <utility>

void EditorTimelineModel::bindPlayback() {
    auto *media=m_canvas?qobject_cast<EditorMedia *>(m_canvas->media()):nullptr;
    m_playback=m_overridePlayback?m_overridePlayback.data():media?media->audioPlayback():nullptr;
    if(!m_playback)return;
    connect(m_playback,&EditorAudioPlayback::started,this,&EditorTimelineModel::beginPlaybackClock);
    connect(m_playback,&EditorAudioPlayback::failed,this,[this](qulonglong id,const QString &message) {
        if(id!=m_playbackId||!m_playing)return;
        m_playbackId=0;rejectPlayback(message);
    });
    connect(m_playback,&EditorAudioPlayback::cancelled,this,[this](qulonglong id) {
        if(id==m_playbackId&&m_playing){m_playbackId=0;endPlayback(false);}
    });
    connect(m_playback,&EditorAudioPlayback::finished,this,[this](qulonglong id) {
        if(id!=m_playbackId||!m_playing)return;
        const auto owner=QPointer<EditorTimelineModel>(this);
        m_advancingPlayback=true;if(m_canvas)m_canvas->setFrame(quint32(totalFrames()-1));
        if(!owner)return;m_advancingPlayback=false;m_playbackId=0;endPlayback(false);
    });
    connect(m_playback,&QObject::destroyed,this,[this] {
        m_playback=nullptr;m_playbackId=0;endPlayback(false);
    });
}
void EditorTimelineModel::endPlayback(bool stopAudio,bool notify) {
    const auto id=std::exchange(m_playbackId,0);const bool active=m_playing;
    m_playing=false;m_playbackClockStarted=false;++m_playbackSerial;m_playbackTimer.stop();m_playbackClock.invalidate();
    const auto owner=QPointer<EditorTimelineModel>(this);const auto playback=m_playback;
    if(stopAudio&&playback&&id)playback->stopRequest(id);
    if(owner&&active&&notify)emit playbackChanged();
}
void EditorTimelineModel::rejectPlayback(const QString &error) {
    const auto message=error.isEmpty()?tr("Could not start timeline playback."):error;
    const auto owner=QPointer<EditorTimelineModel>(this);endPlayback();if(!owner)return;
    m_error=message;emit changed();if(owner)emit playbackFailed(message);
}
void EditorTimelineModel::setPlaying(bool value) {
    if(value==m_playing)return;
    if(!value){endPlayback();return;}
    const auto scrubOwner=QPointer<EditorTimelineModel>(this);
    if(m_scrubbing)finishScrub(m_scrubPending<0?playhead():m_scrubPending);
    if(!scrubOwner)return;
    if(!document()){rejectPlayback(tr("Open a canvas before starting playback."));return;}
    const bool clearError=!m_error.isEmpty();
    m_playing=true;m_playbackClockStarted=false;m_audioClock=hasAudio();m_playbackFirstFrame=playhead();m_error.clear();
    const auto serial=++m_playbackSerial;const auto owner=QPointer<EditorTimelineModel>(this);
    if(clearError)emit changed();if(!owner||serial!=m_playbackSerial||!m_playing)return;
    emit playbackChanged();if(!owner||serial!=m_playbackSerial||!m_playing)return;
    if(!m_audioClock) {
        // Starting the timeline replaces an asset preview on the same output.
        const auto playback=m_playback;
        if(playback)playback->stop();
        if(owner&&serial==m_playbackSerial&&m_playing)beginPlaybackClock(0);
        return;
    }
    const auto playback=m_playback;
    if(!playback){rejectPlayback(tr("The timeline audio output is unavailable."));return;}
    const auto snapshot=m_canvas->editingFrameSnapshot();
    const auto id=playback->play({snapshot.document,snapshot.frame,{}});
    if(!owner||serial!=m_playbackSerial||!m_playing){if(playback)playback->stopRequest(id);return;}
    m_playbackId=id;
    if(!id){rejectPlayback(playback->lastRejection());return;}
    if(playback->requestId()!=id){endPlayback(false);return;}
    // A reentrant observer can dispatch ready events before play() returns.
    if(playback->playing())beginPlaybackClock(id);
}
void EditorTimelineModel::beginPlaybackClock(qulonglong id) {
    if(!m_playing||m_playbackClockStarted||(m_audioClock&&id!=m_playbackId))return;
    m_playbackClockStarted=true;m_playbackClock.start();
    m_playbackTimer.start(std::clamp(int(std::floor(1000/std::max(1.0,fps()))),1,250));
    emit playbackChanged();
}
void EditorTimelineModel::updatePlaybackPosition() {
    if(!m_playing||!m_playbackClockStarted||!m_canvas)return;
    const auto usecs=m_audioClock&&m_playback?m_playback->processedUsecs():m_playbackClock.nsecsElapsed()/1000;
    const long double frame=m_playbackFirstFrame+std::floor(static_cast<long double>(std::max<qint64>(0,usecs))*fps()/1000000);
    const bool complete=frame>=totalFrames();
    const auto target=quint32(std::clamp<long double>(frame,0,totalFrames()-1));
    const auto owner=QPointer<EditorTimelineModel>(this);m_advancingPlayback=true;
    m_canvas->setFrame(target);if(!owner)return;m_advancingPlayback=false;
    if(complete)endPlayback();
}
