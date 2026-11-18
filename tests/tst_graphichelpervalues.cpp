#include "App/Views/Editor/EditorGraphicHelperValues.h"
#include "App/Views/Editor/EditorGraphicAdjust.h"
#include <iiGraphicAdjust>
#include "CTestSupport.h"
#include <QCoreApplication>
#include <vector>
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
struct TestRow { const char* name; QString operation; QVariantMap parameters; };

class GraphicHelperValuesTests {
public:
    static std::vector<TestRow> everyExistingHelperRetainsItsSettingsRows() {
        return {
            {"light", QString("autoDevelopLight"), QVariantMap{}},
            {"white", QString("autoWhiteBalance"), QVariantMap{{"asShotTemperature", 5600.0}, {"asShotTint", 8.0}}},
            {"sample-white", QString("sampledWhiteBalance"), QVariantMap{{"sample", QVariantList{.6, .5, .4, 1.0}}, {"asShotTemperature", 5600.0}, {"asShotTint", 8.0}}},
            {"preset", QString("whiteBalancePreset"), QVariantMap{{"preset", "Tungsten"}}},
            {"mix", QString("autoBlackAndWhiteMix"), QVariantMap{}},
            {"point", QString("samplePointColor"), QVariantMap{{"sample", "#DD6633"}}},
            {"lens-scale", QString("autoLensScale"), QVariantMap{{"distortion", -12.0}, {"vertical", 3.0}}},
            {"chromatic", QString("estimateChromaticAberration"), QVariantMap{}},
        };
    }
    void everyExistingHelperRetainsItsSettings(const QString& operation, const QVariantMap& parameters) {
        const auto source = fixture(); const auto input = native(source); const auto before = GA::toArgb32(input.view()); CTEST_CHECK(before);
        const auto oracle = EditorGraphicAdjust::process(source, operation, parameters); CTEST_CHECK_MESSAGE(oracle.ok(), qPrintable(oracle.error));
        const auto prepared = EditorGraphicAdjust::prepareHelper(operation, parameters); CTEST_CHECK_MESSAGE(prepared.ok(), qPrintable(prepared.error));
        const auto view = input.view(); const auto result = H::calculate(*prepared.request, &view);
        CTEST_CHECK_MESSAGE(result.ok(), result.error.c_str()); CTEST_COMPARE(EditorGraphicAdjust::helperParameters(result), oracle.parameters);
        CTEST_COMPARE(*GA::toArgb32(input.view()), *before); CTEST_COMPARE(source, fixture());
    }
    static std::vector<TestRow> pureHelpersNeedNoRasterInputRows() {
        return {
            {"point", QString("samplePointColor"), QVariantMap{{"sample", QVariantList{.2, .4, .6, 1.0}}}},
            {"white", QString("sampledWhiteBalance"), QVariantMap{{"sample", "#CCCCCC"}}},
            {"preset", QString("whiteBalancePreset"), QVariantMap{{"preset", "Daylight"}}},
        };
    }
    void pureHelpersNeedNoRasterInput(const QString& operation, const QVariantMap& parameters) {
        const auto prepared = EditorGraphicAdjust::prepareHelper(operation, parameters); CTEST_CHECK(prepared.ok());
        CTEST_CHECK(!H::needsImage(*prepared.request)); CTEST_CHECK(H::calculate(*prepared.request, nullptr).ok());
    }
    void imageHelpersRejectMissingInput() {
        for (const auto *operation : {"autoDevelopLight", "autoWhiteBalance", "autoBlackAndWhiteMix", "autoLensScale", "estimateChromaticAberration"}) {
            const auto prepared = EditorGraphicAdjust::prepareHelper(operation, {}); CTEST_CHECK(prepared.ok());
            CTEST_CHECK(H::needsImage(*prepared.request)); const auto result = H::calculate(*prepared.request, nullptr);
            CTEST_CHECK(!result.ok()); CTEST_CHECK(!result.error.empty()); CTEST_CHECK(result.parameters.empty());
        }
    }
    static std::vector<TestRow> malformedParametersAreRejectedRows() {
        return {
            {"not-helper", QString("invert"), QVariantMap{}},
            {"unknown-light", QString("autoDevelopLight"), QVariantMap{{"unknown", 1.0}}},
            {"unknown-wb", QString("autoWhiteBalance"), QVariantMap{{"unknown", 1.0}}},
            {"no-sample", QString("samplePointColor"), QVariantMap{}},
            {"invalid-sample", QString("sampledWhiteBalance"), QVariantMap{{"sample", QVariantList{.2, .4}}}},
            {"boolean-temperature", QString("autoWhiteBalance"), QVariantMap{{"temperature", true}}},
            {"preset", QString("whiteBalancePreset"), QVariantMap{{"preset", "Unknown"}}},
            {"nan", QString("autoLensScale"), QVariantMap{{"angle", std::numeric_limits<double>::quiet_NaN()}}},
            {"extra-mix", QString("autoBlackAndWhiteMix"), QVariantMap{{"amount", 3}}},
            {"extra-ca", QString("estimateChromaticAberration"), QVariantMap{{"amount", 3}}},
        };
    }
    void malformedParametersAreRejected(const QString& operation, const QVariantMap& parameters) {
        const auto prepared = EditorGraphicAdjust::prepareHelper(operation, parameters);
        CTEST_CHECK(!prepared.ok()); CTEST_CHECK(!prepared.error.isEmpty());
    }
    void cancellationAndSdkValidationReturnNoParameters() {
        const auto prepared = EditorGraphicAdjust::prepareHelper("samplePointColor", {{"sample", "#123456"}}); CTEST_CHECK(prepared.ok());
        std::stop_source stop; stop.request_stop(); const auto retired = H::calculate(*prepared.request, nullptr, stop.get_token());
        CTEST_CHECK(retired.cancelled); CTEST_CHECK(!retired.ok()); CTEST_CHECK(retired.error.empty()); CTEST_CHECK(retired.parameters.empty());
        const auto invalid = EditorGraphicAdjust::prepareHelper("sampledWhiteBalance", {{"sample", "#000000"}}); CTEST_CHECK(invalid.ok());
        const auto result = H::calculate(*invalid.request, nullptr); CTEST_CHECK(!result.ok()); CTEST_CHECK(!result.error.empty());
    }
    void catalogAndCanonicalNamesAreComplete() {
        const QStringList expected{"autoBlackAndWhiteMix", "autoDevelopLight", "autoLensScale", "autoWhiteBalance", "estimateChromaticAberration", "samplePointColor", "sampledWhiteBalance", "whiteBalancePreset"};
        CTEST_COMPARE(EditorGraphicAdjust::helpers(), expected);
        for (const auto &name : expected) {
            CTEST_CHECK(EditorGraphicAdjust::isHelper(name)); CTEST_CHECK(EditorGraphicAdjust::isHelper("  " + name + "  "));
        }
        CTEST_CHECK(!EditorGraphicAdjust::isHelper("invert"));
    }
};
int main(int argc, char** argv) {
    QCoreApplication application(argc, argv);
    dreamscapes_ctest::Runner runner(argc == 2 ? argv[1] : "");
    GraphicHelperValuesTests test;
    for (const auto& row : test.everyExistingHelperRetainsItsSettingsRows())
        runner.run(std::string("everyExistingHelperRetainsItsSettings:") + row.name,
                   [&] { test.everyExistingHelperRetainsItsSettings(row.operation, row.parameters); });
    for (const auto& row : test.pureHelpersNeedNoRasterInputRows())
        runner.run(std::string("pureHelpersNeedNoRasterInput:") + row.name,
                   [&] { test.pureHelpersNeedNoRasterInput(row.operation, row.parameters); });
    runner.run("imageHelpersRejectMissingInput", [&] { test.imageHelpersRejectMissingInput(); });
    for (const auto& row : test.malformedParametersAreRejectedRows())
        runner.run(std::string("malformedParametersAreRejected:") + row.name,
                   [&] { test.malformedParametersAreRejected(row.operation, row.parameters); });
    runner.run("cancellationAndSdkValidationReturnNoParameters", [&] { test.cancellationAndSdkValidationReturnNoParameters(); });
    runner.run("catalogAndCanonicalNamesAreComplete", [&] { test.catalogAndCanonicalNamesAreComplete(); });
    return runner.result();
}
