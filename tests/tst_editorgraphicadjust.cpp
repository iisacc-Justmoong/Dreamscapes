#include "App/Views/Editor/EditorGraphicAdjust.h"
#include <iiGraphicAdjust/iiGraphicAdjust.h>
#include <QColorSpace>
#include <QtTest>
#include <algorithm>
#include <cmath>
#include <limits>

namespace GA = iiGraphicAdjust;
namespace {
QImage fixture() {
    QImage image(9,7,QImage::Format_ARGB32);
    for(int y=0;y<image.height();++y) for(int x=0;x<image.width();++x)
        image.setPixel(x,y,qRgba(20+x*23,25+y*30,30+(x+y)*12,(x==0 && y==0) ? 0 : (x==1 ? 91 : 255)));
    return image;
}
QImage sdkImage(const GA::Image &image) {
    auto words=GA::toArgb32(image.view());
    if(!words) return {};
    QImage result(int(image.width),int(image.height),QImage::Format_ARGB32);
    for(int y=0;y<result.height();++y) for(int x=0;x<result.width();++x) result.setPixel(x,y,(*words)[std::size_t(y)*result.width()+x]);
    result.setColorSpace(QColorSpace::SRgb); return result;
}
GA::Image sdkSource(const QImage &source) {
    std::vector<std::uint32_t> words;
    for(int y=0;y<source.height();++y) for(int x=0;x<source.width();++x) words.push_back(source.pixel(x,y));
    return *GA::fromArgb32(source.width(),source.height(),words);
}
}

class EditorGraphicAdjustTests final : public QObject {
    Q_OBJECT
private slots:
    void detachedPixelsAndAlpha();
    void exposureAndToneUnits();
    void indexedCurvesAndMixer();
    void structuredResources();
    void sdkOptionDefaults();
    void malformedParameters();
    void geometricMasksAndLocalAdjustments();
    void automaticSettings();
    void chromaticAberrationEstimatorReturnsOpticsSettings();
    void lookPresetAndGalleryExecution();
};

void EditorGraphicAdjustTests::detachedPixelsAndAlpha() {
    auto source=fixture(); const auto original=source.copy();
    const auto neutral=EditorGraphicAdjust::process(source,"filter.blur.gaussianBlur",{{"radius",0.0}});
    QVERIFY2(neutral.ok(),qPrintable(neutral.error));
    for(int y=0;y<source.height();++y) for(int x=0;x<source.width();++x) QCOMPARE(neutral.image.pixel(x,y),source.pixel(x,y));
    auto output=neutral.image; output.setPixel(2,2,qRgb(0,0,0));
    QCOMPARE(source,original);
    const auto inverted=EditorGraphicAdjust::process(source,"invert"); QVERIFY2(inverted.ok(),qPrintable(inverted.error));
    QCOMPARE(inverted.image.pixel(0,0),source.pixel(0,0));
    QCOMPARE(qAlpha(inverted.image.pixel(1,2)),91);
    QCOMPARE(qRed(inverted.image.pixel(3,2)),255-qRed(source.pixel(3,2)));
    QCOMPARE(source,original);
    const auto premultiplied=source.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    const auto processed=EditorGraphicAdjust::process(premultiplied,"invert"); QVERIFY2(processed.ok(),qPrintable(processed.error));
    QCOMPARE(qAlpha(processed.image.pixel(1,2)),91);
}

void EditorGraphicAdjustTests::exposureAndToneUnits() {
    QImage source(1,1,QImage::Format_ARGB32); source.fill(qRgba(128,128,128,77));
    const auto result=EditorGraphicAdjust::process(source,"exposure",{{"stops",1.0}}); QVERIFY2(result.ok(),qPrintable(result.error));
    const auto encoded=128.0/255.0;
    const auto linear=std::pow((encoded+.055)/1.055,2.4)*2;
    const auto expected=int(std::lround((1.055*std::pow(linear,1/2.4)-.055)*255));
    QCOMPARE(qRed(result.image.pixel(0,0)),expected); QCOMPARE(qAlpha(result.image.pixel(0,0)),77);
    const auto light=EditorGraphicAdjust::process(source,"developLight",{{"exposure",1.0}}); QVERIFY2(light.ok(),qPrintable(light.error));
    QVERIFY(qRed(light.image.pixel(0,0))>128);
    const auto bright=EditorGraphicAdjust::process(source,"brightnessContrast",{{"brightness",.25}}); QVERIFY2(bright.ok(),qPrintable(bright.error));
    QCOMPARE(qRed(bright.image.pixel(0,0)),192);
}

void EditorGraphicAdjustTests::indexedCurvesAndMixer() {
    const auto source=fixture();
    const auto curves=EditorGraphicAdjust::process(source,"curves",{{"master.points[0].output",1.0},{"master.points[1].output",0.0}});
    QVERIFY2(curves.ok(),qPrintable(curves.error));
    QCOMPARE(qRed(curves.image.pixel(3,2)),255-qRed(source.pixel(3,2)));
    GA::DevelopColorOptions options; options.mixer[3].hue=40; options.mixer[3].saturation=-20;
    const auto expected=GA::developColor(sdkSource(source).view(),options); QVERIFY(expected);
    const auto actual=EditorGraphicAdjust::process(source,"developColor",{{"mixer[3].hue",40.0},{"mixer[3].saturation",-20.0}});
    QVERIFY2(actual.ok(),qPrintable(actual.error)); QCOMPARE(actual.image,sdkImage(*expected));
    GA::DevelopCurveOptions defaults;
    const auto defaultCurve=GA::developCurve(sdkSource(source).view(),defaults); QVERIFY(defaultCurve);
    const auto noEdits=EditorGraphicAdjust::process(source,"developCurve"); QVERIFY2(noEdits.ok(),qPrintable(noEdits.error));
    QCOMPARE(noEdits.image,sdkImage(*defaultCurve));
}

void EditorGraphicAdjustTests::sdkOptionDefaults() {
    const auto source=fixture(); const auto input=sdkSource(source);
    GA::MotionBlurOptions motion;
    QCOMPARE(motion.angle,0.0); QCOMPARE(motion.distance,10.0);
    auto expectedMotion=GA::motionBlur(input.view(),motion); QVERIFY(expectedMotion);
    auto actualMotion=EditorGraphicAdjust::process(source,"motionBlur"); QVERIFY2(actualMotion.ok(),qPrintable(actualMotion.error)); QCOMPARE(actualMotion.image,sdkImage(*expectedMotion));
    GA::CameraRawOptions raw;
    QCOMPARE(raw.temperature,0.0); QCOMPARE(raw.exposure,0.0); QCOMPARE(raw.sharpenRadius,1.0); QCOMPARE(raw.sharpenDetail,25.0); QCOMPARE(raw.noiseLuminanceDetail,50.0); QCOMPARE(raw.grainSize,25.0); QCOMPARE(raw.grainRoughness,50.0);
    QCOMPARE(raw.curveSplits[0],25.0); QCOMPARE(raw.curveSplits[1],50.0); QCOMPARE(raw.curveSplits[2],75.0); QVERIFY(raw.pointCurve.empty());
    auto expectedRaw=GA::cameraRawFilter(input.view(),raw); QVERIFY(expectedRaw);
    auto actualRaw=EditorGraphicAdjust::process(source,"cameraRawFilter"); QVERIFY2(actualRaw.ok(),qPrintable(actualRaw.error)); QCOMPARE(actualRaw.image,sdkImage(*expectedRaw));
    GA::LensBlurOptions lens;
    QCOMPARE(lens.radius,15.0); QCOMPARE(lens.blades,6U); QCOMPARE(lens.threshold,255.0); QCOMPARE(lens.depth,GA::LensDepthSource::None); QVERIFY(!lens.depthMap);
    auto expectedLens=GA::lensBlur(input.view(),lens); QVERIFY(expectedLens);
    auto actualLens=EditorGraphicAdjust::process(source,"lensBlur"); QVERIFY2(actualLens.ok(),qPrintable(actualLens.error)); QCOMPARE(actualLens.image,sdkImage(*expectedLens));
    GA::GalleryColoredPencilOptions pencil;
    QCOMPARE(pencil.pencilWidth,4.0); QCOMPARE(pencil.strokePressure,8.0); QCOMPARE(pencil.paperBrightness,25.0); QCOMPARE(pencil.background[0],1.0);
    auto expectedPencil=GA::coloredPencil(input.view(),pencil); QVERIFY(expectedPencil);
    auto actualPencil=EditorGraphicAdjust::process(source,"coloredPencil"); QVERIFY2(actualPencil.ok(),qPrintable(actualPencil.error)); QCOMPARE(actualPencil.image,sdkImage(*expectedPencil));
}

void EditorGraphicAdjustTests::structuredResources() {
    const auto source=fixture();
    QVariantList lutValues;
    for(int b=0;b<2;++b) for(int g=0;g<2;++g) for(int r=0;r<2;++r) lutValues.append(QVariant(QVariantList{double(r),double(g),double(b)}));
    const QVariantMap lut{{"kind","ThreeDimensional"},{"size",2},{"values",lutValues}};
    const auto mapped=EditorGraphicAdjust::process(source,"colorLookup",{{"lut",lut},{"amount",1.0}});
    QVERIFY2(mapped.ok(),qPrintable(mapped.error));
    for(int y=0;y<source.height();++y) for(int x=0;x<source.width();++x) QCOMPARE(mapped.image.pixel(x,y),source.pixel(x,y));
    const auto matched=EditorGraphicAdjust::process(source,"matchColor",{{"reference",source}}); QVERIFY2(matched.ok(),qPrintable(matched.error));
    QCOMPARE(matched.image.size(),source.size());
    const auto mask=EditorGraphicAdjust::makeMask(source,"linearMask"); QVERIFY(mask.ok());
    QVariantList coverage; for(float value:mask.coverage) coverage.append(double(value));
    const auto overlay=EditorGraphicAdjust::process(source,"maskColorOverlay",{{"mask",QVariantMap{{"coverage",coverage}}},{"color","#00FF00"},{"opacity",50.0}});
    QVERIFY2(overlay.ok(),qPrintable(overlay.error)); QVERIFY(overlay.image!=mapped.image);
}

void EditorGraphicAdjustTests::malformedParameters() {
    const auto source=fixture();
    const QList<QPair<QString,QVariantMap>> cases{
        {"developLight",{{"exposure",std::numeric_limits<double>::quiet_NaN()}}},
        {"developLight",{{"exposure",true}}},
        {"developLight",{{"unknown",1.0}}},
        {"developLight",{{"exposure",6.0}}},
        {"filmGrain",{{"seed",1.5}}},
        {"filmGrain",{{"seed",-1}}},
        {"filmGrain",{{"seed",4294967296.0}}},
        {"radialBlur",{{"method","No such method"}}},
        {"radialBlur",{{"method",QVariantMap{}}}},
        {"developColor",{{"mixer[8].hue",1.0}}},
        {"curves",{{"master.points",QVariantList{QVariantMap{{"input",1.0},{"output",0.0}},QVariantMap{{"input",0.0},{"output",1.0}}}}}},
        {"colorLookup",{}},
        {"displace",{}},
        {"notImplemented",{}}
    };
    for(const auto &entry:cases) { const auto result=EditorGraphicAdjust::process(source,entry.first,entry.second); QVERIFY2(!result.ok(),qPrintable(entry.first)); QVERIFY(!result.error.isEmpty()); QVERIFY(result.image.isNull()); }
    QVERIFY(!EditorGraphicAdjust::process({},"invert").ok());
    QVERIFY(!EditorGraphicAdjust::supports("notImplemented"));
    QVERIFY(EditorGraphicAdjust::supports("filter.blur.gaussianBlur"));
    QVERIFY(EditorGraphicAdjust::supports("developDetail"));
    QVERIFY(EditorGraphicAdjust::supports("brushMask"));
}

void EditorGraphicAdjustTests::geometricMasksAndLocalAdjustments() {
    QImage source(4,4,QImage::Format_ARGB32); source.fill(qRgb(64,64,64));
    const auto gradient=EditorGraphicAdjust::makeMask(source,"linearMask",{{"start.x",0.0},{"end.x",1.0}});
    QVERIFY2(gradient.ok(),qPrintable(gradient.error)); QCOMPARE(gradient.width,4); QCOMPARE(gradient.coverage.size(),std::size_t(16));
    QVERIFY(gradient.coverage.front()>gradient.coverage[3]);
    const auto radial=EditorGraphicAdjust::makeMask(source,"radialMask",{{"radiusX",.5},{"radiusY",.5},{"feather",0.0}});
    QVERIFY2(radial.ok(),qPrintable(radial.error)); QVERIFY(radial.coverage[5]>radial.coverage.front());
    const QVariantList points{QVariantMap{{"x",.5},{"y",.5}}};
    const auto brush=EditorGraphicAdjust::makeMask(source,"brushMask",{{"radius",.3},{"points",points}}); QVERIFY2(brush.ok(),qPrintable(brush.error));
    QVERIFY(std::any_of(brush.coverage.cbegin(),brush.coverage.cend(),[](float v){return v>0;}));
    QVariantList coverage; for(int i=0;i<16;++i) coverage.append(i<8 ? 1.0 : 0.0);
    const auto adjusted=EditorGraphicAdjust::process(source,"applyLocalAdjustments",{{"mask",QVariantMap{{"coverage",coverage}}},{"exposure",1.0}});
    QVERIFY2(adjusted.ok(),qPrintable(adjusted.error)); QVERIFY(qRed(adjusted.image.pixel(0,0))>64); QCOMPARE(adjusted.image.pixel(0,3),source.pixel(0,3));
    QVERIFY(!EditorGraphicAdjust::makeMask(source,"linearMask",{{"start.x",1.0},{"end.x",1.0}}).ok());
}

void EditorGraphicAdjustTests::automaticSettings() {
    const auto source=fixture();
    const auto light=EditorGraphicAdjust::process(source,"autoDevelopLight"); QVERIFY2(light.ok(),qPrintable(light.error)); QVERIFY(light.parameters.contains("exposure"));
    const auto white=EditorGraphicAdjust::process(source,"autoWhiteBalance"); QVERIFY2(white.ok(),qPrintable(white.error)); QVERIFY(white.parameters.contains("temperature"));
    const auto preset=EditorGraphicAdjust::process(source,"whiteBalancePreset",{{"preset","Tungsten"}}); QVERIFY2(preset.ok(),qPrintable(preset.error)); QCOMPARE(preset.parameters["temperature"].toDouble(),2850.0);
    const auto point=EditorGraphicAdjust::process(source,"samplePointColor",{{"sample","#DD6633"}}); QVERIFY2(point.ok(),qPrintable(point.error)); QVERIFY(point.parameters["chroma"].toDouble()>0);
    const auto bw=EditorGraphicAdjust::process(source,"autoBlackAndWhiteMix"); QVERIFY2(bw.ok(),qPrintable(bw.error)); QCOMPARE(bw.parameters["blackAndWhiteMix"].toList().size(),8);
}

void EditorGraphicAdjustTests::chromaticAberrationEstimatorReturnsOpticsSettings() {
    QImage source(128,96,QImage::Format_ARGB32);
    const auto pattern=[](double x,double y) { return qRound(255*(.5+.25*std::sin(x*.37)+.18*std::sin(y*.41))); };
    for(int y=0;y<source.height();++y) for(int x=0;x<source.width();++x) {
        const double dx=x-63.5,dy=y-47.5;
        source.setPixel(x,y,qRgba(pattern(63.5+dx*1.006,47.5+dy*1.006),pattern(x,y),pattern(63.5+dx*.994,47.5+dy*.994),255));
    }
    const auto original=source.copy(); const auto input=sdkSource(source);
    const auto estimated=GA::estimateChromaticAberration(input.view()); QVERIFY(estimated);
    QVERIFY(std::abs((*estimated)[0])+std::abs((*estimated)[1])>1);
    QVERIFY(EditorGraphicAdjust::supports("estimateChromaticAberration"));
    QVERIFY(EditorGraphicAdjust::parameterNames("estimateChromaticAberration").isEmpty());
    const auto result=EditorGraphicAdjust::process(source,"estimateChromaticAberration"); QVERIFY2(result.ok(),qPrintable(result.error));
    QCOMPARE(result.parameters["redChromaticAberration"].toDouble(),(*estimated)[0]);
    QCOMPARE(result.parameters["blueChromaticAberration"].toDouble(),(*estimated)[1]);
    QVERIFY(!result.parameters["autoChromaticAberration"].toBool());
    GA::OpticsOptions options; options.redChromaticAberration=(*estimated)[0]; options.blueChromaticAberration=(*estimated)[1];
    const auto corrected=GA::correctOptics(input.view(),options); QVERIFY(corrected);
    QCOMPARE(result.image,sdkImage(*corrected));
    QVERIFY(result.image!=original); QCOMPARE(source,original);
    QVERIFY(!EditorGraphicAdjust::process(source,"estimateChromaticAberration",{{"unknown",true}}).ok());
}

void EditorGraphicAdjustTests::lookPresetAndGalleryExecution() {
    const auto source=fixture(); const auto sdk=sdkSource(source);
    const auto expectedLook=GA::lightcraftLookCineTealAmber(sdk.view(),.6); QVERIFY(expectedLook);
    const auto look=EditorGraphicAdjust::process(source,"lightcraftLookCineTealAmber",{{"amount",.6}}); QVERIFY2(look.ok(),qPrintable(look.error)); QCOMPARE(look.image,sdkImage(*expectedLook));
    const auto expectedPreset=GA::lightcraftPresetWarmGlow(sdk.view(),.7); QVERIFY(expectedPreset);
    const auto preset=EditorGraphicAdjust::process(source,"preset.WarmGlow",{{"amount",.7}}); QVERIFY2(preset.ok(),qPrintable(preset.error)); QCOMPARE(preset.image,sdkImage(*expectedPreset));
    const QVariantList effects{QVariantMap{{"operation","filter.gallery.cutout"},{"parameters",QVariantMap{{"numberOfLevels",3.0}}}},QVariantMap{{"operation","posterEdges"},{"parameters",QVariantMap{{"edgeThickness",4.0}}}}};
    const auto stack=EditorGraphicAdjust::process(source,"filterGallery",{{"effects",effects}}); QVERIFY2(stack.ok(),qPrintable(stack.error));
    auto first=EditorGraphicAdjust::process(source,"cutout",{{"numberOfLevels",3.0}}); QVERIFY(first.ok());
    auto second=EditorGraphicAdjust::process(first.image,"posterEdges",{{"edgeThickness",4.0}}); QVERIFY(second.ok());
    for(int y=0;y<source.height();++y) for(int x=0;x<source.width();++x) {
        const auto a=stack.image.pixel(x,y),b=second.image.pixel(x,y);
        QVERIFY(std::abs(qRed(a)-qRed(b))<=2 && std::abs(qGreen(a)-qGreen(b))<=2 && std::abs(qBlue(a)-qBlue(b))<=2);
        QCOMPARE(qAlpha(a),qAlpha(source.pixel(x,y)));
    }
}

QTEST_GUILESS_MAIN(EditorGraphicAdjustTests)
#include "tst_editorgraphicadjust.moc"
