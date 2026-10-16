#include "EditorTimelineModel.h"
#include "EditorToolUtils.h"
#include "App/Generation/ImageCanvas.h"
#include <QScopedValueRollback>
#include <QUuid>
#include <algorithm>
#include <cmath>
#include <map>

using namespace iiSharedCanvas;
namespace {
std::string uniqueId(const char *prefix) { return std::string(prefix)+QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString(); }
QVariantMap findRow(const QVariantList &rows,const QString &id) {
    for(const auto &row:rows) if(row.toMap().value("id").toString()==id)return row.toMap();
    return {};
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
EditorTimelineModel::EditorTimelineModel(QObject *parent):QObject(parent) {}
const Document *EditorTimelineModel::document() const { return m_canvas && m_canvas->documentReady()?m_canvas->document():nullptr; }
void EditorTimelineModel::setCanvas(EditorCanvas *canvas) {
    if(m_canvas==canvas)return;
    if(m_canvas)disconnect(m_canvas,nullptr,this,nullptr);
    m_canvas=canvas; m_selected.clear(); m_selectedKey.clear(); m_selectedTrack.clear(); m_solo.clear(); m_soloVisibility.clear(); m_audioLocks.clear();
    m_thumbnails.clear();
    if(canvas) {
        const auto contentChanged=[this]{m_thumbnails.clear();refresh();};
        connect(canvas,&CanvasItem::documentChanged,this,contentChanged);
        connect(canvas,&CanvasItem::revisionChanged,this,contentChanged);
        connect(canvas,&CanvasItem::selectionChanged,this,[this] {
            if(m_refreshing)return;
            const auto id=m_canvas->selectedLayerId();
            if(!id.isEmpty() && m_selected!=QStringList{id}){m_selected={id};emit changed();}
        });
        connect(canvas,&CanvasItem::frameChanged,this,&EditorTimelineModel::playheadChanged);
        connect(canvas,&EditorCanvas::activeArtboardChanged,this,contentChanged);
        connect(canvas,&EditorCanvas::undoRedoChanged,this,&EditorTimelineModel::changed);
        connect(canvas,&EditorCanvas::toolStateChanged,this,&EditorTimelineModel::updateLocks);
        connect(canvas,&QObject::destroyed,this,[this]{m_canvas=nullptr;m_thumbnails.clear();refresh();});
    }
    if(canvas&&!canvas->selectedLayerId().isEmpty())m_selected={canvas->selectedLayerId()};
    m_markIn=0; m_markOut=totalFrames(); refresh(); emit playheadChanged();
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
    QScopedValueRollback<bool> guard(m_refreshing,true);
    m_tracks.clear();m_clips.clear();m_keys.clear();
    if(const auto *d=document()) {
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
            for(const auto &key:contentKeys(*d,p.id))m_keys.push_back(QVariantMap{{"id",id+":content:"+QString::number(key.frame)}, {"trackId",id},{"frame",int(key.frame)},
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
    emit changed();
}
void EditorTimelineModel::updateLocks() {
    if(m_refreshing)return;
    bool updated=false;
    const auto update=[&](QVariantList &rows,const char *key){
        for(auto &entry:rows){auto row=entry.toMap();const bool value=locked(row.value(key).toString());
            if(row.value("locked").toBool()!=value){row["locked"]=value;entry=std::move(row);updated=true;}}
    };
    update(m_tracks,"id");update(m_clips,"trackId");
    if(updated)emit changed();
}
void EditorTimelineModel::setMode(const QString &value){if(QStringList{"Layers","Keyframes","Curves"}.contains(value)&&m_mode!=value){m_mode=value;emit changed();}}
void EditorTimelineModel::setTool(const QString &value){if(QStringList{"Select","Blade","Trim","Ripple","Roll","Slip","Slide","Range"}.contains(value)){m_tool=value;emit changed();}}
void EditorTimelineModel::setFilter(const QString &value){if(QStringList{"All","Kept","Out"}.contains(value)){m_filter=value;emit changed();}}
void EditorTimelineModel::setSnap(bool value){m_snap=value;emit changed();}
void EditorTimelineModel::setLinked(bool value){m_linked=value;emit changed();}
void EditorTimelineModel::seek(int frame){if(m_canvas){QScopedValueRollback<bool> guard(m_refreshing,true);m_canvas->setFrame(quint32(std::clamp(frame,0,totalFrames()-1)));}}
void EditorTimelineModel::jumpCut(int direction){int next=direction<0?0:totalFrames()-1;for(const auto &row:m_clips){const auto clip=row.toMap();for(const auto f:{clip["in"].toInt(),clip["out"].toInt()})if(direction<0&&f<playhead())next=std::max(next,f);else if(direction>0&&f>playhead())next=std::min(next,f);}seek(next);}
bool EditorTimelineModel::selectClip(const QString &id,bool multiple) {
    const auto clip=findRow(m_clips,id);if(clip.isEmpty())return false;
    m_selectedKey.clear();m_selectedTrack=clip["trackId"].toString();
    if(multiple){if(m_selected.contains(id))m_selected.removeAll(id);else m_selected.push_back(id);}else m_selected={id};
    if(m_canvas&&clip["type"]=="Layer") {QScopedValueRollback<bool> guard(m_refreshing,true);m_canvas->selectLayer(id);}
    emit changed();return true;
}
bool EditorTimelineModel::selectTrack(const QString &id) {
    if(findRow(m_tracks,id).isEmpty())return false;
    m_selectedTrack=id;m_selectedKey.clear();m_selected.clear();
    for(const auto &value:m_clips)if(value.toMap()["trackId"].toString()==id)m_selected.push_back(value.toMap()["id"].toString());
    if(findRow(m_tracks,id)["type"]=="Layer" && m_canvas) {
        QScopedValueRollback<bool> guard(m_refreshing,true);m_canvas->selectLayer(id);
    }
    emit changed();return true;
}
void EditorTimelineModel::selectAll(){m_selected.clear();for(const auto &row:m_clips)m_selected.push_back(row.toMap()["id"].toString());m_selectedKey.clear();emit changed();}
void EditorTimelineModel::clearSelection(){m_selected.clear();m_selectedKey.clear();m_selectedTrack.clear();emit changed();}
bool EditorTimelineModel::selectKey(const QString &id){const auto key=findRow(m_keys,id);if(key.isEmpty())return false;selectClip(key["trackId"].toString());m_selectedKey=id;m_mode="Keyframes";emit changed();return true;}
QUrl EditorTimelineModel::thumbnail(const QString &id,int frame) const {
    const auto *d=document();const auto *layer=d?findLayer(*d,id.toStdString()):nullptr;if(!layer)return {};
    const auto range=rangeOf(*d,*layer);frame=std::clamp(frame,int(range.firstFrame),int(range.lastFrame));
    if(!dynamic(*layer)&&layerProperties(*layer).motion.empty())frame=int(range.firstFrame);
    const auto key=id+":"+QString::number(frame);if(const auto *cached=m_thumbnails.object(key))return *cached;
    const auto origin=m_canvas->editingOrigin();
    const FrameRenderTileRequest request{{{int(origin.x()),int(origin.y())},{m_canvas->editingWidth(),m_canvas->editingHeight()}},{64,64},RasterSampling::Smooth};
    const auto rendered=renderFrameLayerTiles(*d,FrameIndex(frame),std::size_t(layer-d->layers.data()),{request});
    if(!rendered.ok()||rendered.tiles.empty())return {};
    const auto source=editorTools::dataImage(editorTools::image(rendered.tiles.front().pixels));m_thumbnails.insert(key,new QUrl(source));return source;
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
    m_selected.clear();m_selectedKey.clear();
    for(const auto &value:m_clips) {const auto clip=value.toMap();if(clip["in"].toInt()<m_markOut && clip["out"].toInt()>m_markIn)m_selected.push_back(clip["id"].toString());}
    emit changed();
}
bool EditorTimelineModel::dragClip(const QString &id,int delta,const QString &edge,int tolerance) {
    const auto clip=findRow(m_clips,id);if(clip.isEmpty()||locked(clip["trackId"].toString()))return false;
    if(m_snap&&tolerance>0 && m_tool!="Slip") {
        int nearest=tolerance+1,adjustment=0;
        QList<int> anchors{0,totalFrames(),playhead(),m_markIn,m_markOut};
        for(const auto &value:m_clips){const auto row=value.toMap();if(row["id"]==id || (m_linked&&m_selected.contains(row["id"].toString())))continue;anchors<<row["in"].toInt()<<row["out"].toInt();}
        for(const auto position:{clip["in"].toInt(),clip["out"].toInt()}) {
            if((edge=="in"&&position!=clip["in"].toInt())||(edge=="out"&&position!=clip["out"].toInt()))continue;
            for(int anchor:anchors){const int offset=anchor-position-delta;if(std::abs(offset)<nearest){nearest=std::abs(offset);adjustment=offset;}}
        }
        if(nearest<=tolerance)delta+=adjustment;
    }
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
    else {if(value){m_canvas->m_pixelLocks.insert(id);m_canvas->m_positionLocks.insert(id);}else{m_canvas->m_pixelLocks.remove(id);m_canvas->m_positionLocks.remove(id);}emit m_canvas->toolStateChanged();emit m_canvas->documentInfoChanged();}
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
    const auto layer=key["trackId"].toString().toStdString();
    return mutate([&](Document &d){DocumentEditor e(d);if(key["kind"]=="Content")return e.moveKeyframe(layer,FrameIndex(key["frame"].toInt()),FrameIndex(frame)).ok();
        auto motion=layerProperties(*findLayer(d,layer)).motion;for(auto &value:motion)if(value.frame==FrameIndex(key["frame"].toInt()))value.frame=FrameIndex(frame);
        std::sort(motion.begin(),motion.end(),[](const auto &a,const auto &b){return a.frame<b.frame;});return e.setLayerMotion(layer,std::move(motion)).ok();});
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
