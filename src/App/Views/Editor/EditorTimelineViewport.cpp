#include "EditorTimelineViewport.h"
#include <QHash>
#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace {
constexpr double Overscan = 64;
constexpr int Maximum = std::numeric_limits<int>::max();
int integer(double value) { return int(std::clamp(value, 0.0, double(Maximum))); }
struct Span {
    QVariantMap data;
    int first = 0, last = 0, order = 0;
};
struct Row {
    QVariantMap data;
    int top = 0, height = 0;
    std::vector<Span> clips;
    std::vector<int> ends;
    std::vector<QVariantMap> keys, motion;
};
std::vector<QVariantMap> sortedKeys(const QVariantList &input) {
    std::vector<QVariantMap> result; result.reserve(input.size());
    for(const auto &value:input) result.push_back(value.toMap());
    std::stable_sort(result.begin(), result.end(), [](const auto &a,const auto &b) {
        return a.value("frame").toInt()<b.value("frame").toInt();
    });
    return result;
}
QVariantList visibleKeys(const std::vector<QVariantMap> &keys,int first,int last,bool neighbours,const QVariantMap &preview={}) {
    if(first>last || keys.empty())return {};
    auto begin=std::lower_bound(keys.begin(),keys.end(),first,[](const auto &key,int f){return key.value("frame").toInt()<f;});
    auto end=std::upper_bound(keys.begin(),keys.end(),last,[](int f,const auto &key){return f<key.value("frame").toInt();});
    if(preview.isEmpty()) {
        if(neighbours){if(begin!=keys.begin())--begin;if(end!=keys.end())++end;}
        QVariantList result;result.reserve(std::distance(begin,end));
        for(auto it=begin;it!=end;++it)result.append(*it);
        return result;
    }
    const auto replaced=[&](const auto &key){return !preview.isEmpty()&&key.value("id")==preview.value("id");};
    QVariantList result;result.reserve(std::distance(begin,end)+3);
    for(auto it=begin;it!=end;++it)if(!replaced(*it))result.append(*it);
    QVariantMap before,after;
    if(neighbours) {
        while(begin!=keys.begin()){--begin;if(!replaced(*begin)){before=*begin;break;}}
        while(end!=keys.end()){if(!replaced(*end)){after=*end;break;}++end;}
    }
    if(!preview.isEmpty()) {
        const int frame=preview.value("frame").toInt();
        if(frame>=first&&frame<=last)result.append(preview);
        else if(neighbours&&frame<first&&(before.isEmpty()||frame>=before.value("frame").toInt()))before=preview;
        else if(neighbours&&frame>last&&(after.isEmpty()||frame<=after.value("frame").toInt()))after=preview;
    }
    if(!before.isEmpty())result.append(before);
    if(!after.isEmpty())result.append(after);
    std::stable_sort(result.begin(),result.end(),[](const auto &a,const auto &b){return a.toMap().value("frame").toInt()<b.toMap().value("frame").toInt();});
    return result;
}
}
class EditorTimelineViewport::Private {
public:
    int frames=1,rulerHeight=44;
    double fps=24,width=0,height=0,vertical=0,offset=0,scale=1;
    QVariantList layout,visible,major,minor;
    QVariantMap preview,keyPreview;
    std::vector<Row> rows;
    std::vector<int> rowEnds;
    QHash<QString,std::pair<int,int>> byId;
    QHash<QString,std::pair<int,int>> byKey;
    QHash<QString,int> byTrack;
    QHash<int,std::vector<Span>> overlays;

    void indexPreview() {
        overlays.clear();
        for(auto it=preview.cbegin();it!=preview.cend();++it) {
            const auto found=byId.constFind(it.key());if(found==byId.cend())continue;
            const auto [row,clip]=*found;auto span=rows[row].clips[clip];const auto value=it.value().toMap();
            span.first=value.value("in").toInt();span.last=value.value("out").toInt();
            if(span.first>=0 && span.last>span.first)overlays[row].push_back(std::move(span));
        }
    }
    std::vector<Span> visibleClips(int index,int first,int last) const {
        const auto &row=rows[index];
        const auto start=std::upper_bound(row.ends.begin(),row.ends.end(),first);
        std::vector<Span> result;
        for(auto i=std::size_t(start-row.ends.begin());i<row.clips.size() && row.clips[i].first<last;++i) {
            const auto &clip=row.clips[i];
            if(clip.last>first && !preview.contains(clip.data.value("id").toString()))result.push_back(clip);
        }
        const auto changed=overlays.constFind(index);
        if(changed!=overlays.cend())for(const auto &clip:*changed)
            if(clip.first<last && clip.last>first)result.push_back(clip);
        std::stable_sort(result.begin(),result.end(),[](const auto &a,const auto &b){return a.order<b.order;});
        return result;
    }
};

EditorTimelineViewport::EditorTimelineViewport(QObject *parent):QObject(parent),d(std::make_unique<Private>()) {}
EditorTimelineViewport::~EditorTimelineViewport()=default;
int EditorTimelineViewport::frameCount() const {return d->frames;}
double EditorTimelineViewport::frameRate() const {return d->fps;}
double EditorTimelineViewport::viewportWidth() const {return d->width;}
double EditorTimelineViewport::viewportHeight() const {return d->height;}
double EditorTimelineViewport::verticalOffset() const {return d->vertical;}
int EditorTimelineViewport::rulerHeight() const {return d->rulerHeight;}
double EditorTimelineViewport::contentX() const {return d->offset;}
double EditorTimelineViewport::zoom() const {return d->scale;}
double EditorTimelineViewport::contentWidth() const {return std::max(1.0,d->width*d->scale);}
double EditorTimelineViewport::pixelsPerFrame() const {return contentWidth()/viewFrameCount();}
int EditorTimelineViewport::viewFrameCount() const {return std::max({3,d->frames,integer(std::ceil(d->fps))});}
int EditorTimelineViewport::firstVisibleFrame() const {return std::min(viewFrameCount()-1,integer(std::floor(d->offset/pixelsPerFrame())));}
int EditorTimelineViewport::lastVisibleFrame() const {return std::max(firstVisibleFrame(),std::min(viewFrameCount()-1,integer(std::ceil((d->offset+d->width)/pixelsPerFrame())-1)));}
int EditorTimelineViewport::majorStep() const {
    const auto needed=std::max(1.0,76/pixelsPerFrame()),power=std::pow(10.0,std::floor(std::log10(needed)));
    for(const auto factor:{1,2,5,10})if(factor*power>=needed)return std::max(1,integer(factor*power));
    return Maximum;
}
int EditorTimelineViewport::minorStep() const {const auto step=majorStep();return step%5==0?step/5:step%2==0?step/2:step;}
int EditorTimelineViewport::snapTolerance() const {return std::min(Maximum/4,integer(std::floor(9/pixelsPerFrame())));}
QVariantList EditorTimelineViewport::majorTicks() const {return d->major;}
QVariantList EditorTimelineViewport::minorTicks() const {return d->minor;}
QVariantList EditorTimelineViewport::trackLayout() const {return d->layout;}
QVariantMap EditorTimelineViewport::dragPreview() const {return d->preview;}
QVariantMap EditorTimelineViewport::keyDragPreview() const {return d->keyPreview;}
QVariantList EditorTimelineViewport::visibleTracks() const {return d->visible;}

void EditorTimelineViewport::setFrameCount(int value) {value=std::max(1,value);if(d->frames==value)return;d->frames=value;updatePresentation();emit geometryChanged();}
void EditorTimelineViewport::setFrameRate(double value) {if(!std::isfinite(value)||value<=0||value>100000||d->fps==value)return;d->fps=value;updatePresentation();emit geometryChanged();}
void EditorTimelineViewport::setViewportWidth(double value) {if(!std::isfinite(value))return;value=std::clamp(value,0.0,100000.0);if(d->width==value)return;d->width=value;d->offset=boundedScroll(d->offset);updatePresentation();emit geometryChanged();}
void EditorTimelineViewport::setViewportHeight(double value) {if(!std::isfinite(value))return;value=std::clamp(value,0.0,100000.0);if(d->height==value)return;d->height=value;updatePresentation();emit geometryChanged();}
void EditorTimelineViewport::setVerticalOffset(double value) {if(!std::isfinite(value))return;value=std::clamp(value,-100000.0,double(Maximum));if(d->vertical==value)return;d->vertical=value;updatePresentation();emit geometryChanged();}
void EditorTimelineViewport::setRulerHeight(int value) {if(value<16||value>96||value==d->rulerHeight)return;d->rulerHeight=value;emit geometryChanged();}
void EditorTimelineViewport::setContentX(double value) {if(!std::isfinite(value))return;value=boundedScroll(value);if(d->offset==value)return;d->offset=value;updatePresentation();emit geometryChanged();}
void EditorTimelineViewport::setZoom(double value) {if(!std::isfinite(value)||value<=0)return;value=std::clamp(value,1.0,128.0);if(d->scale==value)return;d->scale=value;d->offset=boundedScroll(d->offset);updatePresentation();emit geometryChanged();}
void EditorTimelineViewport::setTrackLayout(const QVariantList &value) {
    if(d->layout==value)return;
    d->layout=value;d->rows.clear();d->byId.clear();d->byKey.clear();d->byTrack.clear();d->rowEnds.clear();
    for(const auto &entry:value) {
        Row row;row.data=entry.toMap();row.top=row.data.value("top").toInt();row.height=row.data.value("height").toInt();
        if(row.top<0 || row.height<=0 || row.height>Maximum-row.top)continue;
        const auto clips=row.data.value("clips").toList();row.clips.reserve(clips.size());
        for(const auto &clip:clips) {
            Span span;span.data=clip.toMap();span.first=span.data.value("in").toInt();span.last=span.data.value("out").toInt();span.order=int(row.clips.size());
            if(span.first>=0 && span.last>span.first)row.clips.push_back(std::move(span));
        }
        std::stable_sort(row.clips.begin(),row.clips.end(),[](const auto &a,const auto &b){return a.first<b.first;});
        int end=0;for(const auto &clip:row.clips){end=std::max(end,clip.last);row.ends.push_back(end);}
        row.keys=sortedKeys(row.data.value("keys").toList());row.motion=sortedKeys(row.data.value("motionKeys").toList());
        row.data.remove("clips");row.data.remove("keys");row.data.remove("motionKeys");
        d->rows.push_back(std::move(row));
    }
    std::stable_sort(d->rows.begin(),d->rows.end(),[](const auto &a,const auto &b){return a.top<b.top;});
    int end=0;
    for(std::size_t r=0;r<d->rows.size();++r) {
        const auto &row=d->rows[r];end=std::max(end,row.top+row.height);d->rowEnds.push_back(end);
        d->byTrack.insert(row.data.value("id").toString(),int(r));
        for(std::size_t c=0;c<row.clips.size();++c)d->byId.insert(row.clips[c].data.value("id").toString(),{int(r),int(c)});
        for(std::size_t k=0;k<row.keys.size();++k)d->byKey.insert(row.keys[k].value("id").toString(),{int(r),int(k)});
    }
    d->indexPreview();updatePresentation();emit trackLayoutChanged();
}
void EditorTimelineViewport::setDragPreview(const QVariantMap &value) {
    if(d->preview==value)return;d->preview=value;d->indexPreview();updatePresentation();emit dragPreviewChanged();
}
void EditorTimelineViewport::setKeyDragPreview(const QVariantMap &value) {
    if(d->keyPreview==value)return;d->keyPreview=value;updatePresentation();emit dragPreviewChanged();
}
int EditorTimelineViewport::frameAt(double position) const {
    return std::isfinite(position)?std::min(viewFrameCount()-1,integer(std::floor(position/pixelsPerFrame()+1e-9))):firstVisibleFrame();
}
QString EditorTimelineViewport::frameLabel(int frame) const {return "F"+QString::number(std::max(0,frame)).rightJustified(3,'0');}
double EditorTimelineViewport::boundedScroll(double value) const {
    return std::isfinite(value)?std::clamp(value,0.0,std::max(0.0,contentWidth()-d->width)):d->offset;
}
double EditorTimelineViewport::zoomAt(double factor,double anchor) {
    if(!std::isfinite(factor)||factor<=0||!std::isfinite(anchor))return d->offset;
    anchor=std::clamp(anchor,0.0,d->width);const auto frame=(d->offset+anchor)/pixelsPerFrame();
    setZoom(std::clamp(d->scale*factor,1.0,128.0));
    return boundedScroll(frame*pixelsPerFrame()-anchor);
}
double EditorTimelineViewport::edgeScroll(double pointer,double elapsed) const {
    if(!std::isfinite(pointer)||!std::isfinite(elapsed)||d->width<=0)return d->offset;
    const auto zone=std::clamp(d->width/10,16.0,48.0);
    const auto direction=pointer<zone?-std::clamp((zone-pointer)/zone,0.0,1.0)
        :pointer>d->width-zone?std::clamp((pointer-(d->width-zone))/zone,0.0,1.0):0.0;
    return boundedScroll(d->offset+direction*std::abs(direction)*720*std::clamp(elapsed,0.0,100.0)/1000);
}
void EditorTimelineViewport::updatePresentation() {
    const auto ppf=pixelsPerFrame();const int step=majorStep(),minor=minorStep();
    const auto begin=std::min(viewFrameCount()-1,integer(std::floor(std::max(0.0,d->offset-Overscan)/ppf)));
    const auto end=std::max(begin,std::min(viewFrameCount()-1,integer(std::ceil((d->offset+d->width+Overscan)/ppf))));
    const auto ticks=[&](int spacing,bool majors) {
        QVariantList result;
        for(qint64 frame=qint64(begin/spacing)*spacing;frame<=end && result.size()<8192;frame+=spacing) {
            if(!majors && frame%step==0)continue;
            result.append(QVariantMap{{"frame",int(frame)},{"x",frame*ppf}});
        }
        return result;
    };
    auto major=ticks(step,true),small=ticks(minor,false);
    if(major!=d->major || small!=d->minor){d->major=std::move(major);d->minor=std::move(small);emit rulerChanged();}
    QVariantList visible;
    const auto lower=d->vertical-Overscan,upper=d->vertical+d->height+Overscan;
    const auto first=std::upper_bound(d->rowEnds.begin(),d->rowEnds.end(),lower);
    for(auto r=std::size_t(first-d->rowEnds.begin());r<d->rows.size() && d->rows[r].top<=upper && visible.size()<512;++r) {
        const auto &row=d->rows[r];auto projected=row.data;QVariantList clips;
        const auto shown=d->visibleClips(int(r),begin,std::min(Maximum,end+1));clips.reserve(shown.size());
        for(const auto &clip:shown)clips.append(clip.data);
        projected["clips"]=clips;
        const auto keyPreview=d->keyPreview.value("trackId")==row.data.value("id")?d->keyPreview:QVariantMap{};
        auto keys=visibleKeys(row.keys,begin,end,false,keyPreview);
        auto motion=visibleKeys(row.motion,begin,end,true,keyPreview.value("kind")=="Motion"?keyPreview:QVariantMap{});
        // Membership drives delegate creation. Positions are a separate overlay,
        // just like clip previews, so a pointer move does not recreate the rows.
        const auto location=d->byKey.constFind(keyPreview.value("id").toString());
        if(location!=d->byKey.cend()) {
            const auto [keyRow,keyIndex]=*location;const auto &original=d->rows[keyRow].keys[keyIndex];
            for(auto *list:{&keys,&motion}) {
                for(auto &entry:*list)if(entry.toMap().value("id")==original.value("id"))entry=original;
                std::stable_sort(list->begin(),list->end(),[](const auto &a,const auto &b){return a.toMap().value("frame").toInt()<b.toMap().value("frame").toInt();});
            }
        }
        projected["keys"]=std::move(keys);
        projected["motionKeys"]=std::move(motion);
        visible.append(projected);
    }
    if(visible!=d->visible){d->visible=std::move(visible);emit visibleTracksChanged();}
}
QVariantList EditorTimelineViewport::curveKeys(const QString &id) const {
    const auto location=d->byTrack.constFind(id);if(location==d->byTrack.cend())return {};
    const auto &row=d->rows[*location];
    const auto ppf=pixelsPerFrame();
    const int begin=std::min(viewFrameCount()-1,integer(std::floor(std::max(0.0,d->offset-Overscan)/ppf)));
    const int end=std::max(begin,std::min(viewFrameCount()-1,integer(std::ceil((d->offset+d->width+Overscan)/ppf))));
    return visibleKeys(row.motion,begin,end,true,d->keyPreview.value("trackId")==id&&d->keyPreview.value("kind")=="Motion"?d->keyPreview:QVariantMap{});
}
QVariantMap EditorTimelineViewport::hitTest(double position,double y) const {
    if(!std::isfinite(position)||!std::isfinite(y))return {};
    if(y>=0 && y<d->rulerHeight)return {{"kind","ruler"},{"frame",frameAt(position)}};
    const auto row=std::upper_bound(d->rows.begin(),d->rows.end(),y,[](double value,const Row &r){return value<r.top;});
    if(row==d->rows.begin())return {};
    const auto index=std::size_t(row-d->rows.begin()-1);const auto &track=d->rows[index];
    if(y<track.top || y>=track.top+track.height)return {};
    const auto ppf=pixelsPerFrame();
    const auto frame=frameAt(position),pad=std::max(1,integer(std::ceil(8/ppf)));
    const auto begin=std::max(0,frame-pad),end=integer(std::min(double(Maximum),double(frame)+pad+1));
    const auto keyPreview=d->keyPreview.value("trackId")==track.data.value("id")?d->keyPreview:QVariantMap{};
    const auto keys=visibleKeys(track.keys,begin,end,false,keyPreview);
    for(const auto &entry:keys){const auto key=entry.toMap();
        if(std::abs(position-key.value("frame").toInt()*ppf)<=7 && std::abs(y-(track.top+4.0))<=7)
            return {{"kind","key"},{"id",key.value("id")}};
    }
    const auto spans=d->visibleClips(int(index),begin,end);
    if(y>=track.top+4 && y<=track.top+(track.data.value("type")=="Audio"?44:36))
        for(auto it=spans.rbegin();it!=spans.rend();++it) {
            const auto left=it->first*ppf,right=std::max(left+1,it->last*ppf-4);
            if(position<left || position>right)continue;
            auto result=it->data;result["kind"]="clip";
            result["trackId"]=track.data.value("id");
            const bool locked=result.value("locked").toBool();
            result["edge"]=locked?QString{}:position-left<=6?QStringLiteral("in"):right-position<=6?QStringLiteral("out"):QString{};
            return result;
        }
    return {{"kind","empty"},{"trackId",track.data.value("id")}};
}
