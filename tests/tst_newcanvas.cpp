#include "App/Views/Home/CanvasPresets.h"
#include "App/Views/Editor/EditorCanvas.h"
#include <QtTest>
#include <QSet>
#include <limits>

class NewCanvasTests : public QObject {
    Q_OBJECT
private slots:
    void catalogueContainsEveryDesignedFormat() {
        CanvasPresets catalogue;
        QCOMPARE(catalogue.categories().size(), 14);
        QCOMPARE(catalogue.count(), 300);
        QSet<QString> ids;
        for (int category = 0; category < 14; ++category) {
            const auto groups = catalogue.sections(category);
            QVERIFY(!groups.isEmpty());
            int count = 0;
            for (const auto &group : groups) for (const auto &value : group.toMap().value("presets").toList()) {
                const auto preset = value.toMap();
                const auto id = preset.value("id").toString();
                QVERIFY(!ids.contains(id));
                ids.insert(id);
                QCOMPARE(catalogue.preset(id), preset);
                const auto specification = catalogue.specification(preset.value("width").toDouble(),
                    preset.value("height").toDouble(), preset.value("unit").toString(),
                    preset.value("ppi").isNull() ? 300 : preset.value("ppi").toDouble());
                QVERIFY2(specification.value("valid").toBool(), qPrintable(id));
                QCOMPARE(specification.value("pixelWidth"), preset.value("pixelWidth"));
                QCOMPARE(specification.value("pixelHeight"), preset.value("pixelHeight"));
                ++count;
            }
            QCOMPARE(count, catalogue.categories()[category].toMap().value("count").toInt());
        }
        QCOMPARE(ids.size(), 300);
        QCOMPARE(catalogue.sections(0).size(), 16); // Platform-only SNS sections.
        QVERIFY(catalogue.preset("missing").isEmpty());
    }
    void globalSearchAndPhysicalUnits() {
        CanvasPresets catalogue;
        const auto results = catalogue.sections(0, "A4");
        int count = 0;
        for (const auto &value : results) count += value.toMap().value("presets").toList().size();
        QCOMPARE(count, 11);
        QVERIFY(!catalogue.sections(0, "1920 × 1080").isEmpty());
        QVERIFY(!catalogue.sections(0, "Threads").isEmpty());
        QVERIFY(!catalogue.sections(0, "KakaoTalk").isEmpty());
        QCOMPARE(catalogue.sections(0, "X").size(), 1);
        QCOMPARE(catalogue.sections(0, "X").first().toMap().value("name").toString(), "Social media · X");
        QVERIFY(catalogue.sections(0, "does-not-exist").isEmpty());
        const auto a4 = catalogue.specification(210, 297, "mm", 300);
        QCOMPARE(a4.value("pixelWidth").toInt(), 2480);
        QCOMPARE(a4.value("pixelHeight").toInt(), 3508);
        const auto letter = catalogue.specification(8.5, 11, "in", 300);
        QCOMPARE(letter.value("pixelWidth").toInt(), 2550);
        QCOMPARE(letter.value("pixelHeight").toInt(), 3300);
        QCOMPARE(catalogue.convert(25.4, "mm", "in", 300), 1.0);
        QCOMPARE(catalogue.convert(300, "px", "in", 300), 1.0);
    }
    void invalidInputCannotAllocateADocument() {
        CanvasPresets catalogue;
        for (double width : {0.0, -1.0, 10.5, 32769.0, std::numeric_limits<double>::infinity(),
                             std::numeric_limits<double>::quiet_NaN()})
            QVERIFY(!catalogue.specification(width, 100, "px").value("valid").toBool());
        QVERIFY(!catalogue.specification(32768, 32768, "px").value("valid").toBool());
        QVERIFY(!catalogue.specification(100, 100, "cm").value("valid").toBool());
        QVERIFY(!catalogue.specification(100, 100, "mm", 0).value("valid").toBool());
        QVERIFY(!catalogue.specification(100, 100, "px", 300, "invalid").value("valid").toBool());
    }
    void sdkDocumentRetainsDimensionsAndBackground() {
        CanvasPresets catalogue;
        EditorCanvas canvas;
        for (const auto &background : {QString("White"), QString("Black"), QString("Transparent")}) {
            auto specification = catalogue.specification(210, 297, "mm", 300, background);
            specification.insert("name", "A4");
            QVERIFY(canvas.createCanvas(specification));
            QVERIFY(canvas.documentReady());
            QCOMPARE(canvas.canvasWidth(), 2480);
            QCOMPARE(canvas.canvasHeight(), 3508);
            QCOMPARE(canvas.specification().value("name").toString(), "A4");
            const auto *document = canvas.document();
            QVERIFY(document);
            QCOMPARE(document->layers.size(), background == "Transparent" ? 0u : 1u);
            if (background != "Transparent") {
                const auto &asset = std::get<iiSharedCanvas::VectorAsset>(document->assets.front());
                QCOMPARE(asset.paths.front().fill->argb, background == "White" ? 0xffffffffU : 0xff000000U);
            }
        }
        const auto original = canvas.specification();
        QVERIFY(!canvas.createCanvas({{"width", -1}, {"height", 100}, {"unit", "px"}}));
        QCOMPARE(canvas.specification(), original);
        QCOMPARE(canvas.canvasWidth(), 2480);
        // Largest catalogue sheet is sparse vector content, with no full-size raster asset.
        QVERIFY(canvas.createCanvas(catalogue.specification(1189, 841, "mm", 300)));
        QCOMPARE(canvas.document()->assets.size(), 1u);
        QVERIFY(std::holds_alternative<iiSharedCanvas::VectorAsset>(canvas.document()->assets.front()));
    }
};
QTEST_MAIN(NewCanvasTests)
#include "tst_newcanvas.moc"
