#include "App/Views/Home/HomeCanvas.h"
#include <QGuiApplication>
#include <QImage>
#include <QTemporaryDir>
#include <QtTest>

class HomeCanvasTests : public QObject {
    Q_OBJECT
private slots:
    void pasteCommitsPixelsAndSupportsHistory();
    void rejectInvalidDropWithoutChangingCanvas();
    void resizeAndSubmissionPreserveTheImage();
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
    QVERIFY(canvas.pasteAttachment(source, 128, 128));
    const auto pixel=canvas.selectedRasterPixels()->pixels[512*1024+512];
    QCOMPARE(pixel, quint32(0xffff0000));
    QVERIFY(canvas.hasContent()); QVERIFY(canvas.canUndo());
    QVERIFY(canvas.undo()); QVERIFY(!canvas.hasContent());
    QVERIFY(canvas.redo()); QVERIFY(canvas.hasContent());
    QVERIFY(canvas.clearSelectedLayer()); QVERIFY(!canvas.hasContent());
    QVERIFY(canvas.undo()); QVERIFY(canvas.hasContent());
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
