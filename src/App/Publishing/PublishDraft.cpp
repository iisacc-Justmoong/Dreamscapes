#include "PublishDraft.h"
#include <iiFileProviderNative.h>
#include <FileCatalog.h>
#include <iiJson/Json.h>
#include <iiSharedCanvas/File/DocumentFile.h>
#include <iiSharedCanvas/Serialization/IiscCodec.h>
#include <iiSharedCanvas/Validation/Validation.h>
#include <iiSocialPublish/Publishing.h>
#include <QSet>
#include <algorithm>
#include <filesystem>
#include <stdexcept>

namespace {
using Value = ii::json::Value;
const ii::json::Limits Limits{2*1024*1024, 2*1024*1024, 65536, 20000, 8};
constexpr auto ValuesKey = "dreamscapes:publication:v1:values";
constexpr auto CanvasesKey = "dreamscapes:publication:v1:canvases";
constexpr auto CanvasHashKey = "dreamscapes:publication:v1:canvas-sha256";
constexpr auto DestinationPrefix = "publish.destination.";
QString destinationPrefix(const QString &platform) { return QString(DestinationPrefix)+platform+'.'; }
std::filesystem::path localPath(const QUrl &url) {
    if (!url.isLocalFile() || url.toLocalFile().contains(QChar::Null))
        throw std::runtime_error("Choose a local draft file.");
    const auto path = std::filesystem::absolute(std::filesystem::path(url.toLocalFile().toStdU16String())).lexically_normal();
    if (QString::fromStdU16String(path.extension().u16string()).compare(".iisc",Qt::CaseInsensitive) != 0)
        throw std::runtime_error("Choose a shared canvas (.iisc) file.");
    return path;
}
std::string utf8Path(const std::filesystem::path &path) {
    const auto value = path.u8string(); return {value.begin(),value.end()};
}
std::string canvasBytes(const iiSharedCanvas::Document &document) {
    const auto encoded = iiSharedCanvas::encodeIisc(document);
    if (!encoded.ok()) throw std::runtime_error(encoded.error.message);
    return {encoded.bytes.begin(),encoded.bytes.end()};
}
iiSharedCanvas::Document storedDocument(const std::filesystem::path &path) {
    iiSharedCanvas::Document document;
    const auto result = iiSharedCanvas::DocumentFile::readSnapshot(utf8Path(path),document);
    if (!result.ok()) throw std::runtime_error(result.message);
    return document;
}
std::string bodyBytes(iiSharedCanvas::Document document) {
    document.publicationMetadata.reset();
    document.formatVersion = {iiSharedCanvas::CurrentFormatMajor,iiSharedCanvas::CurrentFormatMinor};
    return canvasBytes(document);
}
bool boundedText(const QString &text) {
    return std::size_t(text.toUtf8().size()) <= iiSharedCanvas::SerializationLimits{}.maximumMetadataStringBytes;
}
const Value &member(const Value &value, std::string_view key) {
    const auto entry = value.find(key);
    if (!entry) throw std::runtime_error("The draft is missing a required field.");
    return entry->get();
}
template<class T> const T &exact(const Value &value) {
    const auto entry = value.get<T>();
    if (!entry) throw std::runtime_error("The draft contains an invalid field type.");
    return entry->get();
}
QString text(const Value &value) { return QString::fromStdString(exact<std::string>(value)); }
Value parse(const std::string &bytes) {
    const auto value = ii::json::parse(bytes,Limits);
    if (!value) throw std::runtime_error(value.error().message);
    return *value;
}
QVariantMap defaultValues(QString title, const QString &coverId) {
    if (title.endsWith(".iisc",Qt::CaseInsensitive)) title.chop(5);
    return {{"title",title},{"description",""},{"contentType","Artwork"},{"topic",""},
        {"software","Dreamscapes"},{"tags",""},{"license","All rights reserved"},
        {"adult",false},{"violence",false},{"childlike",false},{"realPeople",false},
        {"publishTime","Publish immediately"},{"scheduledAt",""},{"comments","Everyone"},
        {"reactionCounts",true},{"commentCounts",true},{"mentions",""},{"linkedSource",""},{"credits",""},{"coverId",coverId},
        {"language",""},{"visibility","public"},{"madeWithAi",false},{"contentWarning",""}};
}
std::optional<iiSharedCanvas::PublicationMetadata> legacyMetadata(
    const std::filesystem::path &path,const iiSharedCanvas::Document &document) {
    if (!std::filesystem::exists(std::filesystem::path(path.native()+std::filesystem::path(".iisacc.json").native()))) return {};
    const auto bytes=iiFileProvider::FileCatalog::classificationDocument(path);
    if (bytes.empty()) return {};
    const auto classification=parse(bytes);
    const auto &fields=member(member(classification,"metadata"),"fields");
    if (!fields.find(ValuesKey) && !fields.find(CanvasesKey) && !fields.find(CanvasHashKey)) return {};
    if (exact<std::string>(member(fields,CanvasHashKey))!=iiFileProvider::native::sha256(canvasBytes(document)))
        throw std::runtime_error("Legacy publication metadata belongs to another canvas revision.");
    const auto values=parse(exact<std::string>(member(fields,ValuesKey)));
    const auto blocks=parse(exact<std::string>(member(fields,CanvasesKey)));
    iiSharedCanvas::PublicationMetadata metadata;
    const bool root=document.artboards.empty();
    for (const auto &[key,value] : exact<Value::Object>(values)) {
        if (key=="title") metadata.title=exact<std::string>(value);
        else if (key=="description") metadata.description=exact<std::string>(value);
        else if (key=="coverId") metadata.coverArtboardId=root ? std::string{} : exact<std::string>(value);
        else if (const auto flag=value.get<bool>()) metadata.settings[key]=flag->get();
        else metadata.settings[key]=exact<std::string>(value);
    }
    for (const auto &block : exact<Value::Array>(blocks)) {
        auto id=exact<std::string>(member(block,"id"));
        if (root && id=="canvas") id.clear();
        metadata.canvases.push_back({id,exact<std::string>(member(block,"caption")),exact<std::string>(member(block,"altText"))});
    }
    auto checked=document; checked.publicationMetadata=metadata;
    checked.formatVersion={iiSharedCanvas::CurrentFormatMajor,iiSharedCanvas::CurrentFormatMinor};
    const auto result=iiSharedCanvas::validate(checked);
    if (!result.ok()) throw std::runtime_error(result.issues.front().message);
    return metadata;
}

}
QVariant PublishCanvasModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= rowCount()) return {};
    const auto &row = rows[index.row()];
    switch (role) {
    case CanvasId: return row.id; case CanvasName: return row.name;
    case SourceIndex: return row.page; case CanvasWidth: return row.width;
    case CanvasHeight: return row.height; case Caption: return row.caption;
    case AltText: return row.alt; default: return {};
    }
}
QHash<int, QByteArray> PublishCanvasModel::roleNames() const {
    return {{CanvasId,"canvasId"},{CanvasName,"canvasName"},{SourceIndex,"sourceIndex"},
        {CanvasWidth,"canvasWidth"},{CanvasHeight,"canvasHeight"},{Caption,"caption"},{AltText,"altText"}};
}
void PublishCanvasModel::replace(std::vector<Row> next) { beginResetModel(); rows = std::move(next); endResetModel(); }
PublishDraft::PublishDraft(QObject *parent) : QObject(parent), m_canvases(this) {}
bool PublishDraft::fail(const QString &error) { m_error = error; emit changed(); return false; }
void PublishDraft::edited() { m_dirty = true; m_error.clear(); emit changed(); }
void PublishDraft::begin(EditorProject *project) {
    if (!project || !project->currentCanvas()->documentReady()) return;
    m_project = project;
    if (m_source == project->currentCanvas()) {
        std::vector<PublishCanvasModel::Row> current;
        const auto boards = project->artboards();
        for (const auto &entry : boards) {
            const auto board = entry.toMap();
            current.push_back({board.value("id").toString(),board.value("name").toString(),{},{},
                int(current.size()),board.value("width").toInt(),board.value("height").toInt()});
        }
        if (current.empty()) current.push_back({"canvas",project->documentName(),{},{},0,int(m_source->editingWidth()),int(m_source->editingHeight())});
        std::vector<PublishCanvasModel::Row> next;
        for (const auto &previous : m_canvases.rows) {
            const auto found = std::ranges::find(current,previous.id,&PublishCanvasModel::Row::id);
            if (found != current.end()) { next.push_back(*found); next.back().caption=previous.caption; next.back().alt=previous.alt; }
        }
        for (const auto &row : current)
            if (std::ranges::none_of(next,[&](const auto &entry) { return entry.id==row.id; })) next.push_back(row);
        const bool altered = next.size()!=m_canvases.rows.size() || !std::equal(next.begin(),next.end(),m_canvases.rows.begin(),[](const auto &a,const auto &b) {
            return a.id==b.id && a.page==b.page && a.name==b.name && a.width==b.width && a.height==b.height;
        });
        m_sourceName=project->documentName();
        if (altered) { m_canvases.replace(std::move(next)); if (std::ranges::none_of(m_canvases.rows,[&](const auto &row) {return row.id==m_values.value("coverId").toString();})) m_values["coverId"]=m_canvases.rows.front().id; edited(); }
        else {
            // Re-read native metadata after undo or an editor-side update, but
            // retain pending input and an independently saved publication copy.
            if (!m_dirty && m_path==project->filePath() && m_metadata!=m_source->document()->publicationMetadata)
                if (!applyMetadata(m_source->document()->publicationMetadata)) return;
            emit changed();
        }
        return;
    }
    m_source = project->currentCanvas(); m_sourceName = project->documentName();
    m_path.clear(); m_error.clear(); m_savedCanvasHash.clear(); m_dirty = false;
    auto title = m_sourceName;
    if (title.endsWith(".iisc", Qt::CaseInsensitive)) title.chop(5);
    std::vector<PublishCanvasModel::Row> rows;
    const auto boards = project->artboards();
    for (const auto &entry : boards) {
        const auto board = entry.toMap();
        rows.push_back({board.value("id").toString(), board.value("name").toString(), {}, {},
            int(rows.size()), board.value("width").toInt(), board.value("height").toInt()});
    }
    if (rows.empty()) rows.push_back({"canvas", title, {}, {}, 0, int(m_source->editingWidth()), int(m_source->editingHeight())});
    m_values = defaultValues(title,rows.front().id);
    m_canvases.replace(std::move(rows));
    m_metadata.reset();
    if (m_source->document()->publicationMetadata && !applyMetadata(m_source->document()->publicationMetadata)) return;
    if (project->filePath().endsWith(".iisc",Qt::CaseInsensitive)) m_path=project->filePath();
    emit changed();
}
bool PublishDraft::edit(const QString &key, const QVariant &value) {
    if (!m_values.contains(key) || m_values[key].metaType() != value.metaType()) return false;
    if (value.metaType() == QMetaType::fromType<QString>() && !boundedText(value.toString())) return false;
    if (key == "coverId" && std::ranges::none_of(m_canvases.rows, [&](const auto &row) { return row.id == value.toString(); })) return false;
    if (m_values[key] == value) return true;
    m_values[key] = value; edited(); return true;
}
QVariantMap PublishDraft::destinationSettings(const QString &platform) const {
    QVariantMap out; const auto prefix=destinationPrefix(platform);
    for (auto i=m_values.cbegin();i!=m_values.cend();++i) if (i.key().startsWith(prefix)) out.insert(i.key().mid(prefix.size()),i.value());
    return out;
}
bool PublishDraft::editDestination(const QString &platform, const QString &key, const QVariant &value) {
    return editDestinationSettings(platform,{{key,value}});
}
bool PublishDraft::editDestinationSettings(const QString &platform, const QVariantMap &values) {
    if (!iiSocialPublish::platform(platform.toStdString())) return false;
    for (auto i=values.cbegin();i!=values.cend();++i) {
        if (i.key().isEmpty() || i.key().size()>256 || i.key().contains(QChar::Null)) return false;
        if (i.value().metaType()!=QMetaType::fromType<QString>() && i.value().metaType()!=QMetaType::fromType<bool>()) return false;
        if (i.value().metaType()==QMetaType::fromType<QString>() && !boundedText(i.value().toString())) return false;
    }
    const auto prefix=destinationPrefix(platform); bool changed=false;
    for (auto i=values.cbegin();i!=values.cend();++i) if (!m_values.contains(prefix+i.key()) || m_values[prefix+i.key()]!=i.value()) {
        m_values[prefix+i.key()]=i.value(); changed=true;
    }
    if (changed) edited(); return true;
}
bool PublishDraft::resetDestination(const QString &platform, const QString &key) {
    if (!iiSocialPublish::platform(platform.toStdString())) return false;
    const auto prefix=destinationPrefix(platform); bool removed=false;
    for (auto i=m_values.begin();i!=m_values.end();) {
        if (i.key().startsWith(prefix) && (key.isEmpty() || i.key()==prefix+key)) { i=m_values.erase(i); removed=true; }
        else ++i;
    }
    if (removed) edited();
    return true;
}
bool PublishDraft::editCanvas(const QString &id, const QString &key, const QString &value) {
    if ((key != "caption" && key != "altText") || !boundedText(value)) return false;
    for (int i = 0; i < count(); ++i) if (auto &row = m_canvases.rows[i]; row.id == id) {
        auto &target = key == "caption" ? row.caption : row.alt;
        if (target == value) return true;
        target = value;
        emit m_canvases.dataChanged(m_canvases.index(i), m_canvases.index(i), {key == "caption" ? PublishCanvasModel::Caption : PublishCanvasModel::AltText});
        edited(); return true;
    }
    return false;
}
bool PublishDraft::moveCanvas(int index, int direction) {
    if ((direction != -1 && direction != 1) || index < 0 || index >= count()) return false;
    const int destination = index + direction;
    if (destination < 0 || destination >= count()) return false;
    m_canvases.beginMoveRows({}, index, index, {}, direction > 0 ? destination + 1 : destination);
    std::swap(m_canvases.rows[index], m_canvases.rows[destination]);
    m_canvases.endMoveRows(); edited(); return true;
}
QVariantMap PublishDraft::canvas(int index) const {
    if (index < 0 || index >= count()) return {};
    const auto &row = m_canvases.rows[index];
    return {{"id",row.id},{"name",row.name},{"page",row.page},{"width",row.width},
        {"height",row.height},{"caption",row.caption},{"altText",row.alt}};
}
iiSharedCanvas::PublicationMetadata PublishDraft::metadataForSave() const {
    auto metadata=m_metadata.value_or(iiSharedCanvas::PublicationMetadata{});
    metadata.title=m_values.value("title").toString().toStdString();
    metadata.description=m_values.value("description").toString().toStdString();
    const bool root=m_source->document()->artboards.empty();
    metadata.coverArtboardId=root ? std::string{} : m_values.value("coverId").toString().toStdString();
    std::erase_if(metadata.settings,[](const auto &entry) { return entry.first.starts_with(DestinationPrefix); });
    for (auto i=m_values.cbegin();i!=m_values.cend();++i) {
        if (i.key()=="title" || i.key()=="description" || i.key()=="coverId") continue;
        if (i.value().metaType()==QMetaType::fromType<bool>()) metadata.settings[i.key().toStdString()]=i.value().toBool();
        else metadata.settings[i.key().toStdString()]=i.value().toString().toStdString();
    }
    metadata.canvases.clear();
    for (const auto &row : m_canvases.rows)
        metadata.canvases.push_back({root ? std::string{} : row.id.toStdString(),row.caption.toStdString(),row.alt.toStdString()});
    // Preserve a common root-canvas annotation even when this UI shows artboards.
    if (!root && m_metadata) for (std::size_t i=0;i<m_metadata->canvases.size();++i) {
        const auto &canvas=m_metadata->canvases[i];
        if (canvas.artboardId.empty())
            metadata.canvases.insert(metadata.canvases.begin()+std::min(i,metadata.canvases.size()),canvas);
    }
    return metadata;
}
bool PublishDraft::applyMetadata(const std::optional<iiSharedCanvas::PublicationMetadata> &metadata) {
    auto sourceRows=m_canvases.rows;
    std::ranges::sort(sourceRows,{},&PublishCanvasModel::Row::page);
    for (auto &row : sourceRows) { row.caption.clear(); row.alt.clear(); }
    if (sourceRows.empty()) return fail(tr("Open a canvas before loading publication metadata."));
    auto values=defaultValues(m_sourceName,sourceRows.front().id);
    std::vector<PublishCanvasModel::Row> rows;
    if (metadata) {
        values["title"]=QString::fromStdString(metadata->title);
        values["description"]=QString::fromStdString(metadata->description);
        for (const auto &[key,value] : metadata->settings) {
            const auto name=QString::fromStdString(key);
            if (name.startsWith(DestinationPrefix)) {
                values[name]=std::holds_alternative<bool>(value) ? QVariant(std::get<bool>(value)) : QVariant(QString::fromStdString(std::get<std::string>(value)));
                continue;
            }
            if (!values.contains(name) || name=="title" || name=="description" || name=="coverId") continue;
            const bool flag=values[name].metaType()==QMetaType::fromType<bool>();
            if (flag!=std::holds_alternative<bool>(value)) return fail(tr("Publication metadata contains an invalid setting type."));
            values[name]=flag ? QVariant(std::get<bool>(value)) : QVariant(QString::fromStdString(std::get<std::string>(value)));
        }
        QHash<QString,int> indices;
        for (int i=0;i<int(sourceRows.size());++i) indices.insert(sourceRows[i].id,i);
        QSet<QString> seen;
        const bool root=m_source->document()->artboards.empty();
        for (const auto &canvas : metadata->canvases) {
            if (canvas.artboardId.empty() && !root) continue;
            const auto id=canvas.artboardId.empty() ? QString("canvas") : QString::fromStdString(canvas.artboardId);
            if (!indices.contains(id) || seen.contains(id)) return fail(tr("Publication metadata references an unavailable canvas."));
            seen.insert(id); rows.push_back(sourceRows[indices.value(id)]);
            rows.back().caption=QString::fromStdString(canvas.caption);
            rows.back().alt=QString::fromStdString(canvas.alternativeText);
        }
        for (const auto &row : sourceRows) if (!seen.contains(row.id)) rows.push_back(row);
        if (!metadata->coverArtboardId.empty()) {
            const auto cover=QString::fromStdString(metadata->coverArtboardId);
            if (!indices.contains(cover)) return fail(tr("Choose an existing cover canvas."));
            values["coverId"]=cover;
        }
    } else rows=std::move(sourceRows);
    m_values=std::move(values); m_metadata=metadata; m_canvases.replace(std::move(rows)); return true;
}
bool PublishDraft::save(const QUrl &destination) {
    if (!m_project || !m_source || m_source!=m_project->currentCanvas() || !m_source->document())
        return fail(tr("Reopen the post editor for the current canvas before saving a draft."));
    try {
        const auto path=localPath(destination);
        const auto metadata=metadataForSave();
        const auto source=m_project->filePath();
        const bool exists=std::filesystem::exists(path);
        if (std::filesystem::is_symlink(std::filesystem::symlink_status(path)))
            return fail(tr("Choose a regular local canvas file."));
        bool original=false;
        if (!source.isEmpty()) {
            const auto sourcePath=localPath(QUrl::fromLocalFile(source));
            original=path==sourcePath || (exists && std::filesystem::exists(sourcePath) && std::filesystem::equivalent(path,sourcePath));
        }
        std::string savedCanvasHash;
        if (original) {
            // The editor validates and persists its transaction. Avoid another
            // complete media encode merely to hash a managed original file.
            if (!m_source->setPublicationMetadata(metadata)) return fail(m_source->error());
        } else {
            auto document=*m_source->document(); document.publicationMetadata=metadata;
            document.formatVersion={iiSharedCanvas::CurrentFormatMajor,iiSharedCanvas::CurrentFormatMinor};
            const auto bytes=canvasBytes(document); // Validate the complete copy before writing.
            if (exists) {
                const auto previous=storedDocument(path);
                const auto canonical=canvasBytes(previous);
                const bool known=!m_path.isEmpty() && path==localPath(QUrl::fromLocalFile(m_path)) && !m_savedCanvasHash.empty();
                if (known && iiFileProvider::native::sha256(canonical)!=m_savedCanvasHash)
                    return fail(tr("The publication canvas changed outside this session. Existing contents were preserved."));
                if (!known && bodyBytes(previous)!=bodyBytes(*m_source->document()))
                    return fail(tr("This file contains another canvas. Choose a new .iisc file."));
                if (canonical!=bytes) {
                    const auto physical=iiFileProvider::native::File::read(path,iiSharedCanvas::SerializationLimits{}.maximumContainerBytes);
                    if (physical!=canonical) return fail(tr("Open this working canvas in the editor before changing its publication metadata."));
                    iiFileProvider::native::File::update(path,physical,bytes);
                }
            } else iiFileProvider::native::File::create(path,bytes);
            savedCanvasHash=iiFileProvider::native::sha256(bytes);
        }
        m_path=QString::fromStdU16String(path.u16string()); m_savedCanvasHash=std::move(savedCanvasHash);
        m_metadata=metadata; m_dirty=false; m_error.clear(); emit changed(); return true;
    } catch (const std::exception &error) { return fail(QString::fromUtf8(error.what())); }
}
bool PublishDraft::load(const QUrl &source) {
    if (!m_project || !m_source || m_source!=m_project->currentCanvas() || !m_source->document())
        return fail(tr("Reopen the post editor for the current canvas before loading a draft."));
    try {
        const auto path=localPath(source); const auto document=storedDocument(path);
        if (bodyBytes(document)!=bodyBytes(*m_source->document()))
            return fail(tr("Open this .iisc canvas in the editor before editing its publication metadata."));
        auto metadata=document.publicationMetadata;
        if (!metadata) metadata=legacyMetadata(path,document);
        if (!applyMetadata(metadata)) return false;
        m_path=QString::fromStdU16String(path.u16string()); m_savedCanvasHash=iiFileProvider::native::sha256(canvasBytes(document));
        m_dirty=false; m_error.clear(); emit changed(); return true;
    } catch (const std::exception &error) { return fail(QString::fromUtf8(error.what())); }
}

int PublishDraft::coverPage() const {
    const auto id = m_values.value("coverId").toString();
    for (const auto &row : m_canvases.rows) if (row.id == id) return row.page; return 0;
}
QStringList PublishDraft::tags() const {
    QStringList result; for (const auto &value : m_values.value("tags").toString().split(',')) if (!value.trimmed().isEmpty()) result.push_back(value.trimmed()); return result;
}
