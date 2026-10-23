#include "App/Views/Editor/EditorGraphicHelperValues.h"
#include "App/Views/Editor/EditorGraphicAdjust.h"
#include <iiGraphicAdjust/iiGraphicAdjust.h>
#include <QtTest>
#include <limits>

namespace GA = iiGraphicAdjust;
namespace H = graphicHelper;
namespace {
QImage fixture() {
    QImage source(9, 7, QImage::Format_ARGB32);
    for (int y = 0; y < 7; ++y) for (int x = 0; x < 9; ++x)
        source.setPixel(x, y, qRgba(20 + x * 23, 25 + y * 30, 30 + (x + y) * 12, x == 0 ? 91 : 255));
    return source;
}
GA::Image native(const QImage &source) {
    std::vector<std::uint32_t> pixels;
    for (int y = 0; y < source.height(); ++y) for (int x = 0; x < source.width(); ++x) pixels.push_back(source.pixel(x, y));
    return *GA::fromArgb32(source.width(), source.height(), pixels);
}
}
class GraphicHelperValuesTests : public QObject {
    Q_OBJECT
private slots:
    void everyExistingHelperRetainsItsSettings_data() {
        QTest::addColumn<QString>("operation"); QTest::addColumn<QVariantMap>("parameters");
        QTest::newRow("light") << QString("autoDevelopLight") << QVariantMap{};
        QTest::newRow("white") << QString("autoWhiteBalance") << QVariantMap{{"asShotTemperature", 5600.0}, {"asShotTint", 8.0}};
        QTest::newRow("sample-white") << QString("sampledWhiteBalance") << QVariantMap{{"sample", QVariantList{.6, .5, .4, 1.0}}, {"asShotTemperature", 5600.0}, {"asShotTint", 8.0}};
        QTest::newRow("preset") << QString("whiteBalancePreset") << QVariantMap{{"preset", "Tungsten"}};
        QTest::newRow("mix") << QString("autoBlackAndWhiteMix") << QVariantMap{};
        QTest::newRow("point") << QString("samplePointColor") << QVariantMap{{"sample", "#DD6633"}};
        QTest::newRow("lens-scale") << QString("autoLensScale") << QVariantMap{{"distortion", -12.0}, {"vertical", 3.0}};
        QTest::newRow("chromatic") << QString("estimateChromaticAberration") << QVariantMap{};
    }
    void everyExistingHelperRetainsItsSettings() {
        QFETCH(QString, operation); QFETCH(QVariantMap, parameters);
        const auto source = fixture(); const auto input = native(source); const auto before = GA::toArgb32(input.view()); QVERIFY(before);
        const auto oracle = EditorGraphicAdjust::process(source, operation, parameters); QVERIFY2(oracle.ok(), qPrintable(oracle.error));
        const auto prepared = EditorGraphicAdjust::prepareHelper(operation, parameters); QVERIFY2(prepared.ok(), qPrintable(prepared.error));
        const auto view = input.view(); const auto result = H::calculate(*prepared.request, &view);
        QVERIFY2(result.ok(), result.error.c_str()); QCOMPARE(EditorGraphicAdjust::helperParameters(result), oracle.parameters);
        QCOMPARE(*GA::toArgb32(input.view()), *before); QCOMPARE(source, fixture());
    }
    void pureHelpersNeedNoRasterInput_data() {
        QTest::addColumn<QString>("operation"); QTest::addColumn<QVariantMap>("parameters");
        QTest::newRow("point") << QString("samplePointColor") << QVariantMap{{"sample", QVariantList{.2, .4, .6, 1.0}}};
        QTest::newRow("white") << QString("sampledWhiteBalance") << QVariantMap{{"sample", "#CCCCCC"}};
        QTest::newRow("preset") << QString("whiteBalancePreset") << QVariantMap{{"preset", "Daylight"}};
    }
    void pureHelpersNeedNoRasterInput() {
        QFETCH(QString, operation); QFETCH(QVariantMap, parameters);
        const auto prepared = EditorGraphicAdjust::prepareHelper(operation, parameters); QVERIFY(prepared.ok());
        QVERIFY(!H::needsImage(*prepared.request)); QVERIFY(H::calculate(*prepared.request, nullptr).ok());
    }
    void imageHelpersRejectMissingInput() {
        for (const auto *operation : {"autoDevelopLight", "autoWhiteBalance", "autoBlackAndWhiteMix", "autoLensScale", "estimateChromaticAberration"}) {
            const auto prepared = EditorGraphicAdjust::prepareHelper(operation, {}); QVERIFY(prepared.ok());
            QVERIFY(H::needsImage(*prepared.request)); const auto result = H::calculate(*prepared.request, nullptr);
            QVERIFY(!result.ok()); QVERIFY(!result.error.empty()); QVERIFY(result.parameters.empty());
        }
    }
    void malformedParametersAreRejected_data() {
        QTest::addColumn<QString>("operation"); QTest::addColumn<QVariantMap>("parameters");
        QTest::newRow("not-helper") << QString("invert") << QVariantMap{};
        QTest::newRow("unknown-light") << QString("autoDevelopLight") << QVariantMap{{"unknown", 1.0}};
        QTest::newRow("unknown-wb") << QString("autoWhiteBalance") << QVariantMap{{"unknown", 1.0}};
        QTest::newRow("no-sample") << QString("samplePointColor") << QVariantMap{};
        QTest::newRow("invalid-sample") << QString("sampledWhiteBalance") << QVariantMap{{"sample", QVariantList{.2, .4}}};
        QTest::newRow("boolean-temperature") << QString("autoWhiteBalance") << QVariantMap{{"temperature", true}};
        QTest::newRow("preset") << QString("whiteBalancePreset") << QVariantMap{{"preset", "Unknown"}};
        QTest::newRow("nan") << QString("autoLensScale") << QVariantMap{{"angle", std::numeric_limits<double>::quiet_NaN()}};
        QTest::newRow("extra-mix") << QString("autoBlackAndWhiteMix") << QVariantMap{{"amount", 3}};
        QTest::newRow("extra-ca") << QString("estimateChromaticAberration") << QVariantMap{{"amount", 3}};
    }
    void malformedParametersAreRejected() {
        QFETCH(QString, operation); QFETCH(QVariantMap, parameters);
        const auto prepared = EditorGraphicAdjust::prepareHelper(operation, parameters);
        QVERIFY(!prepared.ok()); QVERIFY(!prepared.error.isEmpty());
    }
    void cancellationAndSdkValidationReturnNoParameters() {
        const auto prepared = EditorGraphicAdjust::prepareHelper("samplePointColor", {{"sample", "#123456"}}); QVERIFY(prepared.ok());
        std::stop_source stop; stop.request_stop(); const auto retired = H::calculate(*prepared.request, nullptr, stop.get_token());
        QVERIFY(retired.cancelled); QVERIFY(!retired.ok()); QVERIFY(retired.error.empty()); QVERIFY(retired.parameters.empty());
        const auto invalid = EditorGraphicAdjust::prepareHelper("sampledWhiteBalance", {{"sample", "#000000"}}); QVERIFY(invalid.ok());
        const auto result = H::calculate(*invalid.request, nullptr); QVERIFY(!result.ok()); QVERIFY(!result.error.empty());
    }
    void catalogAndCanonicalNamesAreComplete() {
        const QStringList expected{"autoBlackAndWhiteMix", "autoDevelopLight", "autoLensScale", "autoWhiteBalance", "estimateChromaticAberration", "samplePointColor", "sampledWhiteBalance", "whiteBalancePreset"};
        QCOMPARE(EditorGraphicAdjust::helpers(), expected);
        for (const auto &name : expected) {
            QVERIFY(EditorGraphicAdjust::isHelper(name)); QVERIFY(EditorGraphicAdjust::isHelper("  " + name + "  "));
        }
        QVERIFY(!EditorGraphicAdjust::isHelper("invert"));
    }
};
QTEST_GUILESS_MAIN(GraphicHelperValuesTests)
#include "tst_graphichelpervalues.moc"
