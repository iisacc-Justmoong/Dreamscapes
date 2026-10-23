#include "EditorTimelineModel.h"
#include <algorithm>
#include <utility>

void EditorTimelineModel::navigateFrame(int frame,bool fromScrub) {
    if(!document())return;
    const auto owner=QPointer<EditorTimelineModel>(this);
    const auto previousRefresh=m_refreshing,previousScrub=m_advancingScrub;
    m_refreshing=true;m_advancingScrub=fromScrub;
    m_canvas->setFrame(quint32(std::clamp(frame,0,totalFrames()-1)));
    if(owner){m_refreshing=previousRefresh;m_advancingScrub=previousScrub;}
}
bool EditorTimelineModel::discardScrub() {
    m_scrubTimer.stop();m_scrubPending=-1;++m_scrubSerial;
    return std::exchange(m_scrubbing,false);
}
void EditorTimelineModel::seek(int frame) {
    const auto owner=QPointer<EditorTimelineModel>(this);endPlayback();if(!owner)return;
    const bool stopped=discardScrub();const auto serial=m_scrubSerial;
    navigateFrame(frame);
    if(owner&&stopped&&serial==m_scrubSerial)emit scrubbingChanged();
}
bool EditorTimelineModel::beginScrub(int frame) {
    if(!document())return false;
    const auto owner=QPointer<EditorTimelineModel>(this);endPlayback();if(!owner)return false;
    cancelDrag();if(!owner||!document())return false;
    const bool previous=discardScrub();m_scrubbing=true;m_scrubInitial=playhead();
    m_scrubPending=std::clamp(frame,0,totalFrames()-1);const auto serial=m_scrubSerial;
    flushScrub();
    if(!owner||serial!=m_scrubSerial||!m_scrubbing)return false;
    if(!previous)emit scrubbingChanged();
    return owner&&serial==m_scrubSerial&&m_scrubbing;
}
void EditorTimelineModel::updateScrub(int frame) {
    if(!m_scrubbing||!document())return;
    frame=std::clamp(frame,0,totalFrames()-1);
    if(m_scrubPending==frame||(m_scrubPending<0&&playhead()==frame))return;
    m_scrubPending=frame;
    // Throttle, not debounce: continuous input still advances every interval.
    // A burst retains only its newest frame instead of queuing stale navigation.
    if(!m_scrubTimer.isActive())m_scrubTimer.start();
}
void EditorTimelineModel::flushScrub() {
    m_scrubTimer.stop();
    if(!m_scrubbing||m_scrubPending<0)return;
    navigateFrame(std::exchange(m_scrubPending,-1),true);
}
void EditorTimelineModel::finishScrub(int frame) {
    if(!m_scrubbing)return;
    const auto owner=QPointer<EditorTimelineModel>(this);const auto serial=m_scrubSerial;
    updateScrub(frame);flushScrub();
    if(owner&&serial==m_scrubSerial&&discardScrub())emit scrubbingChanged();
}
void EditorTimelineModel::cancelScrub() {
    if(!m_scrubbing)return;
    const int initial=m_scrubInitial;discardScrub();const auto serial=m_scrubSerial;
    const auto owner=QPointer<EditorTimelineModel>(this);navigateFrame(initial,true);
    if(owner&&serial==m_scrubSerial)emit scrubbingChanged();
}
