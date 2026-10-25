#include "PublishReview.h"
#include "../Views/Editor/EditorFrameExporter.h"
#include <iiFileProviderStorage.h>
#include <QUuid>
#include <QtConcurrentRun>
#include <algorithm>
#include <filesystem>

using namespace iiSocialPublish;
namespace {
QString q(std::string_view s) { return QString::fromUtf8(s.data(),qsizetype(s.size())); }
std::string s(const QVariant &v) { return v.toString().toStdString(); }
std::vector<std::string> tags(const QVariant &v) {
    std::vector<std::string> out;
    for (const auto &tag : v.toString().split(',',Qt::SkipEmptyParts)) if (!tag.trimmed().isEmpty()) out.push_back(tag.trimmed().toStdString());
    return out;
}
void append(std::string &text,const QVariant &value) { if (!value.toString().isEmpty()) { if (!text.empty()) text+='\n'; text+=s(value); } }
}
PublishReview::PublishReview(QObject *parent):QObject(parent) {
    connect(&m_export,&QFutureWatcher<QVariantMap>::finished,this,[this] {
        const QPointer<PublishReview> owner(this);
        auto result=m_export.result(); m_error=result.take("error").toString();
        if (m_exportDraft && m_exportCanvas && m_exportDraft->project() &&
            m_exportDraft->project()->currentCanvas()==m_exportCanvas && m_exportCanvas->revision()==m_exportRevision) {
            m_exportDraft->editDestinationSettings(m_exportPlatform,result);
        } else m_error=tr("The canvas changed during export. Export again for the current content.");
        if (!owner) return;
        m_exporting=false; rebuild();
    });
}
PublishReview::~PublishReview() { disconnect(&m_export,nullptr,this,nullptr); }
void PublishReview::setDraft(PublishDraft *draft) {
    if (m_draft==draft) return;
    if (m_draft) disconnect(m_draft,nullptr,this,nullptr);
    m_draft=draft;
    if (draft) connect(draft,&PublishDraft::changed,this,&PublishReview::rebuild);
    rebuild();
}
void PublishReview::setPlatform(const QString &id) {
    if (m_platform==id || !iiSocialPublish::platform(id.toStdString())) return;
    m_platform=id; m_error.clear(); rebuild();
}
bool PublishReview::edit(const QString &key,const QVariant &value) {
    if (!m_draft) return false;
    bool valid=false;
    for (const auto &entry : m_view.value("fields").toList()) {
        auto field=entry.toMap(); if (field.value("key")!=key) continue;
        const auto type=field.value("type").toString();
        valid=type=="bool" ? value.metaType()==QMetaType::fromType<bool>() : value.metaType()==QMetaType::fromType<QString>();
        if (valid && type=="choice") valid=field.value("options").toStringList().contains(value.toString());
    }
    // Only existing canvas references may be edited; no arbitrary settings/token keys.
    if (key.startsWith("media.")) for (int i=0;i<m_draft->count();++i) {
        const auto prefix="media."+m_draft->canvas(i).value("id").toString()+'.';
        if (key==prefix+"localFile" || key==prefix+"publicUrl") valid=value.metaType()==QMetaType::fromType<QString>();
        if (key==prefix+"kind") valid=value.toString()=="image" || value.toString()=="video";
    }
    if (!valid) return false;
    return m_draft->editDestination(m_platform,key,value);
}
bool PublishReview::reset(const QString &key) { return m_draft && m_draft->resetDestination(m_platform,key); }
void PublishReview::rebuild() {
    if (!m_draft) { m_view.clear(); emit changed(); return; }
    const auto selected=iiSocialPublish::platform(m_platform.toStdString());
    if (!selected) return;
    const auto common=m_draft->values(), overrides=m_draft->destinationSettings(m_platform);
    QVariantList fields;
    QVariantMap values;
    auto field=[&](QString key,QString label,QVariant inherited,QString type="text",QStringList choices={},bool shared=true) {
        const auto value=overrides.value(key,inherited); values[key]=value;
        fields.append(QVariantMap{{"key",key},{"label",label},{"value",value},{"type",type},{"options",choices},
            {"inherited",!overrides.contains(key)},{"shared",shared}});
        return value;
    };
    ReviewSource source;
    source.post.title=s(field("title",tr("Title"),common.value("title")));
    std::string body=s(common.value("description")); append(body,common.value("credits")); append(body,common.value("mentions"));
    source.post.text=s(field("text",tr("Description / introduction"),q(body)));
    source.post.tags=tags(field("tags",tr("Tags"),common.value("tags")));
    source.post.link=s(field("link",tr("Source / destination link"),common.value("linkedSource")));
    const auto visibility=*selected==Platform::Mastodon ? QStringList{"public","unlisted","private","direct"} :
        *selected==Platform::YouTube ? QStringList{"public","unlisted","private"} : QStringList{"public"};
    source.post.visibility=s(field("visibility",tr("Visibility"),common.value("visibility","public"),"choice",visibility));
    source.post.madeWithAi=common.value("madeWithAi").toBool();
    if (*selected==Platform::X) source.post.madeWithAi=field("madeWithAi",tr("AI disclosure"),source.post.madeWithAi,"bool").toBool();
    source.post.sensitive=common.value("adult").toBool() || common.value("violence").toBool();
    source.post.language=s(common.value("language"));
    if (*selected==Platform::Mastodon) {
        source.post.sensitive=field("sensitive",tr("Sensitive media"),source.post.sensitive,"bool").toBool();
        QString cw=common.value("contentWarning").toString();
        if (cw.isEmpty()) { if (common.value("adult").toBool()) cw="Adult content"; if (common.value("violence").toBool()) cw+=(cw.isEmpty()?"":" · ")+QString("Graphic violence"); }
        source.post.contentWarning=s(field("contentWarning",tr("Content warning"),cw));
        source.post.language=s(field("language",tr("Language (BCP 47)"),common.value("language")));
    }
    if (*selected==Platform::X || *selected==Platform::Threads) {
        QString inherited="everyone";
        if (common.value("comments")=="Followers") inherited="followers (unsupported)";
        if (common.value("comments")=="Nobody") inherited="nobody (unsupported)";
        const auto choices=*selected==Platform::X ? QStringList{"everyone","following","mentionedUsers","subscribers","verified"} : QStringList{"everyone","accounts_you_follow","mentioned_only"};
        source.post.replyControl=s(field("replyControl",tr("Who can reply"),inherited,"choice",choices));
        if ((*selected==Platform::X && source.post.replyControl=="everyone") || source.post.replyControl.ends_with(" (unsupported)")) source.post.replyControl.clear();
    }
    if (*selected==Platform::Pinterest || *selected==Platform::Facebook || *selected==Platform::LinkedIn || *selected==Platform::Tumblr)
        field("destinationId",*selected==Platform::Pinterest ? tr("Board ID") : *selected==Platform::Facebook ? tr("Page ID") : *selected==Platform::Tumblr ? tr("Blog hostname / UUID") : tr("Author URN"),"","text",{},false);
    if (*selected==Platform::Pinterest) source.post.coverImageUrl=s(field("coverImageUrl",tr("Video cover HTTPS URL"),"","text",{},false));
    if (*selected==Platform::YouTube) {
        source.post.youtube.categoryId=s(field("categoryId",tr("Video category ID"),"22","text",{},false));
        auto kids=field("madeForKids",tr("Made for kids"),"Channel default","choice",{"Channel default","Yes","No"},false).toString();
        if (kids!="Channel default") source.post.youtube.madeForKids=kids=="Yes";
        source.post.youtube.containsSyntheticMedia=field("syntheticMedia",tr("Altered / synthetic media"),common.value("madeWithAi"),"bool").toBool();
        source.post.youtube.notifySubscribers=field("notifySubscribers",tr("Notify subscribers"),true,"bool",{},false).toBool();
    }
    const auto policy=reviewPolicy(*selected);
    auto mode=field("sequence",tr("Publish structure"),"Automatic","choice",policy.threads ? QStringList{"Automatic","Single post","Separate posts","Thread"} : QStringList{"Automatic","Single post","Separate posts"},false).toString();
    ReviewOptions options;
    options.sequence=mode=="Single post" ? Sequence::Single : mode=="Separate posts" ? Sequence::Separate : mode=="Thread" ? Sequence::Thread : Sequence::Automatic;
    QStringList counts{"Automatic"};
    for (unsigned i=1;i<=std::min(20u,policy.mediaPerPost);++i) counts.append(QString::number(i));
    options.mediaPerPost=field("mediaPerPost",tr("Attachments per post"),"Automatic","choice",counts,false).toString().toUInt();
    options.includeCaptions=field("includeCaptions",tr("Include canvas captions"),true,"bool",{},false).toBool();
    options.numberParts=field("numberParts",tr("Number the parts"),true,"bool",{},false).toBool();
    options.repeatIntroduction=field("repeatIntroduction",tr("Repeat introduction"),*selected==Platform::Pinterest,"bool",{},false).toBool();
    if (policy.textOnly) options.splitText=field("splitText",tr("Split long text without shortening"),true,"bool",{},false).toBool();
    source.scheduledAt=common.value("publishTime")=="Schedule" ? s(common.value("scheduledAt")) : "";
    QVariantList mediaRows;
    bool mediaReady=true;
    const auto cap=std::ranges::find(capabilities(),*selected,&Capability::platform);
    for (int i=0;i<m_draft->count();++i) {
        auto row=m_draft->canvas(i); const auto id=row.value("id").toString(); const auto prefix="media."+id+'.';
        auto kind=overrides.value(prefix+"kind","image").toString();
        auto local=overrides.value(prefix+"localFile").toString();
        auto url=overrides.value(prefix+"publicUrl").toString();
        const QUrl localUrl(local); if (localUrl.isLocalFile()) local=localUrl.toLocalFile();
        const auto name=(local.isEmpty() ? QUrl(url).path() : local).toLower();
        const auto mime=kind=="video" ? name.endsWith(".mov") ? "video/quicktime" : name.endsWith(".webm") ? "video/webm" : "video/mp4" :
            name.endsWith(".jpg") || name.endsWith(".jpeg") ? "image/jpeg" : name.endsWith(".gif") ? "image/gif" : name.endsWith(".webp") ? "image/webp" : "image/png";
        Media m; m.kind=kind=="video" ? MediaKind::Video : MediaKind::Image;
        m.localFile=std::filesystem::path(local.toStdU16String()); m.publicUrl=url.toStdString(); m.mimeType=mime; m.altText=s(row.value("altText"));
        source.post.media.push_back(std::move(m)); source.captions.push_back(s(row.value("caption")));
        bool ready=cap->requiresPublicMediaUrl ? QUrl(url).scheme()=="https" && !QUrl(url).host().isEmpty() : !local.isEmpty() || !url.isEmpty();
        if (*selected==Platform::X || *selected==Platform::Mastodon || *selected==Platform::LinkedIn || *selected==Platform::YouTube) ready=!local.isEmpty();
        mediaReady &= ready;
        row.insert("localFile",local); row.insert("publicUrl",url); row.insert("kind",kind); row.insert("ready",ready);
        mediaRows.append(row);
    }
    m_plan=planPublication(*selected,source,options);
    QVariantList steps,issues;
    for (std::size_t i=0;i<m_plan.posts.size();++i) {
        auto &step=m_plan.posts[i]; QVariantList indices;
        for (auto index : step.sourceMedia) indices.append(int(index));
        steps.append(QVariantMap{{"index",int(i)},{"text",q(step.post.text)},{"title",q(step.post.title)},
            {"media",indices},{"replyTo",step.replyTo ? int(*step.replyTo) : -1}});
    }
    for (auto &issue : m_plan.issues) issues.append(QVariantMap{{"code",q(issue.code)},{"message",q(issue.message)},{"blocking",issue.blocking}});
    if (!mediaReady) issues.append(QVariantMap{{"code","export"},{"message",tr("Export the current canvas frames or attach exported media. Meta destinations also need public HTTPS URLs.")},{"blocking",true}});
    if (common.value("publishTime")=="Schedule" && source.scheduledAt.empty()) issues.append(QVariantMap{{"code","schedule"},{"message",tr("Enter the shared scheduled time before publishing.")},{"blocking",true}});
    if (common.value("comments")=="Nobody") issues.append(QVariantMap{{"code","comments"},{"message",tr("The shared Nobody comment setting is not supported by this adapter; change the common setting before publishing.")},{"blocking",true}});
    if (common.value("comments")=="Followers" && (*selected==Platform::X || *selected==Platform::Threads) && !overrides.contains("replyControl"))
        issues.append(QVariantMap{{"code","comments"},{"message",tr("Followers cannot be mapped to accounts you follow. Choose an explicit destination reply setting.")},{"blocking",true}});
    if (*selected!=Platform::X && *selected!=Platform::Threads && common.value("comments")!="Everyone") issues.append(QVariantMap{{"code","comments"},{"message",tr("This adapter cannot preserve the shared comment restriction.")},{"blocking",true}});
    const bool ready=m_plan.ready() && std::ranges::none_of(issues,[](const QVariant &v) { return v.toMap().value("blocking").toBool(); });
    QVariantMap next={{"fields",fields},{"values",values},{"steps",steps},{"issues",issues},{"media",mediaRows},{"ready",ready},
        {"name",q(cap->name)},{"supported",cap->officialPublishingApi},{"documentation",q(cap->documentation)},
        {"summary",tr("%1 canvases → %2 posts").arg(m_draft->count()).arg(steps.size())},
        {"sharedSummary",tr("Society source · %1 · %2 · %3").arg(common.value("visibility").toString(),common.value("publishTime").toString(),common.value("license").toString())}};
    if (next!=m_view) m_intent.clear();
    m_view=std::move(next); emit changed();
}
bool PublishReview::exportFrames(const QUrl &folder) {
    if (exporting() || !m_draft || !m_draft->project() || !folder.isLocalFile()) return false;
    auto *canvas=m_draft->project()->currentCanvas();
    if (!canvas->documentReady()) return false;
    auto snapshot=canvas->editingFrameSnapshot();
    const auto *document=canvas->document();
    std::vector<bool> boards,layers;
    for (auto &board : document->artboards) boards.push_back(board.visible);
    for (auto &layer : document->layers) layers.push_back(iiSharedCanvas::layerProperties(layer).visible);
    QVariantList rows; for (int i=0;i<m_draft->count();++i) rows.append(m_draft->canvas(i));
    const auto destination=std::filesystem::path(folder.toLocalFile().toStdU16String())/("publish-"+QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString());
    m_exportDraft=m_draft; m_exportCanvas=canvas; m_exportRevision=canvas->revision(); m_exportPlatform=m_platform; m_error.clear();
    const bool jpeg=m_platform=="instagram";
    m_export.setFuture(QtConcurrent::run([snapshot=std::move(snapshot),boards=std::move(boards),layers=std::move(layers),rows,destination,jpeg]() mutable {
        QVariantMap result;
        try {
            if (!iiFileProvider::native::createDirectory(destination)) throw std::runtime_error("Cannot create the media export folder");
            auto restored=std::make_shared<iiSharedCanvas::Document>(*snapshot.document);
            for (std::size_t i=0;i<boards.size();++i) restored->artboards[i].visible=boards[i];
            for (std::size_t i=0;i<layers.size();++i) iiSharedCanvas::layerProperties(restored->layers[i]).visible=layers[i];
            snapshot.document=std::move(restored);
            for (int i=0;i<rows.size();++i) {
                auto row=rows[i].toMap(); auto id=row.value("id").toString();
                snapshot.artboardId=snapshot.document->artboards.empty() ? "" : id.toStdString();
                auto path=destination/(std::to_string(i+1)+(jpeg ? ".jpg" : ".png"));
                auto error=EditorFrameExporter::exportSnapshot({snapshot,QUrl::fromLocalFile(QString::fromStdU16String(path.u16string())),jpeg ? "JPEG" : "PNG",300});
                if (!error.isEmpty()) throw std::runtime_error(error.toStdString());
                result.insert("media."+id+".localFile",QString::fromStdU16String(path.u16string()));
                result.insert("media."+id+".kind",QString("image"));
            }
        } catch (const std::exception &e) { result.insert("error",QString::fromUtf8(e.what())); }
        return result;
    }));
    m_exporting=true;
    emit changed(); return true;
}
QString PublishReview::submit(SocialPublishingController *backend,const QString &accountId) {
    if (!backend || !m_view.value("ready").toBool() || exporting()) return {};
    auto plan=m_plan;
    if (m_intent.empty()) m_intent=QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
    for (std::size_t i=0;i<plan.posts.size();++i) plan.posts[i].post.idempotencyKey=m_intent+"/"+std::to_string(i+1);
    return backend->submitPlan(accountId,std::move(plan),m_view.value("values").toMap().value("destinationId").toString());
}
