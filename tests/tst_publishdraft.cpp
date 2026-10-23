#include "../src/App/Publishing/PublishDraft.h"
#include "../src/App/Publishing/PublishReview.h"
#include <iiSharedCanvas/Serialization/IiscCodec.h>
#include <iiSharedCanvas/File/DocumentFile.h>
#include <iiFileProviderNative.h>
#include <FileCatalog.h>
#include <iiJson/Json.h>
#include <SocietyDrive.h>
#include <QImage>
#include <QTemporaryDir>
#include <QFile>
#include <QtTest>

namespace {
std::filesystem::path nativePath(const QString &path) { return std::filesystem::path(path.toStdU16String()); }
std::vector<std::uint8_t> body(iiSharedCanvas::Document document) {
    document.publicationMetadata.reset();
    document.formatVersion={iiSharedCanvas::CurrentFormatMajor,iiSharedCanvas::CurrentFormatMinor};
    return iiSharedCanvas::encodeIisc(document).bytes;
}
bool writeCanvas(const QString &path,const iiSharedCanvas::Document &document) {
    const auto encoded=iiSharedCanvas::encodeIisc(document);
    if (!encoded.ok()) return false;
    iiFileProvider::native::File::write(nativePath(path),std::string(encoded.bytes.begin(),encoded.bytes.end()));
    return true;
}
bool noCompanion(const QString &path) { return !QFile::exists(path+".iisacc.json"); }
}
class PublishDraftTests : public QObject {
    Q_OBJECT
    QTemporaryDir m_storage{DREAMSCAPES_TEST_DIRECTORY "/publication-documents-XXXXXX.societycontainer"};
private slots:
    void initTestCase() {
        QVERIFY(m_storage.isValid());
        QVERIFY(iiSocietyContainer::SocietyDrive::create(m_storage.path()));
        qputenv("SOCIETY_CONTAINER_PATH",m_storage.path().toUtf8());
    }
    void destinationReviewInheritsAndPersistsWithoutMutatingCanvas() {
        QTemporaryDir directory(DREAMSCAPES_TEST_DIRECTORY "/post-review-XXXXXX"); QVERIFY(directory.isValid());
        iiSharedCanvas::Document document; document.extent={64,32};
        for (int i=0;i<20;++i) document.artboards.push_back({i==19 ? "canvas" : "board-"+std::to_string(i),"Canvas "+std::to_string(i+1),{{0,0},{64,32}},0xff102030u+unsigned(i)*0x00040301u});
        iiSharedCanvas::PublicationMetadata metadata; metadata.title="Society title"; metadata.description="Description from Society";
        metadata.settings={{"tags",std::string("design,illustration")},{"adult",true},{"language",std::string("ko")},{"madeWithAi",true}};
        for (int i=0;i<20;++i) metadata.canvases.push_back({document.artboards[i].id,"Caption "+std::to_string(i+1),"Alt "+std::to_string(i+1)});
        document.publicationMetadata=metadata;
        EditorProject project; QVERIFY(project.currentCanvas()->loadProjectPage(document,"Source"));
        PublishDraft draft; draft.begin(&project); QCOMPARE(draft.count(),20);
        auto *canvas=project.currentCanvas(); const auto revision=canvas->revision();
        const auto bytes=iiSharedCanvas::encodeIisc(*canvas->document()).bytes;
        QSignalSpy fileChanges(&project,&EditorProject::projectChanged);
        PublishReview review; review.setDraft(&draft);
        auto view=review.view(); QCOMPARE(view.value("values").toMap().value("title").toString(),"Society title");
        QCOMPARE(view.value("steps").toList().size(),5);
        QCOMPARE(view.value("steps").toList()[1].toMap().value("replyTo").toInt(),0);
        QCOMPARE(view.value("media").toList()[19].toMap().value("altText").toString(),"Alt 20");
        QVERIFY(!draft.dirty());
        QVERIFY(review.edit("media.board-0.localFile",QString("fixture.gif")));
        QCOMPARE(review.view().value("steps").toList().size(),6); // GIF is a single attachment, not an inferred PNG.
        QVERIFY(review.reset("media.board-0.localFile"));
        QVERIFY(review.edit("text",QString("X-only introduction")));
        QVERIFY(draft.edit("description",QString("New Society description")));
        QCOMPARE(review.view().value("values").toMap().value("text").toString(),"X-only introduction");
        QCOMPARE(draft.values().value("description").toString(),"New Society description");
        review.setPlatform("linkedin");
        QCOMPARE(review.view().value("steps").toList().size(),1);
        QCOMPARE(review.view().value("values").toMap().value("text").toString(),"New Society description");
        review.setPlatform("x"); QVERIFY(review.reset("text"));
        QCOMPARE(review.view().value("values").toMap().value("text").toString(),"New Society description");
        QVERIFY(review.edit("sequence",QString("Separate posts")));
        for (const auto &step : review.view().value("steps").toList()) QCOMPARE(step.toMap().value("replyTo").toInt(),-1);
        QVERIFY(!review.edit("sequence",QString("Unsupported"))); QVERIFY(!review.edit("accessToken",QString("secret")));
        QVERIFY(!review.edit("media.unknown.localFile",QString("path")));
        const auto path=directory.filePath("review.iisc"); QVERIFY2(draft.save(QUrl::fromLocalFile(path)),qPrintable(draft.error()));
        QVERIFY(noCompanion(path));
        iiSharedCanvas::Document saved; QVERIFY(iiSharedCanvas::DocumentFile::readSnapshot(path.toStdString(),saved).ok());
        QCOMPARE(std::get<std::string>(saved.publicationMetadata->settings.at("publish.destination.x.sequence")),std::string("Separate posts"));
        PublishDraft restored; restored.begin(&project); QVERIFY(restored.load(QUrl::fromLocalFile(path)));
        QCOMPARE(restored.destinationSettings("x").value("sequence").toString(),"Separate posts");
        QCOMPARE(restored.values().value("title").toString(),"Society title");
        QVERIFY(restored.resetDestination("x"));
        QVERIFY(restored.save(QUrl::fromLocalFile(directory.filePath("reset.iisc"))));
        iiSharedCanvas::Document reset; QVERIFY(iiSharedCanvas::DocumentFile::readSnapshot(directory.filePath("reset.iisc").toStdString(),reset).ok());
        QVERIFY(!reset.publicationMetadata->settings.contains("publish.destination.x.sequence"));
        review.setPlatform("mastodon");
        QVERIFY(review.view().value("values").toMap().value("sensitive").toBool());
        QCOMPARE(review.view().value("values").toMap().value("language").toString(),"ko");
        QVERIFY(review.exportFrames(QUrl::fromLocalFile(directory.path())));
        QTRY_VERIFY_WITH_TIMEOUT(!review.exporting(),30000);
        QVERIFY2(review.error().isEmpty(),qPrintable(review.error()));
        const auto media=review.view().value("media").toList();
        QCOMPARE(media.size(),20);
        for (int i=0;i<media.size();++i) { QImage image(media[i].toMap().value("localFile").toString()); QVERIFY(!image.isNull()); QCOMPARE(image.size(),QSize(64,32)); QCOMPARE(image.pixelColor(32,16).rgba(),document.artboards[i].backgroundArgb); }
        QCOMPARE(canvas->revision(),revision); QCOMPARE(fileChanges.size(),0);
        QCOMPARE(iiSharedCanvas::encodeIisc(*canvas->document()).bytes,bytes);
    }
    void actualCanvasesAndDraftChangesStaySeparate() {
        QTemporaryDir directory(DREAMSCAPES_TEST_DIRECTORY "/post-source-XXXXXX"); QVERIFY(directory.isValid());
        QVariantList sources;
        for (int index=0;index<3;++index) {
            QImage image(64,32,QImage::Format_RGB32); image.fill(index==0?Qt::red:index==1?Qt::green:Qt::blue);
            const auto path=directory.filePath(QString::number(index)+".png"); QVERIFY(image.save(path)); sources.append(QUrl::fromLocalFile(path));
        }
        EditorProject project; QVERIFY(project.openImages(sources));
        const auto before=iiSharedCanvas::encodeIisc(*project.currentCanvas()->document()).bytes;
        const auto revision=project.currentCanvas()->revision();
        PublishDraft draft; draft.begin(&project); QCOMPARE(draft.count(),3);
        QCOMPARE(draft.canvas(0).value("width").toInt(),64);
        const auto first=draft.canvas(0).value("id").toString();
        QSignalSpy resets(draft.canvases(),&QAbstractItemModel::modelReset);
        QSignalSpy changes(draft.canvases(),&QAbstractItemModel::dataChanged);
        QSignalSpy documentChanges(&project,&EditorProject::projectChanged);
        QVERIFY(draft.edit("title",QString("Portfolio")));
        QVERIFY(draft.editCanvas(first,"caption","My caption"));
        QVERIFY(draft.editCanvas(first,"altText","A red canvas"));
        QVERIFY(draft.moveCanvas(0,1)); QCOMPARE(draft.canvas(1).value("id").toString(),first);
        QCOMPARE(draft.canvas(1).value("page").toInt(),0);
        QCOMPARE(resets.size(),0); QCOMPARE(changes.size(),2);
        QVERIFY(!draft.moveCanvas(0,-1)); QVERIFY(!draft.moveCanvas(0,2));
        QVERIFY(!draft.edit("adult",QString("true"))); QVERIFY(!draft.edit("unknown",true));
        QVERIFY(!draft.edit("coverId",QString("missing")));
        draft.begin(&project); QCOMPARE(draft.values().value("title").toString(),"Portfolio");
        const auto path=directory.filePath("multiple-canvases.iisc");
        QVERIFY2(draft.save(QUrl::fromLocalFile(path)),qPrintable(draft.error()));
        iiSharedCanvas::Document snapshot;
        QVERIFY(iiSharedCanvas::DocumentFile::readSnapshot(path.toStdString(),snapshot).ok());
        QCOMPARE(body(snapshot),body(*project.currentCanvas()->document()));
        QVERIFY(snapshot.publicationMetadata); QCOMPARE(snapshot.publicationMetadata->title,std::string("Portfolio"));
        QCOMPARE(snapshot.publicationMetadata->canvases[1].artboardId,first.toStdString());
        QCOMPARE(snapshot.publicationMetadata->canvases[1].caption,std::string("My caption"));
        QVERIFY(noCompanion(path));
        PublishDraft restored; restored.begin(&project); QVERIFY2(restored.load(QUrl::fromLocalFile(path)),qPrintable(restored.error()));
        QCOMPARE(restored.canvas(1).value("id").toString(),first);
        QCOMPARE(restored.canvas(1).value("page").toInt(),0);
        QCOMPARE(restored.canvas(1).value("caption").toString(),"My caption");
        // Another consumer may annotate the root alongside the artboards.
        // Preserve that hidden block and its publication position on save.
        snapshot.publicationMetadata->canvases.insert(snapshot.publicationMetadata->canvases.begin(),{"","Shared root caption","Shared root alternative"});
        QVERIFY(writeCanvas(path,snapshot));
        QVERIFY2(restored.load(QUrl::fromLocalFile(path)),qPrintable(restored.error()));
        QCOMPARE(restored.count(),3);
        const auto shared=directory.filePath("shared-root.iisc");
        QVERIFY2(restored.save(QUrl::fromLocalFile(shared)),qPrintable(restored.error()));
        iiSharedCanvas::Document common; QVERIFY(iiSharedCanvas::DocumentFile::readSnapshot(shared.toStdString(),common).ok());
        QVERIFY(common.publicationMetadata); QVERIFY(!common.publicationMetadata->canvases.empty());
        QVERIFY(common.publicationMetadata->canvases.front().artboardId.empty());
        QCOMPARE(common.publicationMetadata->canvases.front().caption,std::string("Shared root caption"));
        QCOMPARE(common.publicationMetadata->canvases.front().alternativeText,std::string("Shared root alternative"));
        QVERIFY(noCompanion(shared));
        QCOMPARE(project.currentCanvas()->revision(),revision); QCOMPARE(documentChanges.size(),0);
        QCOMPARE(iiSharedCanvas::encodeIisc(*project.currentCanvas()->document()).bytes,before);
    }
    void allFieldsRoundTripAndBadLoadsAreAtomic() {
        QTemporaryDir directory(DREAMSCAPES_TEST_DIRECTORY "/post-draft-XXXXXX"); QVERIFY(directory.isValid());
        EditorProject project; QVERIFY(project.createCanvas({{"width",64},{"height",32},{"unit","px"},{"background","White"}}));
        PublishDraft draft; draft.begin(&project);
        const auto id=draft.canvas(0).value("id").toString();
        const auto defaults=draft.values();
        for (auto i=defaults.cbegin();i!=defaults.cend();++i) {
            if(i.key()=="coverId")continue;
            const auto value=i.value().metaType()==QMetaType::fromType<bool>()?QVariant(!i.value().toBool()):QVariant(QString("한국어 ")+i.key());
            QVERIFY(draft.edit(i.key(),value));
        }
        QVERIFY(draft.editCanvas(id,"caption","Caption Ω")); QVERIFY(draft.editCanvas(id,"altText","Alt 한국어"));
        const auto expected=draft.values();
        const auto original=iiSharedCanvas::encodeIisc(*project.currentCanvas()->document()).bytes;
        const auto revision=project.currentCanvas()->revision();
        const auto path=directory.filePath("portfolio.iisc");
        QVERIFY2(draft.save(QUrl::fromLocalFile(path)),qPrintable(draft.error())); QVERIFY(!draft.dirty());
        iiSharedCanvas::Document snapshot;
        QVERIFY(iiSharedCanvas::DocumentFile::readSnapshot(path.toStdString(),snapshot).ok());
        QCOMPARE(body(snapshot),body(*project.currentCanvas()->document()));
        QVERIFY(snapshot.publicationMetadata);
        const auto &metadata=*snapshot.publicationMetadata;
        QCOMPARE(metadata.title,expected["title"].toString().toStdString());
        QCOMPARE(metadata.description,expected["description"].toString().toStdString());
        for(auto i=expected.cbegin();i!=expected.cend();++i) {
            if(i.key()=="title"||i.key()=="description"||i.key()=="coverId")continue;
            QVERIFY(metadata.settings.contains(i.key().toStdString()));
            if(i.value().metaType()==QMetaType::fromType<bool>()) QCOMPARE(std::get<bool>(metadata.settings.at(i.key().toStdString())),i.value().toBool());
            else QCOMPARE(std::get<std::string>(metadata.settings.at(i.key().toStdString())),i.value().toString().toStdString());
        }
        PublishDraft restored; restored.begin(&project); QVERIFY2(restored.load(QUrl::fromLocalFile(path)),qPrintable(restored.error()));
        QCOMPARE(restored.values(),expected); QCOMPARE(restored.canvas(0).value("caption").toString(),"Caption Ω");
        QCOMPARE(restored.canvas(0).value("altText").toString(),"Alt 한국어");
        QFile invalid(directory.filePath("broken.iisc")); QVERIFY(invalid.open(QIODevice::WriteOnly)); invalid.write("{\"format\":\"unsupported\"}"); invalid.close();
        QVERIFY(!restored.load(QUrl::fromLocalFile(invalid.fileName()))); QCOMPARE(restored.values(),expected);
        QCOMPARE(restored.canvas(0).value("caption").toString(),"Caption Ω");
        QVERIFY(!draft.save(QUrl::fromLocalFile(directory.filePath("source.iipost"))));
        QVERIFY(!QFile::exists(directory.filePath("source.iipost")));
        QVERIFY(!draft.save(QUrl("https://example.invalid/draft.iisc"))); QVERIFY(noCompanion(path));
        QCOMPARE(iiSharedCanvas::encodeIisc(*project.currentCanvas()->document()).bytes,original);
        QCOMPARE(project.currentCanvas()->revision(),revision);
        EditorProject different; QVERIFY(different.createCanvas({{"width",32},{"height",32},{"unit","px"},{"background","White"}}));
        draft.begin(&different); QVERIFY(draft.path().isEmpty()); QVERIFY(!draft.dirty()); QVERIFY(draft.values()!=expected);
    }
    void onlyTheIiscFileIsNeededAfterMovingAndReopening() {
        QTemporaryDir directory(DREAMSCAPES_TEST_DIRECTORY "/post-portable-XXXXXX"); QVERIFY(directory.isValid());
        EditorProject project; QVERIFY(project.createCanvas({{"width",64},{"height",32},{"unit","px"},{"background","White"}}));
        const auto original=*project.currentCanvas()->document();
        PublishDraft draft; draft.begin(&project); const auto defaults=draft.values();
        QVERIFY(draft.edit("title",QString("Portable publication")));
        QVERIFY(draft.editCanvas(draft.canvas(0)["id"].toString(),"caption","A self-contained canvas"));
        const auto source=directory.filePath("source.iisc");
        QVERIFY2(draft.save(QUrl::fromLocalFile(source)),qPrintable(draft.error()));
        const auto renamed=directory.filePath("renamed.iisc");
        QVERIFY(QFile::copy(source,renamed)); QVERIFY(QFile::remove(source));
        QVERIFY(noCompanion(source)); QVERIFY(noCompanion(renamed));
        iiSharedCanvas::Document snapshot;
        QVERIFY(iiSharedCanvas::DocumentFile::readSnapshot(renamed.toStdString(),snapshot).ok());
        EditorProject reopened; QVERIFY(reopened.currentCanvas()->loadProjectPage(snapshot,"renamed.iisc"));
        PublishDraft restored; restored.begin(&reopened);
        QCOMPARE(restored.values(),draft.values()); QCOMPARE(restored.canvas(0)["caption"].toString(),"A self-contained canvas");
        snapshot.publicationMetadata->settings["common.future.setting"]=std::string("preserve me");
        QVERIFY(reopened.currentCanvas()->loadProjectPage(snapshot,"renamed.iisc"));
        PublishDraft common; common.begin(&reopened); QVERIFY(common.edit("description",QString("Updated")));
        const auto next=directory.filePath("another.iisc"); QVERIFY2(common.save(QUrl::fromLocalFile(next)),qPrintable(common.error()));
        iiSharedCanvas::Document saved; QVERIFY(iiSharedCanvas::DocumentFile::readSnapshot(next.toStdString(),saved).ok());
        QCOMPARE(std::get<std::string>(saved.publicationMetadata->settings.at("common.future.setting")),std::string("preserve me"));
        const auto raw=directory.filePath("raw.iisc"); QVERIFY(writeCanvas(raw,original));
        PublishDraft unannotated; unannotated.begin(&project);
        QVERIFY2(unannotated.load(QUrl::fromLocalFile(raw)),qPrintable(unannotated.error())); QCOMPARE(unannotated.values(),defaults);
        EditorProject other; QVERIFY(other.createCanvas({{"width",32},{"height",32},{"unit","px"},{"background","White"}}));
        PublishDraft mismatch; mismatch.begin(&other); const auto prior=mismatch.values();
        QVERIFY(!mismatch.load(QUrl::fromLocalFile(renamed))); QCOMPARE(mismatch.values(),prior);
        QFile companion(renamed+".iisacc.json"); QVERIFY(companion.open(QIODevice::WriteOnly)); companion.write("{}"); companion.close();
        QVERIFY2(draft.load(QUrl::fromLocalFile(renamed)),qPrintable(draft.error()));
    }
    void originalWorkingFileStoresMetadataAndKeepsRevisionCoherent() {
        QTemporaryDir directory(DREAMSCAPES_TEST_DIRECTORY "/post-working-XXXXXX"); QVERIFY(directory.isValid());
        EditorProject project; QVERIFY(project.createCanvas({{"width",64},{"height",32},{"unit","px"},{"background","White"}}));
        const auto source=QUrl::fromLocalFile(directory.filePath("working.iisc"));
        QVERIFY(project.saveDocumentAs(source));
        const auto original=*project.currentCanvas()->document();
        const auto revision=project.currentCanvas()->revision();
        PublishDraft draft; draft.begin(&project); QCOMPARE(draft.path(),source.toLocalFile());
        QVERIFY(draft.edit("title",QString("Publication in document")));
        QVERIFY(draft.edit("description",QString(80*1024,QChar('x'))));
        QCOMPARE(project.currentCanvas()->revision(),revision);
        QVERIFY2(draft.save(source),qPrintable(draft.error()));
        QVERIFY(project.currentCanvas()->revision()>revision); QVERIFY(!project.modified());
        iiSharedCanvas::Document persisted;
        QVERIFY(iiSharedCanvas::DocumentFile::readSnapshot(source.toLocalFile().toStdString(),persisted).ok());
        QVERIFY(persisted.publicationMetadata); QCOMPARE(persisted.publicationMetadata->description.size(),std::size_t(80*1024));
        QCOMPARE(iiSharedCanvas::encodeIisc(persisted).bytes,iiSharedCanvas::encodeIisc(*project.currentCanvas()->document()).bytes);
        auto artwork=persisted; artwork.authorship=original.authorship;
        QCOMPARE(body(artwork),body(original)); QVERIFY(noCompanion(source.toLocalFile()));
        const auto savedRevision=project.currentCanvas()->revision();
        QVERIFY2(draft.save(source),qPrintable(draft.error())); QCOMPARE(project.currentCanvas()->revision(),savedRevision);
        const auto layer=project.currentCanvas()->selectedLayerId(); QVERIFY(!layer.isEmpty());
        QVERIFY(project.currentCanvas()->setLayerName(layer,"After metadata"));
        QVERIFY(iiSharedCanvas::DocumentFile::readSnapshot(source.toLocalFile().toStdString(),persisted).ok());
        QCOMPARE(iiSharedCanvas::encodeIisc(persisted).bytes,iiSharedCanvas::encodeIisc(*project.currentCanvas()->document()).bytes);
        QVERIFY(persisted.publicationMetadata); QCOMPARE(persisted.publicationMetadata->title,std::string("Publication in document"));
        auto *canvas=project.currentCanvas();
        canvas->configureTool("elements",{{"selector","Rectangle"},{"field-16","#FF0000"},{"field-2","Solid"}});
        QVERIFY(canvas->createElement({2,3,16,12}));
        QVERIFY(iiSharedCanvas::DocumentFile::readSnapshot(source.toLocalFile().toStdString(),persisted).ok());
        QCOMPARE(iiSharedCanvas::encodeIisc(persisted).bytes,iiSharedCanvas::encodeIisc(*canvas->document()).bytes);
        PublishDraft restored; restored.begin(&project); QCOMPARE(restored.values()["description"].toString().size(),80*1024);
        const auto prior=restored.values();
        QVERIFY(!restored.edit("description",QString(int(iiSharedCanvas::SerializationLimits{}.maximumMetadataStringBytes)+1,QChar('x'))));
        QCOMPARE(restored.values(),prior);
    }
    void savedCopiesUpdateAndExternalChangesAreProtected() {
        QTemporaryDir directory(DREAMSCAPES_TEST_DIRECTORY "/post-updates-XXXXXX"); QVERIFY(directory.isValid());
        EditorProject project; QVERIFY(project.createCanvas({{"width",64},{"height",32},{"unit","px"},{"background","White"}}));
        PublishDraft draft; draft.begin(&project);
        const auto source=directory.filePath("publication.iisc");
        QVERIFY2(draft.save(QUrl::fromLocalFile(source)),qPrintable(draft.error()));
        auto *canvas=project.currentCanvas();
        canvas->configureTool("elements",{{"selector","Rectangle"},{"field-16","#FF0000"},{"field-2","Solid"}});
        QVERIFY(canvas->createElement({2,3,16,12}));
        const auto revision=canvas->revision();
        QVERIFY2(draft.save(QUrl::fromLocalFile(source)),qPrintable(draft.error()));
        iiSharedCanvas::Document snapshot; QVERIFY(iiSharedCanvas::DocumentFile::readSnapshot(source.toStdString(),snapshot).ok());
        QCOMPARE(body(snapshot),body(*canvas->document())); QCOMPARE(canvas->revision(),revision);
        snapshot.publicationMetadata->title="Changed externally"; QVERIFY(writeCanvas(source,snapshot));
        const auto changed=iiFileProvider::native::File::read(nativePath(source));
        QVERIFY(!draft.save(QUrl::fromLocalFile(source)));
        QCOMPARE(iiFileProvider::native::File::read(nativePath(source)),changed);
        QVERIFY2(draft.load(QUrl::fromLocalFile(source)),qPrintable(draft.error()));
        QCOMPARE(draft.values()["title"].toString(),"Changed externally");
        ++snapshot.extent.width; QVERIFY(writeCanvas(source,snapshot));
        const auto values=draft.values(); QVERIFY(!draft.load(QUrl::fromLocalFile(source))); QCOMPARE(draft.values(),values);
    }
    void legacyAnnotationsAreImportedButNewSavesAreSelfContained() {
        using Value=ii::json::Value;
        QTemporaryDir directory(DREAMSCAPES_TEST_DIRECTORY "/post-legacy-XXXXXX"); QVERIFY(directory.isValid());
        EditorProject project; QVERIFY(project.createCanvas({{"width",64},{"height",32},{"unit","px"},{"background","White"}}));
        const auto document=*project.currentCanvas()->document();
        const auto source=directory.filePath("legacy.iisc"); QVERIFY(writeCanvas(source,document));
        PublishDraft draft; draft.begin(&project); const auto id=draft.canvas(0)["id"].toString().toStdString();
        const auto values=ii::json::stringify(Value(Value::Object{{"title",Value("Legacy publication")},{"description",Value("Import without data loss")},{"coverId",Value(id)},{"adult",Value(true)}}));
        const auto canvases=ii::json::stringify(Value(Value::Array{Value(Value::Object{{"id",Value(id)},{"caption",Value("Legacy caption")},{"altText",Value("Legacy alternative")}})}));
        QVERIFY(values); QVERIFY(canvases);
        iiFileProvider::FileMetadata metadata;
        metadata.fields={{"dreamscapes:publication:v1:values",*values},{"dreamscapes:publication:v1:canvases",*canvases},
            {"dreamscapes:publication:v1:canvas-sha256",iiFileProvider::native::sha256(iiFileProvider::native::File::read(nativePath(source)))}};
        iiFileProvider::FileCatalog catalog(nativePath(directory.path())); QVERIFY(catalog.registerFile("legacy.iisc",metadata).metadataWritten);
        const auto oldCompanion=iiFileProvider::native::File::read(nativePath(source+".iisacc.json"));
        QVERIFY2(draft.load(QUrl::fromLocalFile(source)),qPrintable(draft.error()));
        QCOMPARE(draft.values()["title"].toString(),"Legacy publication"); QCOMPARE(draft.values()["adult"].toBool(),true);
        QCOMPARE(draft.canvas(0)["caption"].toString(),"Legacy caption");
        const auto destination=directory.filePath("migrated.iisc"); QVERIFY2(draft.save(QUrl::fromLocalFile(destination)),qPrintable(draft.error()));
        QVERIFY(noCompanion(destination));
        iiSharedCanvas::Document saved; QVERIFY(iiSharedCanvas::DocumentFile::readSnapshot(destination.toStdString(),saved).ok());
        QVERIFY(saved.publicationMetadata); QCOMPARE(saved.publicationMetadata->title,std::string("Legacy publication"));
        QCOMPARE(saved.publicationMetadata->canvases.front().alternativeText,std::string("Legacy alternative"));
        QCOMPARE(iiFileProvider::native::File::read(nativePath(source+".iisacc.json")),oldCompanion);
    }
};
QTEST_MAIN(PublishDraftTests)
#include "tst_publishdraft.moc"
