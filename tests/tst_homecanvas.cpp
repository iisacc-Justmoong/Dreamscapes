#include "App/Views/Home/HomeCanvas.h"
#include "App/Generation/GenerationComposerModel.h"
#include "App/Generation/NativeHomeReferenceInputs.h"
#include "App/Views/Home/NativeHomeReferenceBinding.h"
#include "App/Views/Home/NativeHomeReferencePicker.h"
#include <iiSocietyContainer>
#include <QGuiApplication>
#include <QImage>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QtTest>

class HomeCanvasTests : public QObject {
    Q_OBJECT
    QTemporaryDir m_storage{DREAMSCAPES_TEST_DIRECTORY "/home-documents-XXXXXX.societycontainer"};
private slots:
    void initTestCase() {
        QVERIFY(iiSocietyContainer::SocietyDrive::create(m_storage.path()));
        qputenv("SOCIETY_CONTAINER_PATH", m_storage.path().toUtf8());
        qputenv("SOCIETY_STORAGE_SETTINGS_PATH", m_storage.filePath("settings.ini").toUtf8());
    }
    void pasteCommitsPixelsAndSupportsHistory();
    void rejectInvalidDropWithoutChangingCanvas();
    void resizeAndSubmissionPreserveTheImage();
    void referencesWorkWithoutAQuickCanvas();
    void nativeReferencePickerUsesHostWithoutAQuickView();
};

void HomeCanvasTests::pasteCommitsPixelsAndSupportsHistory()
{
    QTemporaryDir files(QStringLiteral(DREAMSCAPES_TEST_DIRECTORY) + "/home-canvas-XXXXXX");
    QImage image(80, 40, QImage::Format_ARGB32); image.fill(Qt::red);
    const auto source=QUrl::fromLocalFile(files.filePath("red.png")); QVERIFY(image.save(source.toLocalFile()));
    HomeCanvas canvas;
    canvas.setWidth(256); canvas.setHeight(256); canvas.fitToView();
    QVERIFY(canvas.addAttachment(source));
    QCOMPARE(canvas.attachments().size(), 1);
    const auto wrapper = canvas.attachments().first().toMap().value("documentSource").toUrl().toLocalFile();
    QVERIFY(wrapper.endsWith(".iisc")); QVERIFY(QFileInfo::exists(wrapper));
    QVERIFY(QFile::remove(source.toLocalFile()));
    QVERIFY(canvas.pasteAttachment(source, 128, 128));
    const auto pixel=canvas.selectedRasterPixels()->pixels[512*1024+512];
    QCOMPARE(pixel, quint32(0xffff0000));
    QVERIFY(canvas.hasContent()); QVERIFY(canvas.canUndo());
    QVERIFY(canvas.undo()); QVERIFY(!canvas.hasContent());
    QVERIFY(canvas.redo()); QVERIFY(canvas.hasContent());
    QVERIFY(canvas.clearSelectedLayer()); QVERIFY(!canvas.hasContent());
    QVERIFY(canvas.undo()); QVERIFY(canvas.hasContent());
}
void HomeCanvasTests::nativeReferencePickerUsesHostWithoutAQuickView()
{
    QTemporaryDir files(QStringLiteral(DREAMSCAPES_TEST_DIRECTORY) + "/native-home-picker-XXXXXX");
    QVERIFY(files.isValid());
    QImage image(8,8,QImage::Format_ARGB32); image.fill(Qt::yellow);
    const auto first=files.filePath("first.png"), second=files.filePath("second.png");
    QVERIFY(image.save(first)); QVERIFY(image.save(second));
    dreamscapes::NativeHomeReferenceInputs inputs;
    ii::gui::FileDialogCompletion completion;
    ii::gui::FileDialogOptions observed;
    std::vector<std::string> errors;
    const auto chooser=[&](ii::gui::FileDialogOptions options,ii::gui::FileDialogCompletion callback) -> ii::core::Result<void> {
        observed=std::move(options); completion=std::move(callback); return {};
    };
    dreamscapes::home::NativeHomeReferencePicker picker(&inputs, chooser,
        dreamscapes::home::homeReferenceDialogOptions("Attach reference images","Images and canvases"),
        [&](const ii::core::Error &error) { errors.push_back(error.message); });
    QVERIFY(picker.choose()); QVERIFY(picker.pending());
    QVERIFY(observed.multiple && observed.mode==ii::gui::FileDialogMode::open && observed.filters.size()==1);
    QCOMPARE(observed.filters.front().extensions, std::vector<std::string>({"iisc","png","jpg","jpeg","webp","bmp","tif","tiff","heic"}));
    QVERIFY(!picker.choose());
    completion(ii::gui::FileDialogSelection{.canceled=true});
    QVERIFY(!picker.pending() && inputs.attachments().isEmpty());
    QVERIFY(picker.choose()); auto stale=completion;
    completion(ii::gui::FileDialogSelection{.paths={first.toStdString(),files.filePath("missing.png").toStdString()}});
    QVERIFY(!picker.pending() && inputs.attachments().size()==1 && errors.size()==1);
    stale(ii::gui::FileDialogSelection{.paths={second.toStdString()}});
    QCOMPARE(inputs.attachments().size(),1);
    ii::gui::FileDialogCompletion late;
    {
        dreamscapes::home::NativeHomeReferencePicker transient(&inputs,
            [&](ii::gui::FileDialogOptions,ii::gui::FileDialogCompletion callback) -> ii::core::Result<void> { late=std::move(callback); return {}; },
            dreamscapes::home::homeReferenceDialogOptions("Attach","Images"),{});
        QVERIFY(transient.choose());
    }
    late(ii::gui::FileDialogSelection{.paths={second.toStdString()}});
    QCOMPARE(inputs.attachments().size(),1);
    dreamscapes::home::NativeHomeReferencePicker unavailable(&inputs,
        [](ii::gui::FileDialogOptions,ii::gui::FileDialogCompletion) -> ii::core::Result<void> {
            return ii::core::fail(ii::core::ErrorCode::unsupported,"Dialog unavailable");
        },dreamscapes::home::homeReferenceDialogOptions("Attach","Images"),{});
    const auto failed=unavailable.choose(); QVERIFY(!failed && failed.error().code==ii::core::ErrorCode::unsupported && !unavailable.pending());
    auto retiredInputs=std::make_unique<dreamscapes::NativeHomeReferenceInputs>();
    ii::gui::FileDialogCompletion retiredCompletion;
    dreamscapes::home::NativeHomeReferencePicker retired(retiredInputs.get(),
        [&](ii::gui::FileDialogOptions,ii::gui::FileDialogCompletion callback) -> ii::core::Result<void> { retiredCompletion=std::move(callback); return {}; },
        dreamscapes::home::homeReferenceDialogOptions("Attach","Images"),{});
    QVERIFY(retired.choose()); retiredInputs.reset();
    retiredCompletion(ii::gui::FileDialogSelection{.paths={second.toStdString()}});
    QVERIFY(!retired.pending() && !retired.choose());
    QVERIFY(QFileInfo::exists(first) && QFileInfo::exists(second));
}
void HomeCanvasTests::referencesWorkWithoutAQuickCanvas()
{
    QTemporaryDir files(QStringLiteral(DREAMSCAPES_TEST_DIRECTORY) + "/native-home-reference-XXXXXX");
    QVERIFY(files.isValid());
    QImage image(80,40,QImage::Format_ARGB32); image.fill(Qt::blue);
    const auto source = QUrl::fromLocalFile(files.filePath("blue.png")); QVERIFY(image.save(source.toLocalFile()));
    dreamscapes::NativeHomeReferenceInputs inputs;
    GenerationComposerModel draft; draft.setCanvas(&inputs); draft.setPrompt("  blue object  ");
    draft.setAspectRatio("16:9"); draft.setGenerationCount(3);
    dreamscapes::home::NativeHomeComposerSession::Callbacks callbacks;
    callbacks.removeAttachment = [&](std::string key) -> ii::core::Result<void> {
        return inputs.removeAttachment(QString::fromStdString(key)) ? ii::core::Result<void>{}
            : ii::core::fail(ii::core::ErrorCode::invalid_argument, inputs.inputError().toStdString());
    };
    dreamscapes::home::NativeHomeComposerSession session(&draft, {}, "Random", std::move(callbacks));
    dreamscapes::home::NativeHomeReferenceBinding binding(&inputs, &draft, &session,
        [](qint64 bytes) { return "Image · " + std::to_string(bytes) + " bytes"; });
    QSignalSpy changed(&inputs, &dreamscapes::NativeHomeReferenceInputs::attachmentsChanged);
    QVERIFY(draft.addAttachment(source)); QCOMPARE(inputs.attachments().size(),1); QCOMPARE(changed.size(),1);
    QVERIFY(draft.addAttachment(source)); QCOMPARE(inputs.attachments().size(),1); QCOMPARE(changed.size(),1);
    const auto entry=inputs.attachments().front().toMap();
    const auto cards=session.model()->snapshot.get().attachments;
    QCOMPARE(cards.size(),std::size_t(1));
    QCOMPARE(QString::fromStdString(cards.front().name),QStringLiteral("blue.png"));
    QCOMPARE(QString::fromStdString(cards.front().preview),entry.value("previewSource").toUrl().toString(QUrl::FullyEncoded));
    QVERIFY(entry.value("documentSource").toUrl().toLocalFile().endsWith(".iisc"));
    QVERIFY(QFileInfo::exists(source.toLocalFile())); QVERIFY(QFileInfo::exists(entry.value("documentSource").toUrl().toLocalFile()));
    const auto parameters=draft.generationParameters("model-id");
    QCOMPARE(parameters.value("prompt").toString(),QStringLiteral("blue object"));
    QCOMPARE(parameters.value("width").toInt(),1824); QCOMPARE(parameters.value("height").toInt(),1024);
    QCOMPARE(parameters.value("outputCount").toInt(),3);
    QCOMPARE(parameters.value("referenceImages").toList(),QVariantList{entry.value("previewSource").toUrl().toString()});
    QCOMPARE(draft.parameters("model-id",true).value("referenceImages").toList().size(),1);
    const auto key=entry.value("source").toUrl().toString(QUrl::FullyEncoded);
    QCOMPARE(QString::fromStdString(cards.front().key),key);
    QVERIFY(session.model()->dispatch(dreamscapes::home::HomeComposerCommandKind::removeAttachment,cards.front().key));
    QCOMPARE(inputs.attachments().size(),0); QCOMPARE(changed.size(),2);
    QVERIFY(session.model()->snapshot.get().attachments.empty());
    QVERIFY(!inputs.removeAttachment(key)); QVERIFY(!inputs.inputError().isEmpty());
    QCOMPARE(QString::fromStdString(session.model()->snapshot.get().errorText),inputs.inputError());
    draft.setErrorText("Seed error"); QCOMPARE(QString::fromStdString(session.model()->snapshot.get().errorText),QStringLiteral("Seed error"));
    draft.setErrorText({}); QCOMPARE(QString::fromStdString(session.model()->snapshot.get().errorText),inputs.inputError());
    QVERIFY(QFileInfo::exists(source.toLocalFile()));
    QVERIFY(draft.addAttachment(source)); QCOMPARE(session.model()->snapshot.get().attachments.size(),std::size_t(1));
    QVERIFY(session.model()->snapshot.get().errorText.empty());
    draft.setCanvas(nullptr); QVERIFY(session.model()->snapshot.get().attachments.empty());
    draft.setCanvas(&inputs); QCOMPARE(session.model()->snapshot.get().attachments.size(),std::size_t(1));
    QVERIFY(inputs.removeAttachment(key));
    bool nestedRemoval=false;
    auto observer=session.model()->snapshot.observe([&](const auto &snapshot,std::uint64_t) {
        if (!nestedRemoval && !snapshot.attachments.empty()) {
            nestedRemoval=true; (void)inputs.removeAttachment(QString::fromStdString(snapshot.attachments.front().key));
        }
    });
    QVERIFY(inputs.addAttachment(source));
    QVERIFY(nestedRemoval && inputs.attachments().isEmpty() && session.model()->snapshot.get().attachments.empty());
    observer.reset();
    auto temporary=std::make_unique<dreamscapes::NativeHomeReferenceInputs>();
    GenerationComposerModel other; other.setCanvas(temporary.get());
    dreamscapes::home::NativeHomeComposerSession otherSession(&other,{},"Random",{});
    dreamscapes::home::NativeHomeReferenceBinding otherBinding(temporary.get(),&other,&otherSession,
        [](qint64) { return "Image"; });
    QVERIFY(temporary->addAttachment(source)); QCOMPARE(otherSession.model()->snapshot.get().attachments.size(),std::size_t(1));
    temporary.reset();
    QVERIFY(!other.canvas() && otherSession.model()->snapshot.get().attachments.empty());
}
void HomeCanvasTests::rejectInvalidDropWithoutChangingCanvas()
{
    HomeCanvas canvas;
    const auto original=canvas.selectedRasterPixels()->pixels;
    QVERIFY(!canvas.pasteAttachment(QUrl("https://example.com/a.png"), 10, 10));
    QVERIFY(!canvas.addAttachment(QUrl::fromLocalFile("/missing.png")));
    QCOMPARE(canvas.selectedRasterPixels()->pixels, original);
    QCOMPARE(canvas.attachments().size(), 0);
    QVERIFY(!canvas.inputError().isEmpty());
}
void HomeCanvasTests::resizeAndSubmissionPreserveTheImage()
{
    QTemporaryDir files(QStringLiteral(DREAMSCAPES_TEST_DIRECTORY) + "/home-canvas-export-XXXXXX");
    QImage image(80, 40, QImage::Format_ARGB32); image.fill(Qt::green);
    const auto source=QUrl::fromLocalFile(files.filePath("green.png")); QVERIFY(image.save(source.toLocalFile()));
    HomeCanvas canvas;
    canvas.setWidth(256); canvas.setHeight(256); canvas.fitToView();
    QVERIFY(canvas.addAttachment(source)); QVERIFY(canvas.pasteAttachment(source,128,128));
    QVERIFY(canvas.setAspectRatio("16:9"));
    QCOMPARE(canvas.canvasWidth(),1824); QCOMPARE(canvas.canvasHeight(),1024);
    QVERIFY(canvas.hasContent());
    const auto args=canvas.generationParameters("a green object","model-id","16:9",3);
    QCOMPARE(args.value("width").toInt(),1824); QCOMPARE(args.value("height").toInt(),1024);
    QCOMPARE(args.value("outputCount").toInt(),3);
    const auto references=args.value("referenceImages").toList(); QCOMPARE(references.size(),2);
    GenerationComposerModel draft; draft.setCanvas(&canvas); draft.setPrompt("  a green object  ");
    draft.setAspectRatio("16:9"); draft.setGenerationCount(3);
    QCOMPARE(draft.canvas(), static_cast<QObject *>(&canvas));
    const auto draftInputs=draft.generationParameters("model-id");
    QCOMPARE(draftInputs.value("prompt").toString(),QStringLiteral("a green object"));
    QCOMPARE(draftInputs.value("referenceImages").toList().size(),2);
    QCOMPARE(draft.parameters("model-id",true).value("referenceImages").toList().size(),2);
    QImage exported(QUrl(references[0].toString()).toLocalFile());
    QCOMPARE(exported.size(),QSize(1824,1024)); QCOMPARE(exported.pixelColor(912,512),QColor(Qt::green));
    QVERIFY(canvas.removeAttachment(0)); QCOMPARE(canvas.attachments().size(),0);
    QVERIFY(canvas.hasContent()); // Removing a source does not erase pasted pixels.
    QVERIFY(canvas.clearSelectedLayer());
    QCOMPARE(canvas.generationParameters("empty","model-id","16:9",1).value("referenceImages").toList().size(),0);
}
int main(int argc,char **argv) {
    QGuiApplication app(argc,argv);
    qputenv("DREAMSCAPES_TEMP_DIRECTORY",DREAMSCAPES_TEST_DIRECTORY);
    HomeCanvasTests tests; return QTest::qExec(&tests,argc,argv);
}
#include "tst_homecanvas.moc"
