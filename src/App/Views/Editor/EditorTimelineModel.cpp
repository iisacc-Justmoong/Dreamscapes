#include "EditorTimelineModel.h"
#include "EditorTimelineThumbnails_p.h"
#include "EditorAudioPlayback.h"
#include "EditorToolUtils.h"
#include "App/Generation/ImageCanvas.h"
#include <QScopedValueRollback>
#include <QUuid>
#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <limits>
#include <span>

using namespace iiSharedCanvas;
namespace {
std::string uniqueId(const char *prefix) { return std::string(prefix)+QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString(); }
QVariantMap findRow(const QVariantList &rows,const QString &id) {
    for(const auto &row:rows) if(row.toMap().value("id").toString()==id)return row.toMap();
    return {};
}
using SnapAnchor=std::pair<int,std::size_t>;
std::vector<SnapAnchor> clipSnapAnchors(const QVariantList &clips,const QString &id,const QStringList &selection,bool linked) {
    const QSet<QString> moving(selection.cbegin(),selection.cend());
    std::vector<SnapAnchor> anchors;anchors.reserve(std::size_t(clips.size())*2);
    std::size_t priority=5; // Live document/playhead/range anchors precede clip edges.
    for(const auto &value:clips) {
        const auto row=value.toMap();const auto clipId=row["id"].toString();
        if(clipId==id||(linked&&moving.contains(clipId)))continue;
        anchors.emplace_back(row["in"].toInt(),priority++);
        anchors.emplace_back(row["out"].toInt(),priority++);
    }
    std::sort(anchors.begin(),anchors.end());
    // Keep the first original priority at coincident boundaries.
    anchors.erase(std::unique(anchors.begin(),anchors.end(),[](const auto &a,const auto &b){return a.first==b.first;}),anchors.end());
    return anchors;
}
int snapToAnchors(const QVariantMap &clip,int delta,const QString &edge,int tolerance,
    std::span<const SnapAnchor> clips,const std::array<int,5> &live) {
    std::int64_t nearest=std::int64_t(tolerance)+1,adjustment=0;
    for(const auto position:{clip["in"].toInt(),clip["out"].toInt()}) {
        if((edge=="in"&&position!=clip["in"].toInt())||(edge=="out"&&position!=clip["out"].toInt()))continue;
        std::int64_t distance=std::int64_t(tolerance)+1,offset=0;
        auto priority=std::numeric_limits<std::size_t>::max();
        const auto consider=[&](int frame,std::size_t rank) {
            const auto candidate=std::int64_t(frame)-position-delta;
            const auto result=std::int64_t(delta)+candidate;
            if(result<std::numeric_limits<int>::min()||result>std::numeric_limits<int>::max())return;
            const auto amount=std::abs(candidate);
            if(amount<distance||(amount==distance&&rank<priority)) {distance=amount;priority=rank;offset=candidate;}
        };
        for(std::size_t index=0;index<live.size();++index)consider(live[index],index);
        const auto target=std::int64_t(position)+delta;
        const auto next=std::lower_bound(clips.begin(),clips.end(),target,[](const auto &anchor,std::int64_t frame){return anchor.first<frame;});
        if(next!=clips.end())consider(next->first,next->second);
        if(next!=clips.begin()){const auto previous=std::prev(next);consider(previous->first,previous->second);}
        // Equal-distance In/Out candidates preserve the first edge's priority.
        if(distance<nearest){nearest=distance;adjustment=offset;}
    }
    return nearest<=tolerance?int(std::int64_t(delta)+adjustment):delta;
}
LayerFrameRange rangeOf(const Document &d,const Layer &layer) {
    return layerProperties(layer).frameRange.value_or(LayerFrameRange{0,d.timeline.frameCount-1});
}
bool dynamic(const Layer &layer) { return contentKind(layer)==ContentKind::Video || layerTiming(layer)==LayerTiming::Dynamic; }
std::vector<KeyframePlacement> contentKeys(const Document &d,const std::string &id) {
    std::vector<KeyframePlacement> result;
    for(const auto &frame:d.frames)for(const auto &key:frame.keyframes)if(key.layerId==id)result.push_back({frame.index,key.assetId});
    return result;
}
QUrl renderThumbnail(const Document &document,const QString &id,int frame,const FrameRenderTileRequest &request) {
    const auto *layer=findLayer(document,id.toStdString());if(!layer)return {};
    const auto rendered=renderFrameLayerTiles(document,FrameIndex(frame),std::size_t(layer-document.layers.data()),{request});
    if(!rendered.ok()||rendered.tiles.empty())return {};
    return editorTools::dataImage(editorTools::image(rendered.tiles.front().pixels));
}
Layer cloneLayer(Document &d,const Layer &original,const std::string &id) {
    auto copy=original; const auto previous=layerProperties(original).id;
    layerProperties(copy).id=id; layerProperties(copy).name+=" copy";
    std::map<std::string,std::string> copied;
    const auto cloneAsset=[&](const std::string &assetId) {
        if(copied.contains(assetId))return copied.at(assetId);
        auto asset=*findAsset(d,assetId); const auto replacement=uniqueId("timeline.asset.");
        std::visit([&](auto &value){value.id=replacement;},asset); d.assets.emplace_back(std::move(asset)); copied[assetId]=replacement; return replacement;
    };
    std::visit([&](auto &layer) {
        const auto source=[&](auto &value) {
            if constexpr(requires{value.assetId;})value.assetId=cloneAsset(value.assetId);
            else {
                const auto keys=contentKeys(d,previous);
                for(const auto &key:keys) {
                    auto frame=std::find_if(d.frames.begin(),d.frames.end(),[&](const Frame &f){return f.index==key.frame;});
                    frame->keyframes.push_back({id,cloneAsset(key.assetId)});
                }
            }
        };
        if constexpr(requires{layer.content;})source(layer.content); else std::visit(source,layer.source);
    },copy);
    return copy;
}
bool shiftLayer(Document &d,const std::string &id,int delta) {
    const auto *layer=findLayer(d,id); if(!layer)return false;
    const auto range=rangeOf(d,*layer);
    if(std::int64_t(range.firstFrame)+delta<0)return false;
    const auto in=FrameIndex(std::int64_t(range.firstFrame)+delta),out=FrameIndex(std::int64_t(range.lastFrame)+delta);
    auto keys=contentKeys(d,id); const auto motion=layerProperties(*layer).motion;
    FrameIndex extent=std::max(d.timeline.frameCount,out+1);
    for(const auto &key:keys)if(std::int64_t(key.frame)+delta>=0)extent=std::max(extent,FrameIndex(std::int64_t(key.frame)+delta)+1);
    DocumentEditor editor(d); if(!editor.setFrameCount(extent).ok())return false;
    if(!keys.empty()) {
        std::vector<KeyframePlacement> shifted; std::string held;
        for(auto key:keys) {
            const auto frame=std::int64_t(key.frame)+delta;
            if(frame<0){held=key.assetId;continue;} key.frame=FrameIndex(frame); shifted.push_back(std::move(key));
        }
        // Native dynamic sources always own frame zero, even when the visible
        // range starts later. Preserve that anchor while shifting the content.
        if(!held.empty() && (shifted.empty() || shifted.front().frame>0))shifted.insert(shifted.begin(),{0,held});
        if(!shifted.empty() && shifted.front().frame>0)shifted.insert(shifted.begin(),{0,shifted.front().assetId});
        if(!editor.setKeyframedSource(id,std::move(shifted)).ok())return false;
    }
    if(!motion.empty()) {
        std::vector<MotionKeyframe> shifted; std::optional<MotionKeyframe> held;
        for(auto key:motion) { const auto frame=std::int64_t(key.frame)+delta; if(frame<0){held=key;continue;} key.frame=FrameIndex(frame); shifted.push_back(key); }
        if(held && (shifted.empty() || shifted.front().frame>in)){held->frame=in;shifted.insert(shifted.begin(),*held);}
        if(!editor.setLayerMotion(id,std::move(shifted)).ok())return false;
    }
    return editor.setLayerFrameRange(id,LayerFrameRange{in,out}).ok();
}
}
EditorTimelineModel::EditorTimelineModel(QObject *parent):EditorTimelineModel(nullptr,parent) {}
EditorTimelineModel::EditorTimelineModel(EditorAudioPlayback *playback,QObject *parent):QObject(parent),m_overridePlayback(playback) {
    connect(&m_playbackTimer,&QTimer::timeout,this,&EditorTimelineModel::updatePlaybackPosition);
    m_scrubTimer.setSingleShot(true);m_scrubTimer.setInterval(16);m_scrubTimer.setTimerType(Qt::PreciseTimer);
    connect(&m_scrubTimer,&QTimer::timeout,this,&EditorTimelineModel::flushScrub);
}
EditorTimelineModel::~EditorTimelineModel(){
    disconnect(this,nullptr,nullptr,nullptr);if(m_canvas)disconnect(m_canvas,nullptr,this,nullptr);
    if(m_playback)disconnect(m_playback,nullptr,this,nullptr);
    endPlayback(true,false);m_thumbnailWorker.reset();
}
void EditorTimelineModel::invalidateThumbnails() {
    cancelThumbnails();m_thumbnails.clear();m_thumbnailSnapshot.reset();
    ++m_thumbnailRevision;emit thumbnailsChanged();
}
void EditorTimelineModel::cancelThumbnails() {
    ++m_thumbnailGeneration;m_requestedThumbnails.clear();
    if(m_thumbnailWorker)m_thumbnailWorker->cancelPending();
}
const Document *EditorTimelineModel::document() const { return m_canvas && m_canvas->documentReady()?m_canvas->document():nullptr; }
void EditorTimelineModel::setCanvas(EditorCanvas *canvas) {
    if(m_canvas==canvas)return;
    const auto owner=QPointer<EditorTimelineModel>(this);endPlayback();if(!owner)return;
    if(discardScrub())emit scrubbingChanged();if(!owner)return;
    if(m_playback)disconnect(m_playback,nullptr,this,nullptr);
    if(m_canvas)disconnect(m_canvas,nullptr,this,nullptr);
    cancelDrag();
    m_canvas=canvas; m_strokeRefreshPending=false; m_selected.clear(); m_selectedKey.clear(); m_selectedTrack.clear(); m_solo.clear(); m_soloVisibility.clear(); m_audioLocks.clear();
    invalidateThumbnails();
    if(canvas) {
        const auto contentChanged=[this] {
            const auto owner=QPointer<EditorTimelineModel>(this);endPlayback();if(!owner)return;
            if(discardScrub())emit scrubbingChanged();if(!owner)return;
            if(m_canvas&&m_canvas->liveStrokeActive()){m_strokeRefreshPending=true;return;}
            m_strokeRefreshPending=false;invalidateThumbnails();refresh();
        };
        connect(canvas,&CanvasItem::documentChanged,this,contentChanged);
        connect(canvas,&CanvasItem::revisionChanged,this,contentChanged);
        connect(canvas,&CanvasItem::liveStrokeActiveChanged,this,[this,contentChanged] {
            if(m_canvas&&!m_canvas->liveStrokeActive()&&m_strokeRefreshPending)contentChanged();
        });
        connect(canvas,&CanvasItem::selectionChanged,this,[this] {
            if(m_refreshing)return;
            const auto id=m_canvas->selectedLayerId();
            if(!id.isEmpty() && m_selected!=QStringList{id}){cancelDrag();m_selected={id};emit changed();}
        });
        connect(canvas,&CanvasItem::frameChanged,this,[this] {
            const auto owner=QPointer<EditorTimelineModel>(this);
            if(m_scrubbing&&!m_advancingScrub){discardScrub();emit scrubbingChanged();if(!owner)return;}
            if(m_playing&&!m_advancingPlayback)endPlayback();
            if(owner)emit playheadChanged();
        });
        connect(canvas,&EditorCanvas::activeArtboardChanged,this,contentChanged);
        connect(canvas,&EditorCanvas::undoRedoChanged,this,&EditorTimelineModel::changed);
        connect(canvas,&EditorCanvas::toolStateChanged,this,&EditorTimelineModel::updateLocks);
        connect(canvas,&QObject::destroyed,this,[this]{m_canvas=nullptr;invalidateThumbnails();refresh();});
    }
    if(canvas&&!canvas->selectedLayerId().isEmpty())m_selected={canvas->selectedLayerId()};
    bindPlayback();m_markIn=0; m_markOut=totalFrames(); refresh(); emit playheadChanged();
}
double EditorTimelineModel::fps() const { const auto *d=document();return d?double(d->timeline.frameRate.numerator)/d->timeline.frameRate.denominator:24; }
int EditorTimelineModel::totalFrames() const { return document()?int(document()->timeline.frameCount):1; }
int EditorTimelineModel::playhead() const { return m_canvas?int(m_canvas->frame()):0; }
bool EditorTimelineModel::canUndo() const { return m_canvas && m_canvas->canUndo(); }
bool EditorTimelineModel::canRedo() const { return m_canvas && m_canvas->canRedo(); }
QVariantMap EditorTimelineModel::selectedClip() const { return m_selected.isEmpty()?QVariantMap{}:findRow(m_clips,m_selected.front()); }
QVariantMap EditorTimelineModel::selectedKey() const { return findRow(m_keys,m_selectedKey); }
QString EditorTimelineModel::selectedTrackId() const { const auto key=selectedKey();return key.isEmpty()?selectedClip().value("trackId",m_selectedTrack).toString():key.value("trackId").toString(); }
bool EditorTimelineModel::locked(const QString &id) const { return m_audioLocks.contains(id) || (m_canvas && (m_canvas->m_pixelLocks.contains(id)||m_canvas->m_positionLocks.contains(id))); }
bool EditorTimelineModel::reject(const QString &error) {m_error=error;emit changed();return false;}
bool EditorTimelineModel::mutate(const std::function<bool(Document &)> &edit) {
    if(!m_canvas)return reject(tr("Open a canvas first."));
    m_error.clear();bool committed=false;
    { QScopedValueRollback<bool> guard(m_refreshing,true);committed=m_canvas->commit(edit); }
    if(!committed)return reject(m_canvas->error().isEmpty()?tr("The edit is outside the document or source range."):m_canvas->error());
    refresh();return true;
}
void EditorTimelineModel::refresh() {
    if(m_refreshing)return;
    const auto owner=QPointer<EditorTimelineModel>(this);
    if(discardScrub())emit scrubbingChanged();if(!owner)return;
    QScopedValueRollback<bool> guard(m_refreshing,true);
    cancelDrag();
    const auto oldTracks=m_tracks,oldClips=m_clips,oldKeys=m_keys;
    m_tracks.clear();m_clips.clear();m_keys.clear();
    if(const auto *d=document()) {
        std::map<std::string,std::vector<KeyframePlacement>> keysByLayer;
        for(const auto &frame:d->frames)for(const auto &key:frame.keyframes)
            keysByLayer[key.layerId].push_back({frame.index,key.assetId});
        bool soloActive=!m_solo.isEmpty();
        for(const auto &layer:d->layers)if(m_canvas->belongsToActiveArtboard(layer)) {
            const auto &p=layerProperties(layer);soloActive&=p.visible==(QString::fromStdString(p.id)==m_solo);
        }
        for(const auto &track:d->audioTracks)soloActive&=(!track.muted)==(QString::fromStdString(track.id)==m_solo);
        for(auto it=d->layers.crbegin();it!=d->layers.crend();++it) {
            if(!m_canvas->belongsToActiveArtboard(*it))continue;
            const auto &p=layerProperties(*it); const auto id=QString::fromStdString(p.id),name=QString::fromStdString(p.name);
            const auto range=rangeOf(*d,*it); const auto kind=contentKind(*it);
            const QString representation=kind==ContentKind::Vector?"Vector":kind==ContentKind::Video?"Video":kind==ContentKind::IpAdapter?"Embedding":"Bitmap";
            const auto *video=std::get_if<VideoLayer>(&*it);
            m_tracks.push_back(QVariantMap{{"id",id},{"name",name},{"type","Layer"},{"locked",locked(id)},{"muted",!p.visible},{"solo",soloActive&&m_solo==id}});
            m_clips.push_back(QVariantMap{{"id",id},{"trackId",id},{"name",name},{"type","Layer"},{"in",int(range.firstFrame)},{"out",int(range.lastFrame+1)},
                {"frames",int(range.lastFrame-range.firstFrame+1)},{"timing",dynamic(*it)?"Dynamic":"Static"},{"representation",representation},
                {"opacity",p.opacity},{"excluded",!p.visible},{"locked",locked(id)},{"sourceFrame",video?int(video->playback.sourceInFrame):0},{"sourceEditable",video!=nullptr}});
            for(const auto &key:keysByLayer[p.id])m_keys.push_back(QVariantMap{{"id",id+":content:"+QString::number(key.frame)}, {"trackId",id},{"frame",int(key.frame)},
                {"kind","Content"},{"assetId",QString::fromStdString(key.assetId)},{"value",p.opacity}});
            for(const auto &key:p.motion)m_keys.push_back(QVariantMap{{"id",id+":motion:"+QString::number(key.frame)},{"trackId",id},{"frame",int(key.frame)},
                {"kind","Motion"},{"value",key.value.opacity},{"interpolation",key.interpolation==MotionInterpolation::Hold?"Hold":key.interpolation==MotionInterpolation::SmoothStep?"Smooth":"Linear"}});
        }
        for(const auto &track:d->audioTracks) {
            const auto id=QString::fromStdString(track.id);
            m_tracks.push_back(QVariantMap{{"id",id},{"name",QString::fromStdString(track.name)},{"type","Audio"},{"locked",locked(id)},{"muted",track.muted},{"solo",soloActive&&m_solo==id}});
            for(const auto &clip:track.clips) {
                QVariantList waveform; const auto *asset=findAudioAsset(*d,clip.assetId);
                if(asset && !asset->samples.empty()) {
                    const auto count=audioSampleFrameCount(clip.durationFrames,d->timeline.frameRate,asset->sampleRate).value_or(0);
                    for(std::uint64_t bucket=0;bucket<64;++bucket) {
                        double peak=0; const auto first=clip.sourceOffsetSamples+count*bucket/64,last=clip.sourceOffsetSamples+count*(bucket+1)/64;
                        const auto step=std::max<std::uint64_t>(1,(last-first)/128);
                        for(auto sample=first;sample<last;sample+=step)for(unsigned channel=0;channel<asset->channelCount;++channel) {
                            const auto index=sample*asset->channelCount+channel;
                            if(index<asset->samples.size())peak=std::max(peak,std::abs(double(asset->samples[index]))/32768);
                        }
                        waveform.push_back(peak);
                    }
                }
                m_clips.push_back(QVariantMap{{"id",QString::fromStdString(clip.id)},{"trackId",id},{"name",QString::fromStdString(clip.name)},{"type","Audio"},
                    {"in",int(clip.startFrame)},{"out",int(clip.startFrame+clip.durationFrames)},{"frames",int(clip.durationFrames)}, {"timing","Dynamic"},
                    {"excluded",!clip.enabled||track.muted},{"locked",locked(id)},{"waveform",waveform},{"sourceFrame",asset?int(double(clip.sourceOffsetSamples)*fps()/asset->sampleRate):0},{"sourceEditable",true}});
            }
        }
    }
    m_selected.removeIf([&](const QString &id){return findRow(m_clips,id).isEmpty();});
    if(findRow(m_tracks,m_selectedTrack).isEmpty())m_selectedTrack.clear();
    if(!m_solo.isEmpty()&&findRow(m_tracks,m_solo).isEmpty()){m_solo.clear();m_soloVisibility.clear();}
    if(findRow(m_keys,m_selectedKey).isEmpty())m_selectedKey.clear();
    m_markIn=std::clamp(m_markIn,0,totalFrames()-1);m_markOut=std::clamp(m_markOut,1,totalFrames());
    const bool projectionUpdated=oldTracks!=m_tracks||oldClips!=m_clips||oldKeys!=m_keys
        ||m_projectedFps!=fps()||m_projectedFrames!=totalFrames();
    m_projectedFps=fps();m_projectedFrames=totalFrames();
    m_layerCount=0;m_keptCount=0;m_hasAudio=false;
    for(const auto &value:m_clips){const auto clip=value.toMap();if(clip["type"]=="Layer"){
        ++m_layerCount;if(!clip["excluded"].toBool())++m_keptCount;}}
    for(const auto &value:m_tracks)m_hasAudio|=value.toMap()["type"]=="Audio";
    updateLayout();
    if(projectionUpdated){emit projectionChanged();emit playheadChanged();}
    emit changed();
}
void EditorTimelineModel::setRulerHeight(int value) {
    if(value<16||value>96||value==m_rulerHeight)return;
    m_rulerHeight=value;
    updateLayout(); // View geometry only; never a document command or refresh.
}
void EditorTimelineModel::updateLayout() {
    QHash<QString,QVariantList> clips,keys,motion;
    for(const auto &value:m_clips){const auto row=value.toMap();
        if(row["type"]=="Audio"||m_filter=="All"||(m_filter=="Out"?row["excluded"].toBool():!row["excluded"].toBool()))
            clips[row["trackId"].toString()].append(value);}
    for(const auto &value:m_keys){const auto row=value.toMap();const auto id=row["trackId"].toString();
        keys[id].append(value);if(row["kind"]=="Motion")motion[id].append(value);}
    QVariantList layout;int top=m_rulerHeight;
    for(const auto &value:m_tracks){auto row=value.toMap();const auto id=row["id"].toString();
        if(row["type"]!="Audio"&&m_filter!="All"&&clips.value(id).isEmpty())continue;
        auto motionKeys=motion.value(id);
        std::stable_sort(motionKeys.begin(),motionKeys.end(),[](const QVariant &a,const QVariant &b){return a.toMap()["frame"].toInt()<b.toMap()["frame"].toInt();});
        const bool curve=m_mode=="Curves"&&!motionKeys.isEmpty();
        const int height=(row["type"]=="Audio"?48:40)+(curve?64:0);
        row["top"]=top;row["height"]=height;row["curve"]=curve;
        row["clips"]=clips.value(id);row["keys"]=m_mode=="Layers"?QVariantList{}:keys.value(id);row["motionKeys"]=motionKeys;
        top+=height;layout.append(row);
    }
    const int height=layout.isEmpty()?m_rulerHeight+82:top;
    if(m_trackLayout!=layout||m_laneHeight!=height){m_trackLayout=std::move(layout);m_laneHeight=height;emit layoutChanged();}
}
QString EditorTimelineModel::timecode() const {
    const int rate=std::max(1,int(std::round(fps()))),frame=playhead();
    const auto padded=[](int value){return QString::number(value).rightJustified(2,'0');};
    return padded(frame/rate/3600)+':'+padded(frame/rate/60%60)+':'+padded(frame/rate%60)+':'+padded(frame%rate)
        +QStringLiteral(" · F")+QString::number(frame).rightJustified(3,'0');
}
void EditorTimelineModel::updateLocks() {
    if(m_refreshing)return;
    bool updated=false;
    const auto update=[&](QVariantList &rows,const char *key){
        for(auto &entry:rows){auto row=entry.toMap();const bool value=locked(row.value(key).toString());
            if(row.value("locked").toBool()!=value){row["locked"]=value;entry=std::move(row);updated=true;}}
    };
    update(m_tracks,"id");update(m_clips,"trackId");
    if(updated){cancelDrag();updateLayout();emit projectionChanged();emit changed();}
}
void EditorTimelineModel::setMode(const QString &value){if(QStringList{"Layers","Keyframes","Curves"}.contains(value)&&m_mode!=value){cancelDrag();m_mode=value;updateLayout();emit changed();}}
void EditorTimelineModel::setTool(const QString &value){if(QStringList{"Select","Blade","Trim","Ripple","Roll","Slip","Slide","Range"}.contains(value)){if(m_tool!=value){cancelDrag();m_tool=value;emit changed();}}}
void EditorTimelineModel::setFilter(const QString &value){if(QStringList{"All","Kept","Out"}.contains(value)){if(m_filter!=value){cancelDrag();m_filter=value;updateLayout();emit changed();}}}
void EditorTimelineModel::setSnap(bool value){if(m_snap!=value){cancelDrag();m_snap=value;emit changed();}}
void EditorTimelineModel::setLinked(bool value){if(m_linked!=value){cancelDrag();m_linked=value;emit changed();}}
void EditorTimelineModel::jumpCut(int direction){int next=direction<0?0:totalFrames()-1;for(const auto &row:m_clips){const auto clip=row.toMap();for(const auto f:{clip["in"].toInt(),clip["out"].toInt()})if(direction<0&&f<playhead())next=std::max(next,f);else if(direction>0&&f>playhead())next=std::min(next,f);}seek(next);}
bool EditorTimelineModel::selectClip(const QString &id,bool multiple) {
    const auto clip=findRow(m_clips,id);if(clip.isEmpty())return false;
    cancelDrag();
    m_selectedKey.clear();m_selectedTrack=clip["trackId"].toString();
    if(multiple){if(m_selected.contains(id))m_selected.removeAll(id);else m_selected.push_back(id);}else m_selected={id};
    if(m_canvas&&clip["type"]=="Layer") {QScopedValueRollback<bool> guard(m_refreshing,true);m_canvas->selectLayer(id);}
    emit changed();return true;
}
bool EditorTimelineModel::selectTrack(const QString &id) {
    if(findRow(m_tracks,id).isEmpty())return false;
    cancelDrag();m_selectedTrack=id;m_selectedKey.clear();m_selected.clear();
    for(const auto &value:m_clips)if(value.toMap()["trackId"].toString()==id)m_selected.push_back(value.toMap()["id"].toString());
    if(findRow(m_tracks,id)["type"]=="Layer" && m_canvas) {
        QScopedValueRollback<bool> guard(m_refreshing,true);m_canvas->selectLayer(id);
    }
    emit changed();return true;
}
void EditorTimelineModel::selectAll(){cancelDrag();m_selected.clear();for(const auto &row:m_clips)m_selected.push_back(row.toMap()["id"].toString());m_selectedKey.clear();emit changed();}
void EditorTimelineModel::clearSelection(){cancelDrag();m_selected.clear();m_selectedKey.clear();m_selectedTrack.clear();emit changed();}
bool EditorTimelineModel::selectKey(const QString &id){const auto key=findRow(m_keys,id);if(key.isEmpty())return false;selectClip(key["trackId"].toString());m_selectedKey=id;if(m_mode=="Layers")m_mode="Keyframes";updateLayout();emit changed();return true;}
QUrl EditorTimelineModel::thumbnail(const QString &id,int frame) const {
    const auto *d=document();const auto *layer=d?findLayer(*d,id.toStdString()):nullptr;if(!layer)return {};
    const auto range=rangeOf(*d,*layer);frame=std::clamp(frame,int(range.firstFrame),int(range.lastFrame));
    if(!dynamic(*layer)&&layerProperties(*layer).motion.empty())frame=int(range.firstFrame);
    const auto key=id+":"+QString::number(frame);if(const auto *cached=m_thumbnails.object(key))return *cached;
    const auto origin=m_canvas->editingOrigin();
    const FrameRenderTileRequest request{{{int(origin.x()),int(origin.y())},{m_canvas->editingWidth(),m_canvas->editingHeight()}},{64,64},RasterSampling::Smooth};
    const auto source=renderThumbnail(*d,id,frame,request);m_thumbnails.insert(key,new QUrl(source));return source;
}
QUrl EditorTimelineModel::requestThumbnail(const QString &id,int frame) {
    const auto *d=document();const auto *layer=d?findLayer(*d,id.toStdString()):nullptr;if(!layer)return {};
    const auto range=rangeOf(*d,*layer);frame=std::clamp(frame,int(range.firstFrame),int(range.lastFrame));
    if(!dynamic(*layer)&&layerProperties(*layer).motion.empty())frame=int(range.firstFrame);
    const auto key=id+":"+QString::number(frame);
    if(const auto *cached=m_thumbnails.object(key)) {
        m_requestedThumbnails.remove(id);
        if(m_thumbnailWorker)m_thumbnailWorker->cancel(id);
        return *cached;
    }
    if(m_canvas->liveStrokeActive())return {};
    if(m_requestedThumbnails.value(id)==key)return {};
    m_requestedThumbnails.insert(id,key);
    if(!m_thumbnailSnapshot)m_thumbnailSnapshot=m_canvas->editingFrameSnapshot().document;
    if(!m_thumbnailWorker)m_thumbnailWorker=std::make_unique<EditorTimelineThumbnails>();
    const auto snapshot=m_thumbnailSnapshot;const auto generation=m_thumbnailGeneration;
    const auto origin=m_canvas->editingOrigin();
    const FrameRenderTileRequest request{{{int(origin.x()),int(origin.y())},{m_canvas->editingWidth(),m_canvas->editingHeight()}},{64,64},RasterSampling::Smooth};
    const auto discarded=m_thumbnailWorker->request(id,[snapshot,id,frame,request] {
        return snapshot?renderThumbnail(*snapshot,id,frame,request):QUrl{};
    },[this,generation,id,key](const QUrl &source) {
        if(generation!=m_thumbnailGeneration||m_requestedThumbnails.value(id)!=key)return;
        m_requestedThumbnails.remove(id);m_thumbnails.insert(key,new QUrl(source));
        emit thumbnailReady(id);
    });
    if(!discarded.isEmpty())m_requestedThumbnails.remove(discarded);
    return {};
}
QVariant EditorTimelineModel::frameNumber(const QString &text) const {
    const auto value = m_numberInputs.queries().send({dreamscapes::vm::NumberInputKind::frame,text.toStdString()});
    return value && value->accepted ? QVariant(value->value) : QVariant(std::numeric_limits<double>::quiet_NaN());
}
bool EditorTimelineModel::setRangeValues(const QString &id, double first, double last) {
    if (!std::isfinite(first) || !std::isfinite(last) || std::floor(first) != first || std::floor(last) != last ||
        first < 0 || last > std::numeric_limits<int>::max() || last <= first) return false;
    return setRange(id,int(first),int(last));
}
bool EditorTimelineModel::editFrameText(FrameField field, const QString &id, const QString &text) {
    const auto input = m_numberInputs.queries().send({dreamscapes::vm::NumberInputKind::frame,text.toStdString()});
    if (!input || !input->accepted) return reject(tr("Enter a valid frame number."));
    const int value = int(input->value);
    if (field == Seek) { setPlaying(false); seek(value); return true; }
    if (field == Source) return setSourceFrame(id,value);
    if (field == Key) return moveKey(id,value);
    const auto clip = findRow(m_clips,id);
    if (clip.isEmpty()) return false;
    const double first = clip.value("in").toDouble(), last = clip.value("out").toDouble();
    if (field == In) return setRangeValues(id,value,last);
    if (field == Out) return setRangeValues(id,first,value);
    if (field == Duration) return setRangeValues(id,first,first + value);
    return false;
}
bool EditorTimelineModel::setRange(const QString &id,int in,int out) {
    const auto clip=findRow(m_clips,id);if(clip.isEmpty()||in<0||out<=in||locked(clip["trackId"].toString()))return reject(tr("Choose an unlocked layer and a positive duration."));
    return mutate([&](Document &d){DocumentEditor e(d);if(out>int(d.timeline.frameCount)&&!e.setFrameCount(FrameIndex(out)).ok())return false;
        if(clip["type"]=="Layer")return e.setLayerFrameRange(id.toStdString(),LayerFrameRange{FrameIndex(in),FrameIndex(out-1)}).ok();
        auto value=*findAudioClip(*findAudioTrack(d,clip["trackId"].toString().toStdString()),id.toStdString());value.startFrame=FrameIndex(in);value.durationFrames=FrameIndex(out-in);
        return e.replaceAudioClip(clip["trackId"].toString().toStdString(),id.toStdString(),std::move(value)).ok();});
}
bool EditorTimelineModel::trim(const QString &id,const QString &edge,int delta){const auto clip=findRow(m_clips,id);return setRange(id,clip["in"].toInt()+(edge=="in"?delta:0),clip["out"].toInt()+(edge=="out"?delta:0));}
bool EditorTimelineModel::moveClip(const QString &id,int delta) {
    const auto clip=findRow(m_clips,id);if(clip.isEmpty()||locked(clip["trackId"].toString()))return reject(tr("Unlock the layer before moving it."));
    if(clip["type"]=="Audio")return setRange(id,clip["in"].toInt()+delta,clip["out"].toInt()+delta);
    return mutate([&](Document &d){return shiftLayer(d,id.toStdString(),delta);});
}
bool EditorTimelineModel::toolAvailable(const QString &tool) const {
    if(tool=="Select"||tool=="Range")return document()!=nullptr;
    const auto clip=selectedClip();if(clip.isEmpty()||clip["locked"].toBool())return false;
    if(tool=="Blade")return clip["frames"].toInt()>1;
    if(tool=="Trim")return true;
    if(tool=="Slip")return clip["sourceEditable"].toBool();
    if(clip["type"]!="Audio")return false;
    if(tool=="Ripple")return true;
    bool previous=false,next=false;
    for(const auto &value:m_clips) {
        const auto row=value.toMap();if(row["trackId"]!=clip["trackId"]||row["id"]==clip["id"])continue;
        previous|=row["out"]==clip["in"];next|=row["in"]==clip["out"];
    }
    return tool=="Roll" ? next : tool=="Slide" && previous && next;
}
void EditorTimelineModel::markRange(int first,int last) {
    m_markIn=std::clamp(std::min(first,last),0,totalFrames()-1);
    m_markOut=std::clamp(std::max(first,last)+1,1,totalFrames());emit changed();
}
void EditorTimelineModel::selectMarkedRange() {
    cancelDrag();m_selected.clear();m_selectedKey.clear();
    for(const auto &value:m_clips) {const auto clip=value.toMap();if(clip["in"].toInt()<m_markOut && clip["out"].toInt()>m_markIn)m_selected.push_back(clip["id"].toString());}
    emit changed();
}
int EditorTimelineModel::snappedDelta(const QString &id,int delta,const QString &edge,int tolerance) const {
    if(!m_snap||tolerance<=0||m_tool=="Slip")return delta;
    return snapToAnchors(findRow(m_clips,id),delta,edge,tolerance,
        clipSnapAnchors(m_clips,id,m_selected,m_linked),{0,totalFrames(),playhead(),m_markIn,m_markOut});
}
bool EditorTimelineModel::beginDrag(const QString &id,const QString &edge,double pointer,double pixelsPerFrame,int tolerance) {
    cancelDrag();
    const auto clip=findRow(m_clips,id);
    if(!document()||clip.isEmpty()||locked(clip["trackId"].toString())||m_tool=="Blade"
        ||!std::isfinite(pointer)||!std::isfinite(pixelsPerFrame)||pixelsPerFrame<=0
        ||(!edge.isEmpty()&&edge!="in"&&edge!="out"))return false;
    m_dragId=id;m_dragEdge=edge;m_dragPointer=pointer;m_dragPixelsPerFrame=pixelsPerFrame;
    m_dragTolerance=std::max(0,tolerance);
    m_dragClip=clip;m_dragToolAvailable=toolAvailable(m_tool);
    if(m_snap&&m_dragTolerance>0&&m_tool!="Slip")m_dragSnapAnchors=clipSnapAnchors(m_clips,id,m_selected,m_linked);
    const QSet<QString> selected(m_selected.cbegin(),m_selected.cend());
    const bool linked=m_linked&&selected.contains(id);
    const bool serial=m_tool=="Roll"||m_tool=="Slide"||m_tool=="Ripple";
    for(const auto &value:m_clips) {
        const auto row=value.toMap();const auto rowId=row["id"].toString();
        if(serial?(row["trackId"]==clip["trackId"]&&rowId!=id):(linked?selected.contains(rowId):rowId==id))m_dragRows.append(value);
    }
    return updateDrag(pointer);
}
bool EditorTimelineModel::beginKeyDrag(const QString &id,double pointer,double pixelsPerFrame,int tolerance) {
    cancelDrag();
    const auto key=findRow(m_keys,id);
    if(!document()||key.isEmpty()||locked(key["trackId"].toString())
        ||(key["kind"]=="Content"&&key["frame"].toInt()==0)
        ||!std::isfinite(pointer)||!std::isfinite(pixelsPerFrame)||pixelsPerFrame<=0)return false;
    if(m_selectedKey!=id&&!selectKey(id))return false;
    m_dragId=id;m_dragKey=key;m_dragPointer=pointer;m_dragPixelsPerFrame=pixelsPerFrame;
    m_dragTolerance=std::max(0,tolerance);
    if(m_snap&&m_dragTolerance>0)m_dragSnapAnchors=clipSnapAnchors(m_clips,{}, {},false);
    std::size_t priority=5+std::size_t(m_clips.size())*2;
    for(const auto &value:m_keys) {
        const auto other=value.toMap();
        if(other["id"]==id)continue;
        if(other["trackId"]==key["trackId"]&&other["kind"]==key["kind"])
            m_dragOccupiedKeys.push_back(other["frame"].toInt());
        if(m_snap&&m_dragTolerance>0)m_dragSnapAnchors.emplace_back(other["frame"].toInt(),priority++);
    }
    std::sort(m_dragOccupiedKeys.begin(),m_dragOccupiedKeys.end());
    std::sort(m_dragSnapAnchors.begin(),m_dragSnapAnchors.end());
    m_dragSnapAnchors.erase(std::unique(m_dragSnapAnchors.begin(),m_dragSnapAnchors.end(),
        [](const auto &a,const auto &b){return a.first==b.first;}),m_dragSnapAnchors.end());
    return updateDrag(pointer);
}
bool EditorTimelineModel::updateDrag(double pointer) {
    if(!dragging()||!std::isfinite(pointer))return false;
    const auto &clip=m_dragClip;
    int delta=int(std::clamp(std::round((pointer-m_dragPointer)/m_dragPixelsPerFrame),
        double(std::numeric_limits<int>::min()/4),double(std::numeric_limits<int>::max()/4)));
    if(!m_dragKey.isEmpty()) {
        const int initial=m_dragKey["frame"].toInt();
        if(m_snap&&m_dragTolerance>0)delta=snapToAnchors({{"in",initial},{"out",initial}},delta,{},
            m_dragTolerance,m_dragSnapAnchors,{0,totalFrames()-1,playhead(),m_markIn,m_markOut});
        const auto target=qint64(initial)+delta;
        const bool valid=target>=0&&target<totalFrames()
            &&!std::binary_search(m_dragOccupiedKeys.begin(),m_dragOccupiedKeys.end(),int(target));
        auto preview=m_dragKey;preview["frame"]=int(std::clamp<qint64>(target,0,std::numeric_limits<int>::max()));
        preview["valid"]=valid;
        const bool updated=m_keyDragPreview!=preview||m_dragValid!=valid;
        m_dragDelta=delta;m_dragValid=valid;m_keyDragPreview=std::move(preview);
        if(updated)emit dragPreviewChanged();
        return valid;
    }
    if(m_snap&&m_dragTolerance>0&&m_tool!="Slip")
        delta=snapToAnchors(clip,delta,m_dragEdge,m_dragTolerance,m_dragSnapAnchors,{0,totalFrames(),playhead(),m_markIn,m_markOut});
    QVariantMap preview;
    bool valid=true;
    const auto place=[&](const QVariantMap &original,qint64 in,qint64 out,qint64 source) {
        if(original.isEmpty()){valid=false;return;}
        auto row=original;
        valid&=in>=0&&out>in&&out<std::numeric_limits<int>::max()&&source>=0&&source<std::numeric_limits<int>::max()
            &&!locked(original["trackId"].toString());
        row["in"]=int(std::clamp<qint64>(in,0,std::numeric_limits<int>::max()));
        row["out"]=int(std::clamp<qint64>(out,0,std::numeric_limits<int>::max()));
        row["frames"]=row["out"].toInt()-row["in"].toInt();
        row["sourceFrame"]=int(std::clamp<qint64>(source,0,std::numeric_limits<int>::max()));
        preview.insert(row["id"].toString(),row);
    };
    const qint64 first=clip["in"].toInt(),last=clip["out"].toInt(),source=clip["sourceFrame"].toInt();
    if(!m_dragEdge.isEmpty()||m_tool=="Trim") {
        const auto edge=m_dragEdge.isEmpty()?QStringLiteral("out"):m_dragEdge;
        place(clip,first+(edge=="in"?delta:0),last+(edge=="out"?delta:0),source);
    } else if(m_tool=="Slip") {
        valid=clip["sourceEditable"].toBool();
        place(clip,first,last,source+delta);
    } else if(m_tool=="Roll"||m_tool=="Slide"||m_tool=="Ripple") {
        valid=m_dragToolAvailable;
        bool previous=false,next=false;
        for(const auto &value:m_dragRows){const auto row=value.toMap();
            const qint64 in=row["in"].toInt(),out=row["out"].toInt(),offset=row["sourceFrame"].toInt();
            if(m_tool=="Slide"&&out==first){place(row,in,out+delta,offset);previous=true;}
            if((m_tool=="Roll"||m_tool=="Slide")&&in==last){place(row,in+delta,out,offset+delta);next=true;}
            if(m_tool=="Ripple"&&in>=last)place(row,in+delta,out+delta,offset);
        }
        if(m_tool=="Slide"){place(clip,first+delta,last+delta,source);valid&=previous&&next;}
        else {place(clip,first,last+delta,source);if(m_tool=="Roll")valid&=next;}
    } else {
        for(const auto &value:m_dragRows){const auto row=value.toMap();
            place(row,qint64(row["in"].toInt())+delta,qint64(row["out"].toInt())+delta,row["sourceFrame"].toInt());}
    }
    for(auto it=preview.begin();it!=preview.end();++it){auto row=it->toMap();row["valid"]=valid;it.value()=row;}
    const bool updated=m_dragPreview!=preview||m_dragValid!=valid;
    m_dragDelta=delta;m_dragValid=valid;m_dragPreview=std::move(preview);
    if(updated)emit dragPreviewChanged();
    return valid;
}
bool EditorTimelineModel::finishDrag() {
    if(!dragging())return false;
    const auto id=m_dragId,edge=m_dragEdge;const int delta=m_dragDelta;const bool valid=m_dragValid;
    const auto key=m_dragKey,preview=m_keyDragPreview;
    cancelDrag();
    if(!valid)return reject(key.isEmpty()?tr("The drag is outside the unlocked clip or source range.")
        :tr("A keyframe must stay within the timeline and cannot replace an existing key."));
    if(!key.isEmpty())return !delta||moveKey(id,preview["frame"].toInt());
    return dragClip(id,delta,edge,0);
}
void EditorTimelineModel::cancelDrag() {
    if(!dragging())return;
    m_dragId.clear();m_dragEdge.clear();m_dragDelta=0;m_dragValid=false;m_dragPreview.clear();
    m_dragClip.clear();m_dragRows.clear();m_dragSnapAnchors.clear();m_dragToolAvailable=false;
    m_dragKey.clear();m_keyDragPreview.clear();m_dragOccupiedKeys.clear();
    emit dragPreviewChanged();
}
bool EditorTimelineModel::dragClip(const QString &id,int delta,const QString &edge,int tolerance) {
    const auto clip=findRow(m_clips,id);if(clip.isEmpty()||locked(clip["trackId"].toString()))return false;
    delta=snappedDelta(id,delta,edge,tolerance);
    if(!delta)return true;
    if(!edge.isEmpty()||m_tool=="Trim")return trim(id,edge.isEmpty()?QStringLiteral("out"):edge,delta);
    if(m_tool=="Slip")return setSourceFrame(id,clip["sourceFrame"].toInt()+delta);
    if(m_tool=="Roll" || m_tool=="Slide" || m_tool=="Ripple") {
        if(!toolAvailable(m_tool))return false;
        return mutate([&](Document &d){
            auto *track=findAudioTrack(d,clip["trackId"].toString().toStdString());if(!track)return false;
            const auto original=*findAudioClip(*track,id.toStdString());
            auto *current=findAudioClip(*track,id.toStdString());
            const auto changeStart=[&](AudioClip &value,int amount) {
                const auto *asset=findAudioAsset(d,value.assetId);if(!asset)return false;
                const auto offset=std::int64_t(value.sourceOffsetSamples)+std::llround(double(amount)*asset->sampleRate/fps());
                if(offset<0 || std::int64_t(value.startFrame)+amount<0 || std::int64_t(value.durationFrames)-amount<1)return false;
                value.sourceOffsetSamples=std::uint64_t(offset);value.startFrame=FrameIndex(int(value.startFrame)+amount);value.durationFrames=FrameIndex(int(value.durationFrames)-amount);return true;
            };
            if(m_tool=="Roll") {
                if(int(current->durationFrames)+delta<1)return false;
                for(auto &next:track->clips)if(next.id!=current->id&&next.startFrame==original.startFrame+original.durationFrames){if(!changeStart(next,delta))return false;current->durationFrames=FrameIndex(int(current->durationFrames)+delta);return true;}
            } else if(m_tool=="Slide") {
                bool previous=false,next=false;
                for(auto &value:track->clips) {
                    if(value.id==original.id)continue;
                    if(value.startFrame+value.durationFrames==original.startFrame){if(int(value.durationFrames)+delta<1)return false;value.durationFrames=FrameIndex(int(value.durationFrames)+delta);previous=true;}
                    else if(value.startFrame==original.startFrame+original.durationFrames){if(!changeStart(value,delta))return false;next=true;}
                }
                if(!previous||!next||int(current->startFrame)+delta<0)return false;
                current->startFrame=FrameIndex(int(current->startFrame)+delta);return true;
            } else {
                if(int(current->durationFrames)+delta<1)return false;
                current->durationFrames=FrameIndex(int(current->durationFrames)+delta);
                for(auto &value:track->clips)if(value.id!=current->id && value.startFrame>=original.startFrame+original.durationFrames){if(int(value.startFrame)+delta<0)return false;value.startFrame=FrameIndex(int(value.startFrame)+delta);}
                FrameIndex last=1;for(const auto &value:track->clips)last=std::max(last,value.startFrame+value.durationFrames);
                d.timeline.frameCount=std::max(d.timeline.frameCount,last);return true;
            }
            return false;
        });
    }
    const QStringList ids=m_linked && m_selected.contains(id) ? m_selected : QStringList{id};
    for(const auto &selected:ids)if(locked(findRow(m_clips,selected)["trackId"].toString()))return reject(tr("Unlock the linked selection before moving it."));
    return mutate([&](Document &d){for(const auto &selected:ids){const auto row=findRow(m_clips,selected);
        if(row["type"]=="Layer"){if(!shiftLayer(d,selected.toStdString(),delta))return false;}
        else {auto *track=findAudioTrack(d,row["trackId"].toString().toStdString());auto *value=track?findAudioClip(*track,selected.toStdString()):nullptr;if(!value||int(value->startFrame)+delta<0)return false;value->startFrame=FrameIndex(int(value->startFrame)+delta);d.timeline.frameCount=std::max(d.timeline.frameCount,value->startFrame+value->durationFrames);}
    }
    for(auto &track:d.audioTracks)std::stable_sort(track.clips.begin(),track.clips.end(),[](const AudioClip &a,const AudioClip &b){return a.startFrame<b.startFrame;});
    return true;});
}
bool EditorTimelineModel::setSourceFrame(const QString &id,int frame) {
    const auto clip=findRow(m_clips,id);if(clip.isEmpty()||!clip["sourceEditable"].toBool()||frame<0||locked(clip["trackId"].toString()))return false;
    return mutate([&](Document &d){DocumentEditor e(d);if(clip["type"]=="Audio") {
        auto value=*findAudioClip(*findAudioTrack(d,clip["trackId"].toString().toStdString()),id.toStdString());const auto *asset=findAudioAsset(d,value.assetId);
        if(!asset)return false;
        value.sourceOffsetSamples=std::uint64_t(std::llround(double(frame)*asset->sampleRate/fps()));return e.replaceAudioClip(clip["trackId"].toString().toStdString(),id.toStdString(),value).ok();}
        auto *layer=findLayer(d,id.toStdString());const auto *video=layer?std::get_if<VideoLayer>(layer):nullptr;if(!video)return false;
        auto playback=video->playback;playback.sourceInFrame=FrameIndex(frame);return e.setVideoPlayback(id.toStdString(),playback).ok();});
}
bool EditorTimelineModel::setClipVisible(const QString &id,bool visible) {
    const auto clip=findRow(m_clips,id);if(clip.isEmpty()||locked(clip["trackId"].toString()))return false;
    return mutate([&](Document &d){DocumentEditor e(d);if(clip["type"]=="Layer")return e.setLayerVisible(id.toStdString(),visible).ok();
        auto value=*findAudioClip(*findAudioTrack(d,clip["trackId"].toString().toStdString()),id.toStdString());value.enabled=visible;return e.replaceAudioClip(clip["trackId"].toString().toStdString(),id.toStdString(),value).ok();});
}
bool EditorTimelineModel::setTrackMuted(const QString &id,bool muted) {
    const auto row=findRow(m_tracks,id);if(row["type"]=="Layer")return setClipVisible(id,!muted);
    if(locked(id))return false;
    return mutate([&](Document &d){auto value=findAudioTrack(d,id.toStdString());if(!value)return false;auto copy=*value;copy.muted=muted;DocumentEditor e(d);return e.replaceAudioTrack(id.toStdString(),copy).ok();});
}
bool EditorTimelineModel::setTrackLocked(const QString &id,bool value) {
    if(!m_canvas||findRow(m_tracks,id).isEmpty())return false;
    if(findRow(m_tracks,id)["type"]=="Audio"){if(value)m_audioLocks.insert(id);else m_audioLocks.remove(id);}
    else {if(value){m_canvas->m_pixelLocks.insert(id);m_canvas->m_positionLocks.insert(id);}else{m_canvas->m_pixelLocks.remove(id);m_canvas->m_positionLocks.remove(id);}emit m_canvas->toolStateChanged();emit m_canvas->layerStateChanged();}
    refresh();return true;
}
bool EditorTimelineModel::setTrackSolo(const QString &id) {
    const auto row=findRow(m_tracks,id);if(row.isEmpty())return false;
    const auto restoring=row["solo"].toBool();auto saved=m_soloVisibility;
    if(saved.isEmpty())for(const auto &value:m_tracks){const auto item=value.toMap();saved[item["id"].toString()]=!item["muted"].toBool();}
    if(!mutate([&](Document &d){DocumentEditor e(d);for(const auto &[key,wasVisible]:saved.asKeyValueRange()) {
        const bool visible=restoring?wasVisible:key==id;
        if(findLayer(d,key.toStdString())){if(!e.setLayerVisible(key.toStdString(),visible).ok())return false;}
        else if(auto *track=findAudioTrack(d,key.toStdString())){auto copy=*track;copy.muted=!visible;if(!e.replaceAudioTrack(key.toStdString(),copy).ok())return false;}
    }return true;}))return false;
    m_solo=restoring?QString():id;m_soloVisibility=restoring?QMap<QString,bool>():saved;refresh();return true;
}
bool EditorTimelineModel::addLayer(const QString &kind,const QString &name) {
    if(!QStringList{"Static bitmap","Static vector","Dynamic bitmap","Dynamic vector"}.contains(kind))return reject(tr("Choose a native layer kind."));
    if(!m_canvas || (kind.endsWith("bitmap") && std::uint64_t(m_canvas->editingWidth())*m_canvas->editingHeight()>MediaLimits{}.maxPixelsPerFrame))return reject(tr("This canvas exceeds the bitmap layer allocation limit."));
    const auto id=uniqueId("timeline.layer."),asset=uniqueId("timeline.asset.");
    if(!mutate([&](Document &d){DocumentEditor e(d);LayerProperties p;p.id=id;p.name=(name.isEmpty()?kind:name).toStdString();
        if(kind.endsWith("vector")){if(!e.insertVectorAsset(asset,d.extent,{}).ok())return false;}else if(!e.insertRasterAsset(asset,makeRasterLayer(m_canvas->editingWidth(),m_canvas->editingHeight())).ok())return false;
        if(kind.startsWith("Dynamic")){p.frameRange=LayerFrameRange{0,d.timeline.frameCount-1};return e.insertDynamicLayer(p,kind.endsWith("vector")?LayerRepresentation::Vector:LayerRepresentation::Bitmap,{{0,asset}}).ok();}
        return e.insertStaticLayer(p,kind.endsWith("vector")?LayerRepresentation::Vector:LayerRepresentation::Bitmap,asset).ok();}))return false;
    return selectClip(QString::fromStdString(id));
}
bool EditorTimelineModel::addAudioTrack(){const auto id=uniqueId("timeline.audio.");if(!mutate([&](Document &d){DocumentEditor e(d);return e.insertAudioTrack({id,"Audio"}).ok();}))return false;return selectTrack(QString::fromStdString(id));}
bool EditorTimelineModel::renameTrack(const QString &id,const QString &name) {
    if(name.trimmed().isEmpty()||locked(id))return false;
    return mutate([&](Document &d){DocumentEditor e(d);if(findLayer(d,id.toStdString()))return e.setLayerName(id.toStdString(),name.trimmed().toStdString()).ok();
        const auto *track=findAudioTrack(d,id.toStdString());if(!track)return false;auto copy=*track;copy.name=name.trimmed().toStdString();return e.replaceAudioTrack(id.toStdString(),copy).ok();});
}
bool EditorTimelineModel::moveTrack(const QString &id,int direction) {
    if(locked(id))return false;
    return mutate([&](Document &d){DocumentEditor e(d);if(auto *layer=findLayer(d,id.toStdString())) {
        const int index=int(layer-d.layers.data());int next=index+(direction<0?1:-1);
        while(next>=0 && next<int(d.layers.size()) && !m_canvas->belongsToActiveArtboard(d.layers[std::size_t(next)]))next+=(direction<0?1:-1);
        if(next<0||next>=int(d.layers.size()))return false;return e.moveLayer(id.toStdString(),std::size_t(next)).ok();}
        const auto *track=findAudioTrack(d,id.toStdString());if(!track)return false;const int index=int(track-d.audioTracks.data()),next=index+direction;if(next<0||next>=int(d.audioTracks.size()))return false;return e.moveAudioTrack(id.toStdString(),std::size_t(next)).ok();});
}
bool EditorTimelineModel::removeTrack(const QString &id) {
    const auto row=findRow(m_tracks,id);if(row.isEmpty()||locked(id))return false;
    return mutate([&](Document &d){DocumentEditor e(d);return row["type"]=="Layer" ? e.removeLayer(id.toStdString()).ok() : e.removeAudioTrack(id.toStdString()).ok();});
}
bool EditorTimelineModel::split() {
    const auto selected=selectedClip();const auto id=selected["id"].toString();const int head=playhead();
    if(selected.isEmpty()||locked(selected["trackId"].toString())||head<=selected["in"].toInt()||head>=selected["out"].toInt())return reject(tr("Place the playhead inside an unlocked clip."));
    const auto replacement=uniqueId("timeline.split.");
    if(!mutate([&](Document &d){
        if(selected["type"]=="Audio") {
            auto *track=findAudioTrack(d,selected["trackId"].toString().toStdString());auto *left=track?findAudioClip(*track,id.toStdString()):nullptr;if(!left)return false;
            const auto index=left-track->clips.data();auto right=*left;const auto *asset=findAudioAsset(d,left->assetId);if(!asset)return false;
            const auto offset=audioSampleFrameCount(FrameIndex(head)-left->startFrame,d.timeline.frameRate,asset->sampleRate);if(!offset)return false;
            right.id=replacement;right.startFrame=FrameIndex(head);right.durationFrames=left->startFrame+left->durationFrames-FrameIndex(head);right.sourceOffsetSamples+=*offset;
            left->durationFrames=FrameIndex(head)-left->startFrame;track->clips.insert(track->clips.begin()+index+1,std::move(right));return true;
        }
        const auto *layer=findLayer(d,id.toStdString());const auto index=layer-d.layers.data();const auto original=*layer;
        auto copy=cloneLayer(d,original,replacement);const auto range=rangeOf(d,original);
        layerProperties(copy).frameRange=LayerFrameRange{FrameIndex(head),range.lastFrame};
        layerProperties(*findLayer(d,id.toStdString())).frameRange=LayerFrameRange{range.firstFrame,FrameIndex(head-1)};
        d.layers.insert(d.layers.begin()+index+1,std::move(copy));return true;}))return false;
    return selectClip(QString::fromStdString(replacement));
}
bool EditorTimelineModel::duplicateSelected() {
    auto selected=selectedClip();
    if(selected.isEmpty()) { const auto track=findRow(m_tracks,m_selectedTrack);if(track["type"]!="Audio")return false;selected={{"type","Audio"},{"trackId",m_selectedTrack}}; }
    if(locked(selected["trackId"].toString()))return false;
    const auto id=uniqueId("timeline.copy.");
    if(!mutate([&](Document &d){DocumentEditor e(d);if(selected["type"]=="Layer"){const auto *layer=findLayer(d,selected["id"].toString().toStdString());const auto index=layer-d.layers.data();auto copy=cloneLayer(d,*layer,id);d.layers.insert(d.layers.begin()+index+1,std::move(copy));return true;}
        auto *track=findAudioTrack(d,selected["trackId"].toString().toStdString());if(!track)return false;auto copy=*track;copy.id=id;copy.name+=" copy";for(auto &clip:copy.clips)clip.id=uniqueId("timeline.clip.");return e.insertAudioTrack(std::move(copy)).ok();}))return false;
    if(selected["type"]=="Layer")selectClip(QString::fromStdString(id));else selectTrack(QString::fromStdString(id));return true;
}
bool EditorTimelineModel::removeSelected() {
    if(m_selected.isEmpty())return removeTrack(m_selectedTrack);const auto ids=m_selected;
    for(const auto &id:ids)if(locked(findRow(m_clips,id)["trackId"].toString()))return reject(tr("Unlock the selected layers first."));
    return mutate([&](Document &d){DocumentEditor e(d);for(const auto &id:ids) {
        const auto row=findRow(m_clips,id);
        if(row["type"]=="Layer"){if(!e.removeLayer(id.toStdString()).ok())return false;}
        else if(!e.removeAudioClip(row["trackId"].toString().toStdString(),id.toStdString()).ok())return false;
    }return true;});
}
bool EditorTimelineModel::rippleDelete() {
    // Parallel artwork does not have the serial shot durations of a video recipe.
    // Ripple only a genuinely empty interval; never truncate overlapping layers.
    const auto selected=selectedClip();if(selected.isEmpty())return false;
    const int in=selected["in"].toInt(),out=selected["out"].toInt(),length=out-in;
    for(const auto &value:m_clips){const auto clip=value.toMap();if(m_selected.contains(clip["id"].toString()))continue;
        if(clip["in"].toInt()<out&&clip["out"].toInt()>in)return reject(tr("Other content overlaps this interval. Delete the layer without rippling, or choose an empty interval."));}
    const auto selectedIds=m_selected;
    return mutate([&](Document &d){DocumentEditor e(d);for(const auto &id:selectedIds){const auto row=findRow(m_clips,id);if(row["type"]=="Layer"){if(!e.removeLayer(id.toStdString()).ok())return false;}else if(!e.removeAudioClip(row["trackId"].toString().toStdString(),id.toStdString()).ok())return false;}
        const auto remaining=m_clips;for(const auto &value:remaining){const auto clip=value.toMap();if(selectedIds.contains(clip["id"].toString())||clip["in"].toInt()<out)continue;
            if(clip["type"]=="Layer"){if(!shiftLayer(d,clip["id"].toString().toStdString(),-length))return false;}
            else {auto copy=*findAudioClip(*findAudioTrack(d,clip["trackId"].toString().toStdString()),clip["id"].toString().toStdString());copy.startFrame-=FrameIndex(length);if(!e.replaceAudioClip(clip["trackId"].toString().toStdString(),copy.id,copy).ok())return false;}}
        return e.setFrameCount(FrameIndex(std::max(1,int(d.timeline.frameCount)-length))).ok();});
}
bool EditorTimelineModel::moveKey(const QString &id,int frame) {
    const auto key=findRow(m_keys,id);if(key.isEmpty()||frame<0||frame>=totalFrames()||locked(key["trackId"].toString()))return false;
    if(frame==key["frame"].toInt())return true;
    const auto layer=key["trackId"].toString().toStdString();
    const bool selected=m_selectedKey==id;
    const bool moved=mutate([&](Document &d){DocumentEditor e(d);if(key["kind"]=="Content")return e.moveKeyframe(layer,FrameIndex(key["frame"].toInt()),FrameIndex(frame)).ok();
        auto motion=layerProperties(*findLayer(d,layer)).motion;for(auto &value:motion)if(value.frame==FrameIndex(key["frame"].toInt()))value.frame=FrameIndex(frame);
        std::sort(motion.begin(),motion.end(),[](const auto &a,const auto &b){return a.frame<b.frame;});return e.setLayerMotion(layer,std::move(motion)).ok();});
    if(moved&&selected){m_selectedKey=key["trackId"].toString()+(key["kind"]=="Content"?":content:":":motion:")+QString::number(frame);emit changed();}
    return moved;
}
bool EditorTimelineModel::setKeyOpacity(const QString &id,double value) {
    const auto key=findRow(m_keys,id);if(key["kind"]!="Motion"||!std::isfinite(value)||value<0||value>1||locked(key["trackId"].toString()))return false;
    return mutate([&](Document &d){const auto layer=key["trackId"].toString().toStdString();auto motion=layerProperties(*findLayer(d,layer)).motion;
        for(auto &entry:motion)if(entry.frame==FrameIndex(key["frame"].toInt()))entry.value.opacity=value;DocumentEditor e(d);return e.setLayerMotion(layer,std::move(motion)).ok();});
}
bool EditorTimelineModel::removeKey(const QString &id) {
    const auto key=findRow(m_keys,id);if(key.isEmpty()||locked(key["trackId"].toString()))return false;
    return mutate([&](Document &d){DocumentEditor e(d);const auto layer=key["trackId"].toString().toStdString();if(key["kind"]=="Content")return e.removeKeyframe(layer,FrameIndex(key["frame"].toInt())).ok();
        auto motion=layerProperties(*findLayer(d,layer)).motion;std::erase_if(motion,[&](const auto &value){return value.frame==FrameIndex(key["frame"].toInt());});return e.setLayerMotion(layer,std::move(motion)).ok();});
}
bool EditorTimelineModel::importFrameSource(const QString &id,int frame,const QUrl &source) {
    if(frame<0||frame>=totalFrames()||locked(id))return false;
    const auto loaded=dreamscapes::readImageCanvas(source.isLocalFile()?source.toLocalFile():source.toString());
    if(!loaded.ok())return reject(QString::fromStdString(loaded.result.message));
    const auto rendered=renderFrame(loaded.document,0);if(!rendered.ok())return reject(QString::fromStdString(rendered.message));
    return mutate([&](Document &d){const auto *layer=findLayer(d,id.toStdString());if(!layer||layerTiming(*layer)!=LayerTiming::Dynamic||contentKind(*layer)!=ContentKind::Raster)return false;
        DocumentEditor e(d);return e.setDynamicFrameContent(id.toStdString(),FrameIndex(frame),RasterAsset{uniqueId("timeline.frame."),rendered.pixels}).ok();});
}
bool EditorTimelineModel::setFrameRate(int numerator,int denominator){if(numerator<=0||denominator<=0)return false;return mutate([&](Document &d){DocumentEditor e(d);return e.setFrameRate({std::uint32_t(numerator),std::uint32_t(denominator)}).ok();});}
void EditorTimelineModel::undo(){if(m_canvas)m_canvas->undo();refresh();}
void EditorTimelineModel::redo(){if(m_canvas)m_canvas->redo();refresh();}
