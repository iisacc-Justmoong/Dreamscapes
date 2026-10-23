#include "EditorGraphicAdjust.h"
#include <iiGraphicAdjust/iiGraphicAdjust.h>
#include <QColor>
#include <QColorSpace>
#include <QMetaType>
#include <QRegularExpression>
#include <QUrl>
#include <algorithm>
#include <cmath>
#include <deque>
#include <limits>
#include <memory>
#include <optional>
#include <string_view>
#include <type_traits>

namespace EditorGraphicAdjust {
namespace {
namespace GA = iiGraphicAdjust;
using namespace iiGraphicAdjust;
QString token(QString text) {
    if (text.contains("::")) text = text.section("::", -1);
    return text.toLower().remove(QRegularExpression("[^a-z0-9]"));
}
QString sdkError(const Error &e) { return QString::fromUtf8(e.message.data(), qsizetype(e.message.size())); }
GA::Result<Image> importImage(const QImage &source) {
    if (source.isNull()) return std::unexpected(Error{ErrorCode::InvalidDimensions, "Supply a nonempty source image"});
    QImage srgb = source;
    if (srgb.colorSpace().isValid() && srgb.colorSpace() != QColorSpace::SRgb)
        srgb = srgb.convertedToColorSpace(QColorSpace::SRgb);
    auto words = srgb.convertToFormat(QImage::Format_ARGB32);
    std::vector<std::uint32_t> pixels(std::size_t(words.width()) * words.height());
    for (int y = 0; y < words.height(); ++y)
        std::copy_n(reinterpret_cast<const std::uint32_t *>(words.constScanLine(y)), words.width(), pixels.begin() + std::size_t(y) * words.width());
    return fromArgb32(std::size_t(words.width()), std::size_t(words.height()), pixels);
}
struct Borrowed { virtual ~Borrowed() = default; };
template<class T> struct Storage final : Borrowed { std::vector<T> values; explicit Storage(std::vector<T> v) : values(std::move(v)) {} };
struct Reader {
    QString error;
    std::size_t width = 0, height = 0;
    std::deque<Image> images;
    std::vector<std::unique_ptr<Borrowed>> borrowed;
    const MaskInputs *masks = nullptr;
    bool fail(const QString &message) { if (error.isEmpty()) error = message; return false; }
    bool keys(const QVariantMap &map, const QStringList &allowed) {
        for (auto it = map.cbegin(); it != map.cend(); ++it)
            if (!allowed.contains(it.key())) return fail(QString("Unsupported parameter: %1").arg(it.key()));
        return true;
    }
    template<class T> std::span<const T> hold(std::vector<T> values) {
        auto owned = std::make_unique<Storage<T>>(std::move(values));
        const std::span<const T> view(owned->values);
        borrowed.push_back(std::move(owned)); return view;
    }
};
template<class T> bool read(Reader &, const QVariant &, T &);
template<class T> bool fields(Reader &, const QVariantMap &, T &);
template<class T> bool member(Reader &reader, const QVariantMap &map, const char *name, T &out) {
    const auto it = map.constFind(QString::fromLatin1(name));
    if (it == map.cend()) return true;
    if (read(reader, it.value(), out)) return true;
    if (reader.error.isEmpty()) reader.fail(QString("Invalid parameter: %1").arg(name));
    else reader.error = QString::fromLatin1(name) + ": " + reader.error;
    return false;
}
template<class T> bool enumValue(Reader &, const QVariant &, T &);
bool maskMember(Reader &reader, const QVariantMap &map, const char *name, MaskView &out) {
    if (reader.masks) {
        const auto &inputs = *reader.masks;
        const auto *slot = std::string_view(name) == "mask" ? &inputs.mask
            : std::string_view(name) == "base" ? &inputs.base : &inputs.operand;
        if (*slot) {
            const auto view = **slot;
            if (!view.width || !view.height || view.width > 65536 || view.height > 65536
                || view.height > std::numeric_limits<std::size_t>::max() / view.width
                || view.width * view.height != view.coverage.size()) return reader.fail("Mask dimensions must match its coverage");
            for (float value : view.coverage)
                if (!std::isfinite(value) || value < 0 || value > 1) return reader.fail("Mask coverage must be finite in [0,1]");
            out = view; return true;
        }
    }
    return member(reader, map, name, out);
}
void nativeMaskKeys(QVariantMap &parameters, const MaskInputs &inputs) {
    if (inputs.mask) parameters["mask"] = QVariant{};
    if (inputs.base) parameters["base"] = QVariant{};
    if (inputs.operand) parameters["operand"] = QVariant{};
}
bool numericVariant(const QVariant &v) {
    switch(v.metaType().id()) {
    case QMetaType::Double: case QMetaType::Float: case QMetaType::Int: case QMetaType::UInt: case QMetaType::LongLong: case QMetaType::ULongLong: case QMetaType::Short: case QMetaType::UShort: case QMetaType::Long: case QMetaType::ULong: case QMetaType::Char: case QMetaType::SChar: case QMetaType::UChar: return true;
    default: return false;
    }
}
template<> bool fields<BlendOptions>(Reader &, const QVariantMap &, BlendOptions &);
template<> bool fields<MaskPoint>(Reader &, const QVariantMap &, MaskPoint &);
template<> bool fields<LinearMaskOptions>(Reader &, const QVariantMap &, LinearMaskOptions &);
template<> bool fields<RadialMaskOptions>(Reader &, const QVariantMap &, RadialMaskOptions &);
template<> bool fields<BrushStroke>(Reader &, const QVariantMap &, BrushStroke &);
template<> bool fields<LuminanceRangeOptions>(Reader &, const QVariantMap &, LuminanceRangeOptions &);
template<> bool fields<ColorRangeOptions>(Reader &, const QVariantMap &, ColorRangeOptions &);
template<> bool fields<LocalAdjustmentOptions>(Reader &, const QVariantMap &, LocalAdjustmentOptions &);
template<> bool fields<LocalAdjustment>(Reader &, const QVariantMap &, LocalAdjustment &);
template<> bool fields<GradientStop>(Reader &, const QVariantMap &, GradientStop &);
template<> bool fields<GradientMapOptions>(Reader &, const QVariantMap &, GradientMapOptions &);
template<> bool fields<ColorLut>(Reader &, const QVariantMap &, ColorLut &);
template<> bool fields<ColorLookupOptions>(Reader &, const QVariantMap &, ColorLookupOptions &);
template<> bool fields<CameraRawWheel>(Reader &, const QVariantMap &, CameraRawWheel &);
template<> bool fields<CameraRawOptions>(Reader &, const QVariantMap &, CameraRawOptions &);
template<> bool fields<SpherizeOptions>(Reader &, const QVariantMap &, SpherizeOptions &);
template<> bool fields<WaveOptions>(Reader &, const QVariantMap &, WaveOptions &);
template<> bool fields<RippleOptions>(Reader &, const QVariantMap &, RippleOptions &);
template<> bool fields<DisplaceOptions>(Reader &, const QVariantMap &, DisplaceOptions &);
template<> bool fields<ShearPoint>(Reader &, const QVariantMap &, ShearPoint &);
template<> bool fields<ZigZagOptions>(Reader &, const QVariantMap &, ZigZagOptions &);
template<> bool fields<DevelopEffectsOptions>(Reader &, const QVariantMap &, DevelopEffectsOptions &);
template<> bool fields<PostCropVignetteOptions>(Reader &, const QVariantMap &, PostCropVignetteOptions &);
template<> bool fields<FilmGrainOptions>(Reader &, const QVariantMap &, FilmGrainOptions &);
template<> bool fields<DevelopDetailOptions>(Reader &, const QVariantMap &, DevelopDetailOptions &);
template<> bool fields<HueSaturationOptions>(Reader &, const QVariantMap &, HueSaturationOptions &);
template<> bool fields<VibranceOptions>(Reader &, const QVariantMap &, VibranceOptions &);
template<> bool fields<ToneBalance>(Reader &, const QVariantMap &, ToneBalance &);
template<> bool fields<ColorBalanceOptions>(Reader &, const QVariantMap &, ColorBalanceOptions &);
template<> bool fields<BlackWhiteOptions>(Reader &, const QVariantMap &, BlackWhiteOptions &);
template<> bool fields<PhotoFilterOptions>(Reader &, const QVariantMap &, PhotoFilterOptions &);
template<> bool fields<ChannelMix>(Reader &, const QVariantMap &, ChannelMix &);
template<> bool fields<ChannelMixerOptions>(Reader &, const QVariantMap &, ChannelMixerOptions &);
template<> bool fields<SelectiveColorAdjustment>(Reader &, const QVariantMap &, SelectiveColorAdjustment &);
template<> bool fields<SelectiveColorOptions>(Reader &, const QVariantMap &, SelectiveColorOptions &);
template<> bool fields<ReplaceColorOptions>(Reader &, const QVariantMap &, ReplaceColorOptions &);
template<> bool fields<MatchColorOptions>(Reader &, const QVariantMap &, MatchColorOptions &);
template<> bool fields<GalleryColoredPencilOptions>(Reader &, const QVariantMap &, GalleryColoredPencilOptions &);
template<> bool fields<GalleryCutoutOptions>(Reader &, const QVariantMap &, GalleryCutoutOptions &);
template<> bool fields<GalleryDryBrushOptions>(Reader &, const QVariantMap &, GalleryDryBrushOptions &);
template<> bool fields<GalleryFilmGrainOptions>(Reader &, const QVariantMap &, GalleryFilmGrainOptions &);
template<> bool fields<GalleryFrescoOptions>(Reader &, const QVariantMap &, GalleryFrescoOptions &);
template<> bool fields<GalleryNeonGlowOptions>(Reader &, const QVariantMap &, GalleryNeonGlowOptions &);
template<> bool fields<GalleryPaintDaubsOptions>(Reader &, const QVariantMap &, GalleryPaintDaubsOptions &);
template<> bool fields<GalleryPaletteKnifeOptions>(Reader &, const QVariantMap &, GalleryPaletteKnifeOptions &);
template<> bool fields<GalleryPlasticWrapOptions>(Reader &, const QVariantMap &, GalleryPlasticWrapOptions &);
template<> bool fields<GalleryPosterEdgesOptions>(Reader &, const QVariantMap &, GalleryPosterEdgesOptions &);
template<> bool fields<GalleryRoughPastelsOptions>(Reader &, const QVariantMap &, GalleryRoughPastelsOptions &);
template<> bool fields<GallerySmudgeStickOptions>(Reader &, const QVariantMap &, GallerySmudgeStickOptions &);
template<> bool fields<GallerySpongeOptions>(Reader &, const QVariantMap &, GallerySpongeOptions &);
template<> bool fields<GalleryUnderpaintingOptions>(Reader &, const QVariantMap &, GalleryUnderpaintingOptions &);
template<> bool fields<GalleryWatercolorOptions>(Reader &, const QVariantMap &, GalleryWatercolorOptions &);
template<> bool fields<GalleryAccentedEdgesOptions>(Reader &, const QVariantMap &, GalleryAccentedEdgesOptions &);
template<> bool fields<GalleryAngledStrokesOptions>(Reader &, const QVariantMap &, GalleryAngledStrokesOptions &);
template<> bool fields<GalleryCrosshatchOptions>(Reader &, const QVariantMap &, GalleryCrosshatchOptions &);
template<> bool fields<GalleryDarkStrokesOptions>(Reader &, const QVariantMap &, GalleryDarkStrokesOptions &);
template<> bool fields<GalleryInkOutlinesOptions>(Reader &, const QVariantMap &, GalleryInkOutlinesOptions &);
template<> bool fields<GallerySpatterOptions>(Reader &, const QVariantMap &, GallerySpatterOptions &);
template<> bool fields<GallerySprayedStrokesOptions>(Reader &, const QVariantMap &, GallerySprayedStrokesOptions &);
template<> bool fields<GallerySumiEOptions>(Reader &, const QVariantMap &, GallerySumiEOptions &);
template<> bool fields<GalleryDiffuseGlowOptions>(Reader &, const QVariantMap &, GalleryDiffuseGlowOptions &);
template<> bool fields<GalleryGlassOptions>(Reader &, const QVariantMap &, GalleryGlassOptions &);
template<> bool fields<GalleryOceanRippleOptions>(Reader &, const QVariantMap &, GalleryOceanRippleOptions &);
template<> bool fields<GalleryBasReliefOptions>(Reader &, const QVariantMap &, GalleryBasReliefOptions &);
template<> bool fields<GalleryChalkCharcoalOptions>(Reader &, const QVariantMap &, GalleryChalkCharcoalOptions &);
template<> bool fields<GalleryCharcoalOptions>(Reader &, const QVariantMap &, GalleryCharcoalOptions &);
template<> bool fields<GalleryChromeOptions>(Reader &, const QVariantMap &, GalleryChromeOptions &);
template<> bool fields<GalleryConteCrayonOptions>(Reader &, const QVariantMap &, GalleryConteCrayonOptions &);
template<> bool fields<GalleryGraphicPenOptions>(Reader &, const QVariantMap &, GalleryGraphicPenOptions &);
template<> bool fields<GalleryHalftonePatternOptions>(Reader &, const QVariantMap &, GalleryHalftonePatternOptions &);
template<> bool fields<GalleryNotePaperOptions>(Reader &, const QVariantMap &, GalleryNotePaperOptions &);
template<> bool fields<GalleryPhotocopyOptions>(Reader &, const QVariantMap &, GalleryPhotocopyOptions &);
template<> bool fields<GalleryPlasterOptions>(Reader &, const QVariantMap &, GalleryPlasterOptions &);
template<> bool fields<GalleryReticulationOptions>(Reader &, const QVariantMap &, GalleryReticulationOptions &);
template<> bool fields<GalleryStampOptions>(Reader &, const QVariantMap &, GalleryStampOptions &);
template<> bool fields<GalleryTornEdgesOptions>(Reader &, const QVariantMap &, GalleryTornEdgesOptions &);
template<> bool fields<GalleryWaterPaperOptions>(Reader &, const QVariantMap &, GalleryWaterPaperOptions &);
template<> bool fields<GalleryGlowingEdgesOptions>(Reader &, const QVariantMap &, GalleryGlowingEdgesOptions &);
template<> bool fields<GalleryCraquelureOptions>(Reader &, const QVariantMap &, GalleryCraquelureOptions &);
template<> bool fields<GalleryGrainOptions>(Reader &, const QVariantMap &, GalleryGrainOptions &);
template<> bool fields<GalleryMosaicTilesOptions>(Reader &, const QVariantMap &, GalleryMosaicTilesOptions &);
template<> bool fields<GalleryPatchworkOptions>(Reader &, const QVariantMap &, GalleryPatchworkOptions &);
template<> bool fields<GalleryStainedGlassOptions>(Reader &, const QVariantMap &, GalleryStainedGlassOptions &);
template<> bool fields<GalleryTexturizerOptions>(Reader &, const QVariantMap &, GalleryTexturizerOptions &);
template<> bool fields<SpotOptions>(Reader &, const QVariantMap &, SpotOptions &);
template<> bool fields<SpotSourceOptions>(Reader &, const QVariantMap &, SpotSourceOptions &);
template<> bool fields<EyeOptions>(Reader &, const QVariantMap &, EyeOptions &);
template<> bool fields<VisualizeSpotsOptions>(Reader &, const QVariantMap &, VisualizeSpotsOptions &);
template<> bool fields<DustDetectionOptions>(Reader &, const QVariantMap &, DustDetectionOptions &);
template<> bool fields<SharpenOptions>(Reader &, const QVariantMap &, SharpenOptions &);
template<> bool fields<NoiseOptions>(Reader &, const QVariantMap &, NoiseOptions &);
template<> bool fields<ColorAdjustment>(Reader &, const QVariantMap &, ColorAdjustment &);
template<> bool fields<MixerAdjustment>(Reader &, const QVariantMap &, MixerAdjustment &);
template<> bool fields<ColorWheel>(Reader &, const QVariantMap &, ColorWheel &);
template<> bool fields<ColorGradingOptions>(Reader &, const QVariantMap &, ColorGradingOptions &);
template<> bool fields<PointColorOptions>(Reader &, const QVariantMap &, PointColorOptions &);
template<> bool fields<DevelopColorOptions>(Reader &, const QVariantMap &, DevelopColorOptions &);
template<> bool fields<CalibrationOptions>(Reader &, const QVariantMap &, CalibrationOptions &);
template<> bool fields<HdrRgba>(Reader &, const QVariantMap &, HdrRgba &);
template<> bool fields<HdrToningOptions>(Reader &, const QVariantMap &, HdrToningOptions &);
template<> bool fields<CloudsOptions>(Reader &, const QVariantMap &, CloudsOptions &);
template<> bool fields<FibersOptions>(Reader &, const QVariantMap &, FibersOptions &);
template<> bool fields<LensFlareOptions>(Reader &, const QVariantMap &, LensFlareOptions &);
template<> bool fields<FilterLight>(Reader &, const QVariantMap &, FilterLight &);
template<> bool fields<LightingEffectsOptions>(Reader &, const QVariantMap &, LightingEffectsOptions &);
template<> bool fields<RelightOptions>(Reader &, const QVariantMap &, RelightOptions &);
template<> bool fields<FlameOptions>(Reader &, const QVariantMap &, FlameOptions &);
template<> bool fields<TreeOptions>(Reader &, const QVariantMap &, TreeOptions &);
template<> bool fields<PictureFrameOptions>(Reader &, const QVariantMap &, PictureFrameOptions &);
template<> bool fields<BrightnessContrastOptions>(Reader &, const QVariantMap &, BrightnessContrastOptions &);
template<> bool fields<LevelChannel>(Reader &, const QVariantMap &, LevelChannel &);
template<> bool fields<LevelsOptions>(Reader &, const QVariantMap &, LevelsOptions &);
template<> bool fields<CurvePoint>(Reader &, const QVariantMap &, CurvePoint &);
template<> bool fields<CurveChannel>(Reader &, const QVariantMap &, CurveChannel &);
template<> bool fields<CurvesOptions>(Reader &, const QVariantMap &, CurvesOptions &);
template<> bool fields<ExposureOptions>(Reader &, const QVariantMap &, ExposureOptions &);
template<> bool fields<ShadowsHighlightsOptions>(Reader &, const QVariantMap &, ShadowsHighlightsOptions &);
template<> bool fields<AutoToneOptions>(Reader &, const QVariantMap &, AutoToneOptions &);
template<> bool fields<MotionBlurOptions>(Reader &, const QVariantMap &, MotionBlurOptions &);
template<> bool fields<RadialBlurOptions>(Reader &, const QVariantMap &, RadialBlurOptions &);
template<> bool fields<SurfaceBlurOptions>(Reader &, const QVariantMap &, SurfaceBlurOptions &);
template<> bool fields<UnsharpMaskOptions>(Reader &, const QVariantMap &, UnsharpMaskOptions &);
template<> bool fields<SmartSharpenOptions>(Reader &, const QVariantMap &, SmartSharpenOptions &);
template<> bool fields<SmartBlurOptions>(Reader &, const QVariantMap &, SmartBlurOptions &);
template<> bool fields<ShapeBlurOptions>(Reader &, const QVariantMap &, ShapeBlurOptions &);
template<> bool fields<LensBlurOptions>(Reader &, const QVariantMap &, LensBlurOptions &);
template<> bool fields<TiltShiftOptions>(Reader &, const QVariantMap &, TiltShiftOptions &);
template<> bool fields<IrisBlurPin>(Reader &, const QVariantMap &, IrisBlurPin &);
template<> bool fields<FieldBlurPin>(Reader &, const QVariantMap &, FieldBlurPin &);
template<> bool fields<SpinBlurPin>(Reader &, const QVariantMap &, SpinBlurPin &);
template<> bool fields<BlurPathPoint>(Reader &, const QVariantMap &, BlurPathPoint &);
template<> bool fields<BlurPath>(Reader &, const QVariantMap &, BlurPath &);
template<> bool fields<WhiteBalanceOptions>(Reader &, const QVariantMap &, WhiteBalanceOptions &);
template<> bool fields<DevelopLightOptions>(Reader &, const QVariantMap &, DevelopLightOptions &);
template<> bool fields<DevelopCurveOptions>(Reader &, const QVariantMap &, DevelopCurveOptions &);
template<> bool fields<FilterNoiseOptions>(Reader &, const QVariantMap &, FilterNoiseOptions &);
template<> bool fields<DustAndScratchesOptions>(Reader &, const QVariantMap &, DustAndScratchesOptions &);
template<> bool fields<MorphologyOptions>(Reader &, const QVariantMap &, MorphologyOptions &);
template<> bool fields<ReduceNoiseOptions>(Reader &, const QVariantMap &, ReduceNoiseOptions &);
template<> bool fields<CustomFilterOptions>(Reader &, const QVariantMap &, CustomFilterOptions &);
template<> bool fields<GuidedBlurOptions>(Reader &, const QVariantMap &, GuidedBlurOptions &);
template<> bool fields<LiquifyPoint>(Reader &, const QVariantMap &, LiquifyPoint &);
template<> bool fields<LiquifyStroke>(Reader &, const QVariantMap &, LiquifyStroke &);
template<> bool fields<LiquifyOptions>(Reader &, const QVariantMap &, LiquifyOptions &);
template<> bool fields<WideAngleConstraint>(Reader &, const QVariantMap &, WideAngleConstraint &);
template<> bool fields<AdaptiveWideAngleOptions>(Reader &, const QVariantMap &, AdaptiveWideAngleOptions &);
template<> bool fields<PuppetPin>(Reader &, const QVariantMap &, PuppetPin &);
template<> bool fields<PuppetWarpOptions>(Reader &, const QVariantMap &, PuppetWarpOptions &);
template<> bool fields<PerspectivePlane>(Reader &, const QVariantMap &, PerspectivePlane &);
template<> bool fields<PerspectiveWarpOptions>(Reader &, const QVariantMap &, PerspectiveWarpOptions &);
template<> bool fields<VanishingPlane>(Reader &, const QVariantMap &, VanishingPlane &);
template<> bool fields<VanishingTearOff>(Reader &, const QVariantMap &, VanishingTearOff &);
template<> bool fields<VanishingPaste>(Reader &, const QVariantMap &, VanishingPaste &);
template<> bool fields<VanishingCloneStroke>(Reader &, const QVariantMap &, VanishingCloneStroke &);
template<> bool fields<VanishingPointOptions>(Reader &, const QVariantMap &, VanishingPointOptions &);
template<> bool fields<MaskOverlayOptions>(Reader &, const QVariantMap &, MaskOverlayOptions &);
template<> bool fields<GamutWarningOptions>(Reader &, const QVariantMap &, GamutWarningOptions &);
template<> bool fields<DevelopOptions>(Reader &, const QVariantMap &, DevelopOptions &);
template<> bool fields<LensFilterProfile>(Reader &, const QVariantMap &, LensFilterProfile &);
template<> bool fields<LensCorrectionOptions>(Reader &, const QVariantMap &, LensCorrectionOptions &);
template<> bool fields<EmbeddedLensWarp>(Reader &, const QVariantMap &, EmbeddedLensWarp &);
template<> bool fields<EmbeddedLensVignette>(Reader &, const QVariantMap &, EmbeddedLensVignette &);
template<> bool fields<OpticsOptions>(Reader &, const QVariantMap &, OpticsOptions &);
template<> bool fields<ColorHalftoneOptions>(Reader &, const QVariantMap &, ColorHalftoneOptions &);
template<> bool fields<CrystallizeOptions>(Reader &, const QVariantMap &, CrystallizeOptions &);
template<> bool fields<MezzotintOptions>(Reader &, const QVariantMap &, MezzotintOptions &);
template<> bool fields<PointillizeOptions>(Reader &, const QVariantMap &, PointillizeOptions &);
template<> bool fields<EmbossOptions>(Reader &, const QVariantMap &, EmbossOptions &);
template<> bool fields<DiffuseOptions>(Reader &, const QVariantMap &, DiffuseOptions &);
template<> bool fields<ExtrudeOptions>(Reader &, const QVariantMap &, ExtrudeOptions &);
template<> bool fields<OilPaintOptions>(Reader &, const QVariantMap &, OilPaintOptions &);
template<> bool fields<TilesOptions>(Reader &, const QVariantMap &, TilesOptions &);
template<> bool fields<TraceContourOptions>(Reader &, const QVariantMap &, TraceContourOptions &);
template<> bool fields<WindOptions>(Reader &, const QVariantMap &, WindOptions &);
template<> bool enumValue<BlendMode>(Reader &, const QVariant &, BlendMode &);
template<> bool enumValue<BlendSpace>(Reader &, const QVariant &, BlendSpace &);
template<> bool enumValue<MaskCombine>(Reader &, const QVariant &, MaskCombine &);
template<> bool enumValue<LutKind>(Reader &, const QVariant &, LutKind &);
template<> bool enumValue<LutInterpolation>(Reader &, const QVariant &, LutInterpolation &);
template<> bool enumValue<CameraRawVignetteStyle>(Reader &, const QVariant &, CameraRawVignetteStyle &);
template<> bool enumValue<FilterEdge>(Reader &, const QVariant &, FilterEdge &);
template<> bool enumValue<SpherizeMode>(Reader &, const QVariant &, SpherizeMode &);
template<> bool enumValue<WaveType>(Reader &, const QVariant &, WaveType &);
template<> bool enumValue<RippleSize>(Reader &, const QVariant &, RippleSize &);
template<> bool enumValue<PolarMode>(Reader &, const QVariant &, PolarMode &);
template<> bool enumValue<ZigZagStyle>(Reader &, const QVariant &, ZigZagStyle &);
template<> bool enumValue<VignetteStyle>(Reader &, const QVariant &, VignetteStyle &);
template<> bool enumValue<HueRange>(Reader &, const QVariant &, HueRange &);
template<> bool enumValue<ColorRange>(Reader &, const QVariant &, ColorRange &);
template<> bool enumValue<SelectiveColorMethod>(Reader &, const QVariant &, SelectiveColorMethod &);
template<> bool enumValue<GalleryBrushType>(Reader &, const QVariant &, GalleryBrushType &);
template<> bool enumValue<GalleryTexture>(Reader &, const QVariant &, GalleryTexture &);
template<> bool enumValue<GalleryLightDirection>(Reader &, const QVariant &, GalleryLightDirection &);
template<> bool enumValue<GalleryStrokeDirection>(Reader &, const QVariant &, GalleryStrokeDirection &);
template<> bool enumValue<GalleryGlassTexture>(Reader &, const QVariant &, GalleryGlassTexture &);
template<> bool enumValue<GalleryPatternType>(Reader &, const QVariant &, GalleryPatternType &);
template<> bool enumValue<GalleryGrainType>(Reader &, const QVariant &, GalleryGrainType &);
template<> bool enumValue<EyeKind>(Reader &, const QVariant &, EyeKind &);
template<> bool enumValue<NoiseDistribution>(Reader &, const QVariant &, NoiseDistribution &);
template<> bool enumValue<MixerBand>(Reader &, const QVariant &, MixerBand &);
template<> bool enumValue<HdrToningMethod>(Reader &, const QVariant &, HdrToningMethod &);
template<> bool enumValue<FlareLens>(Reader &, const QVariant &, FlareLens &);
template<> bool enumValue<LightKind>(Reader &, const QVariant &, LightKind &);
template<> bool enumValue<TextureChannel>(Reader &, const QVariant &, TextureChannel &);
template<> bool enumValue<FlameType>(Reader &, const QVariant &, FlameType &);
template<> bool enumValue<FlameStyle>(Reader &, const QVariant &, FlameStyle &);
template<> bool enumValue<FlameShape>(Reader &, const QVariant &, FlameShape &);
template<> bool enumValue<PictureFrameStyle>(Reader &, const QVariant &, PictureFrameStyle &);
template<> bool enumValue<RadialBlurMethod>(Reader &, const QVariant &, RadialBlurMethod &);
template<> bool enumValue<FilterBlurQuality>(Reader &, const QVariant &, FilterBlurQuality &);
template<> bool enumValue<SmartBlurMode>(Reader &, const QVariant &, SmartBlurMode &);
template<> bool enumValue<BlurShape>(Reader &, const QVariant &, BlurShape &);
template<> bool enumValue<LensDepthSource>(Reader &, const QVariant &, LensDepthSource &);
template<> bool enumValue<WhiteBalancePreset>(Reader &, const QVariant &, WhiteBalancePreset &);
template<> bool enumValue<MorphologyShape>(Reader &, const QVariant &, MorphologyShape &);
template<> bool enumValue<FilterColorModel>(Reader &, const QVariant &, FilterColorModel &);
template<> bool enumValue<MeshInterpolation>(Reader &, const QVariant &, MeshInterpolation &);
template<> bool enumValue<LiquifyTool>(Reader &, const QVariant &, LiquifyTool &);
template<> bool enumValue<WideAngleModel>(Reader &, const QVariant &, WideAngleModel &);
template<> bool enumValue<WideAngleOrientation>(Reader &, const QVariant &, WideAngleOrientation &);
template<> bool enumValue<PuppetMode>(Reader &, const QVariant &, PuppetMode &);
template<> bool enumValue<PuppetDensity>(Reader &, const QVariant &, PuppetDensity &);
template<> bool enumValue<PerspectiveStraighten>(Reader &, const QVariant &, PerspectiveStraighten &);
template<> bool enumValue<VanishingEdge>(Reader &, const QVariant &, VanishingEdge &);
template<> bool enumValue<DiagnosticColorSpace>(Reader &, const QVariant &, DiagnosticColorSpace &);
template<> bool enumValue<LightcraftLook>(Reader &, const QVariant &, LightcraftLook &);
template<> bool enumValue<LightcraftPreset>(Reader &, const QVariant &, LightcraftPreset &);
template<> bool enumValue<ErrorCode>(Reader &, const QVariant &, ErrorCode &);
template<> bool enumValue<LensFilterEdge>(Reader &, const QVariant &, LensFilterEdge &);
template<> bool enumValue<MezzotintPattern>(Reader &, const QVariant &, MezzotintPattern &);
template<> bool enumValue<DiffuseMode>(Reader &, const QVariant &, DiffuseMode &);
template<> bool enumValue<ExtrudeShape>(Reader &, const QVariant &, ExtrudeShape &);
template<> bool enumValue<TileFill>(Reader &, const QVariant &, TileFill &);
template<> bool enumValue<WindMethod>(Reader &, const QVariant &, WindMethod &);

template<class T> struct ArrayType : std::false_type {};
template<class T, std::size_t N> struct ArrayType<std::array<T,N>> : std::true_type { using element = T; static constexpr auto size = N; };
template<class T> struct VectorType : std::false_type {};
template<class T> struct VectorType<std::vector<T>> : std::true_type { using element = T; };
template<class T> struct SpanType : std::false_type {};
template<class T, std::size_t N> struct SpanType<std::span<T,N>> : std::true_type { using element = std::remove_const_t<T>; };
template<class T> struct OptionalType : std::false_type {};
template<class T> struct OptionalType<std::optional<T>> : std::true_type { using element = T; };
QVariantList sequence(const QVariant &v) { return v.toList(); }
QVariantMap nested(const QVariantMap &input) {
    QVariantMap result;
    QVariantMap expanded;
    for (auto it = input.cbegin(); it != input.cend(); ++it) {
        auto path=it.key(); path.replace(QRegularExpression("\\[([0-9]+)\\]"),".\\1"); expanded[path]=it.value();
    }
    for (auto it = expanded.cbegin(); it != expanded.cend(); ++it) {
        const auto dot = it.key().indexOf('.');
        if (dot < 0) { result[it.key()] = it.value(); continue; }
        const auto head = it.key().left(dot);
        auto child = result.value(head).toMap(); child[it.key().mid(dot + 1)] = it.value(); result[head] = child;
    }
    for (auto it = result.begin(); it != result.end(); ++it)
        if (it.value().metaType().id() == QMetaType::QVariantMap) it.value() = nested(it.value().toMap());
    return result;
}
bool colorValue(const QVariant &v, Rgba &out) {
    if (v.metaType().id() != QMetaType::QString && v.metaType().id() != QMetaType::QColor) return false;
    const QColor c = v.metaType().id() == QMetaType::QColor ? v.value<QColor>() : QColor(v.toString());
    if (!c.isValid()) return false;
    out = {float(c.redF()),float(c.greenF()),float(c.blueF()),float(c.alphaF())}; return true;
}
bool galleryValue(Reader &, const QVariant &, GalleryEffect &);
template<class T> bool read(Reader &reader, const QVariant &v, T &out) {
    if constexpr (std::is_same_v<T,bool>) {
        if (v.metaType().id() != QMetaType::Bool) return reader.fail("Expected a boolean");
        out = v.toBool(); return true;
    } else if constexpr (std::is_arithmetic_v<T>) {
        const auto id = v.metaType().id();
        if (id == QMetaType::Bool || id == QMetaType::QString || id == QMetaType::QVariantMap || id == QMetaType::QVariantList || !v.isValid()) return reader.fail("Expected a finite number");
        bool ok = false; const double n = v.toDouble(&ok);
        if (!ok || !std::isfinite(n)) return reader.fail("Expected a finite number");
        if constexpr (std::is_integral_v<T>)
            if (std::trunc(n) != n || static_cast<long double>(n) < static_cast<long double>(std::numeric_limits<T>::lowest()) || static_cast<long double>(n) > static_cast<long double>(std::numeric_limits<T>::max())) return reader.fail("Expected a bounded integer");
        out = T(n); return true;
    } else if constexpr (std::is_enum_v<T>) return enumValue(reader,v,out);
    else if constexpr (std::is_same_v<T,ImageView>) {
        QImage image;
        if (v.metaType().id() == QMetaType::QImage) image = v.value<QImage>();
        else {
            const auto map = v.toMap();
            if(v.metaType().id()==QMetaType::QVariantMap && !reader.keys(map,{"source","path","image"})) return false;
            auto source = map.isEmpty() ? v : map.value("source", map.value("path", map.value("image")));
            if (source.metaType().id() == QMetaType::QImage) image = source.value<QImage>();
            else {
                const QUrl url(source.toString());
                if (!url.isLocalFile() && !url.scheme().isEmpty()) return reader.fail("Supply a local image path or QImage");
                image.load(url.isLocalFile() ? url.toLocalFile() : source.toString());
            }
        }
        auto converted = importImage(image);
        if (!converted) return reader.fail(sdkError(converted.error()));
        reader.images.push_back(std::move(*converted)); out = reader.images.back().view(); return true;
    } else if constexpr (std::is_same_v<T,MaskView>) {
        const auto map = v.toMap(); std::size_t width = reader.width, height = reader.height;
        std::vector<float> coverage;
        if (!reader.keys(map,{"width","height","coverage"}) || !member(reader,map,"width",width) || !member(reader,map,"height",height)) return false;
        if (!map.contains("coverage") || !read(reader,map.value("coverage"),coverage)) return reader.fail("Supply mask coverage");
        if (width == 0 || height == 0 || width > 65536 || height > 65536 || width * height != coverage.size()) return reader.fail("Mask dimensions must match its coverage");
        for (float value : coverage) if (value < 0 || value > 1) return reader.fail("Mask coverage must be in [0,1]");
        out = {width,height,reader.hold(std::move(coverage))}; return true;
    } else if constexpr (std::is_same_v<T,Rgba>) {
        if (colorValue(v,out)) return true;
        if (v.metaType().id() == QMetaType::QVariantMap) {
            const auto map = v.toMap();
            return reader.keys(map,{"r","g","b","a"}) && member(reader,map,"r",out.r) && member(reader,map,"g",out.g) && member(reader,map,"b",out.b) && member(reader,map,"a",out.a);
        }
        const auto list = sequence(v);
        if (list.size()!=3 && list.size()!=4) return reader.fail("Expected RGBA, RGB or a color string");
        return read(reader,list[0],out.r) && read(reader,list[1],out.g) && read(reader,list[2],out.b) && (list.size()==3 || read(reader,list[3],out.a));
    } else if constexpr (std::is_same_v<T,GalleryEffect>) return galleryValue(reader,v,out);
    else if constexpr (ArrayType<T>::value) {
        if(v.metaType().id()==QMetaType::QVariantMap) {
            const auto map=v.toMap();
            for(auto it=map.cbegin();it!=map.cend();++it) { bool ok=false; const auto i=it.key().toULongLong(&ok); if(!ok || i>=ArrayType<T>::size || !read(reader,it.value(),out[std::size_t(i)])) return reader.fail("Invalid indexed array parameter"); }
            return true;
        }
        auto list = sequence(v);
        if constexpr (std::is_arithmetic_v<typename ArrayType<T>::element> && (ArrayType<T>::size == 3 || ArrayType<T>::size == 4)) {
            Rgba c;
            if (colorValue(v,c)) { list = {c.r,c.g,c.b}; if constexpr (ArrayType<T>::size==4) list.append(c.a); }
        }
        if (list.size()!=qsizetype(ArrayType<T>::size)) return reader.fail("Array length does not match the SDK option");
        for (std::size_t i=0;i<ArrayType<T>::size;++i) if (!read(reader,list[qsizetype(i)],out[i])) return false;
        return true;
    } else if constexpr (VectorType<T>::value || SpanType<T>::value) {
        if constexpr(VectorType<T>::value) {
            if(v.metaType().id()==QMetaType::QVariantMap) {
                const auto map=v.toMap();
                for(auto it=map.cbegin();it!=map.cend();++it) { bool ok=false; const auto i=it.key().toULongLong(&ok); if(!ok || i>=65536) return reader.fail("Invalid indexed vector parameter"); if(i>=out.size()) out.resize(std::size_t(i)+1); if(!read(reader,it.value(),out[std::size_t(i)])) return false; }
                return true;
            }
        }
        if (v.metaType().id()!=QMetaType::QVariantList && !v.canConvert<QVariantList>()) return reader.fail("Expected an array");
        const auto list=sequence(v);
        if (list.size()>64*1024*1024) return reader.fail("Array exceeds the SDK image limit");
        if constexpr (VectorType<T>::value) {
            out.clear(); out.reserve(std::size_t(list.size()));
            for (const auto &value:list) { typename VectorType<T>::element entry{}; if (!read(reader,value,entry)) return false; out.push_back(std::move(entry)); }
        } else {
            std::vector<typename SpanType<T>::element> values;
            if (!read(reader,v,values)) return false;
            out = reader.hold(std::move(values));
        }
        return true;
    } else if constexpr (OptionalType<T>::value) {
        if (!v.isValid() || v.isNull()) { out.reset(); return true; }
        typename OptionalType<T>::element value{};
        if (!read(reader,v,value)) return false; out=std::move(value); return true;
    } else {
        if (v.metaType().id()!=QMetaType::QVariantMap) return reader.fail("Expected an option object");
        return fields(reader,v.toMap(),out);
    }
}
template<> bool enumValue<BlendMode>(Reader &r,const QVariant &v,BlendMode &out) {
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="normal") || (numericVariant(v) && v.toDouble()==0)) { out=BlendMode::Normal; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="multiply") || (numericVariant(v) && v.toDouble()==1)) { out=BlendMode::Multiply; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="screen") || (numericVariant(v) && v.toDouble()==2)) { out=BlendMode::Screen; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="overlay") || (numericVariant(v) && v.toDouble()==3)) { out=BlendMode::Overlay; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="darken") || (numericVariant(v) && v.toDouble()==4)) { out=BlendMode::Darken; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="lighten") || (numericVariant(v) && v.toDouble()==5)) { out=BlendMode::Lighten; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="softlight") || (numericVariant(v) && v.toDouble()==6)) { out=BlendMode::SoftLight; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="difference") || (numericVariant(v) && v.toDouble()==7)) { out=BlendMode::Difference; return true; }
    return r.fail("Unknown BlendMode value");
}
template<> bool enumValue<BlendSpace>(Reader &r,const QVariant &v,BlendSpace &out) {
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="srgb") || (numericVariant(v) && v.toDouble()==0)) { out=BlendSpace::Srgb; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="linearsrgb") || (numericVariant(v) && v.toDouble()==1)) { out=BlendSpace::LinearSrgb; return true; }
    return r.fail("Unknown BlendSpace value");
}
template<> bool enumValue<MaskCombine>(Reader &r,const QVariant &v,MaskCombine &out) {
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="add") || (numericVariant(v) && v.toDouble()==0)) { out=MaskCombine::Add; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="subtract") || (numericVariant(v) && v.toDouble()==1)) { out=MaskCombine::Subtract; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="intersect") || (numericVariant(v) && v.toDouble()==2)) { out=MaskCombine::Intersect; return true; }
    return r.fail("Unknown MaskCombine value");
}
template<> bool enumValue<LutKind>(Reader &r,const QVariant &v,LutKind &out) {
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="onedimensional") || (numericVariant(v) && v.toDouble()==0)) { out=LutKind::OneDimensional; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="threedimensional") || (numericVariant(v) && v.toDouble()==1)) { out=LutKind::ThreeDimensional; return true; }
    return r.fail("Unknown LutKind value");
}
template<> bool enumValue<LutInterpolation>(Reader &r,const QVariant &v,LutInterpolation &out) {
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="trilinear") || (numericVariant(v) && v.toDouble()==0)) { out=LutInterpolation::Trilinear; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="tetrahedral") || (numericVariant(v) && v.toDouble()==1)) { out=LutInterpolation::Tetrahedral; return true; }
    return r.fail("Unknown LutInterpolation value");
}
template<> bool enumValue<CameraRawVignetteStyle>(Reader &r,const QVariant &v,CameraRawVignetteStyle &out) {
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="highlightpriority") || (numericVariant(v) && v.toDouble()==0)) { out=CameraRawVignetteStyle::HighlightPriority; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="colorpriority") || (numericVariant(v) && v.toDouble()==1)) { out=CameraRawVignetteStyle::ColorPriority; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="paintoverlay") || (numericVariant(v) && v.toDouble()==2)) { out=CameraRawVignetteStyle::PaintOverlay; return true; }
    return r.fail("Unknown CameraRawVignetteStyle value");
}
template<> bool enumValue<FilterEdge>(Reader &r,const QVariant &v,FilterEdge &out) {
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="wrap") || (numericVariant(v) && v.toDouble()==0)) { out=FilterEdge::Wrap; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="repeat") || (numericVariant(v) && v.toDouble()==1)) { out=FilterEdge::Repeat; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="transparent") || (numericVariant(v) && v.toDouble()==2)) { out=FilterEdge::Transparent; return true; }
    return r.fail("Unknown FilterEdge value");
}
template<> bool enumValue<SpherizeMode>(Reader &r,const QVariant &v,SpherizeMode &out) {
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="normal") || (numericVariant(v) && v.toDouble()==0)) { out=SpherizeMode::Normal; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="horizontalonly") || (numericVariant(v) && v.toDouble()==1)) { out=SpherizeMode::HorizontalOnly; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="verticalonly") || (numericVariant(v) && v.toDouble()==2)) { out=SpherizeMode::VerticalOnly; return true; }
    return r.fail("Unknown SpherizeMode value");
}
template<> bool enumValue<WaveType>(Reader &r,const QVariant &v,WaveType &out) {
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="sine") || (numericVariant(v) && v.toDouble()==0)) { out=WaveType::Sine; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="triangle") || (numericVariant(v) && v.toDouble()==1)) { out=WaveType::Triangle; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="square") || (numericVariant(v) && v.toDouble()==2)) { out=WaveType::Square; return true; }
    return r.fail("Unknown WaveType value");
}
template<> bool enumValue<RippleSize>(Reader &r,const QVariant &v,RippleSize &out) {
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="small") || (numericVariant(v) && v.toDouble()==0)) { out=RippleSize::Small; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="medium") || (numericVariant(v) && v.toDouble()==1)) { out=RippleSize::Medium; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="large") || (numericVariant(v) && v.toDouble()==2)) { out=RippleSize::Large; return true; }
    return r.fail("Unknown RippleSize value");
}
template<> bool enumValue<PolarMode>(Reader &r,const QVariant &v,PolarMode &out) {
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="rectangulartopolar") || (numericVariant(v) && v.toDouble()==0)) { out=PolarMode::RectangularToPolar; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="polartorectangular") || (numericVariant(v) && v.toDouble()==1)) { out=PolarMode::PolarToRectangular; return true; }
    return r.fail("Unknown PolarMode value");
}
template<> bool enumValue<ZigZagStyle>(Reader &r,const QVariant &v,ZigZagStyle &out) {
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="aroundcenter") || (numericVariant(v) && v.toDouble()==0)) { out=ZigZagStyle::AroundCenter; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="outfromcenter") || (numericVariant(v) && v.toDouble()==1)) { out=ZigZagStyle::OutFromCenter; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="pondripples") || (numericVariant(v) && v.toDouble()==2)) { out=ZigZagStyle::PondRipples; return true; }
    return r.fail("Unknown ZigZagStyle value");
}
template<> bool enumValue<VignetteStyle>(Reader &r,const QVariant &v,VignetteStyle &out) {
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="highlightpriority") || (numericVariant(v) && v.toDouble()==0)) { out=VignetteStyle::HighlightPriority; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="colorpriority") || (numericVariant(v) && v.toDouble()==1)) { out=VignetteStyle::ColorPriority; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="paintoverlay") || (numericVariant(v) && v.toDouble()==2)) { out=VignetteStyle::PaintOverlay; return true; }
    return r.fail("Unknown VignetteStyle value");
}
template<> bool enumValue<HueRange>(Reader &r,const QVariant &v,HueRange &out) {
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="master") || (numericVariant(v) && v.toDouble()==0)) { out=HueRange::Master; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="reds") || (numericVariant(v) && v.toDouble()==1)) { out=HueRange::Reds; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="yellows") || (numericVariant(v) && v.toDouble()==2)) { out=HueRange::Yellows; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="greens") || (numericVariant(v) && v.toDouble()==3)) { out=HueRange::Greens; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="cyans") || (numericVariant(v) && v.toDouble()==4)) { out=HueRange::Cyans; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="blues") || (numericVariant(v) && v.toDouble()==5)) { out=HueRange::Blues; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="magentas") || (numericVariant(v) && v.toDouble()==6)) { out=HueRange::Magentas; return true; }
    return r.fail("Unknown HueRange value");
}
template<> bool enumValue<ColorRange>(Reader &r,const QVariant &v,ColorRange &out) {
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="reds") || (numericVariant(v) && v.toDouble()==0)) { out=ColorRange::Reds; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="yellows") || (numericVariant(v) && v.toDouble()==1)) { out=ColorRange::Yellows; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="greens") || (numericVariant(v) && v.toDouble()==2)) { out=ColorRange::Greens; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="cyans") || (numericVariant(v) && v.toDouble()==3)) { out=ColorRange::Cyans; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="blues") || (numericVariant(v) && v.toDouble()==4)) { out=ColorRange::Blues; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="magentas") || (numericVariant(v) && v.toDouble()==5)) { out=ColorRange::Magentas; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="whites") || (numericVariant(v) && v.toDouble()==6)) { out=ColorRange::Whites; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="neutrals") || (numericVariant(v) && v.toDouble()==7)) { out=ColorRange::Neutrals; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="blacks") || (numericVariant(v) && v.toDouble()==8)) { out=ColorRange::Blacks; return true; }
    return r.fail("Unknown ColorRange value");
}
template<> bool enumValue<SelectiveColorMethod>(Reader &r,const QVariant &v,SelectiveColorMethod &out) {
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="relative") || (numericVariant(v) && v.toDouble()==0)) { out=SelectiveColorMethod::Relative; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="absolute") || (numericVariant(v) && v.toDouble()==1)) { out=SelectiveColorMethod::Absolute; return true; }
    return r.fail("Unknown SelectiveColorMethod value");
}
template<> bool enumValue<GalleryBrushType>(Reader &r,const QVariant &v,GalleryBrushType &out) {
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="simple") || (numericVariant(v) && v.toDouble()==0)) { out=GalleryBrushType::Simple; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="lightrough") || (numericVariant(v) && v.toDouble()==1)) { out=GalleryBrushType::LightRough; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="darkrough") || (numericVariant(v) && v.toDouble()==2)) { out=GalleryBrushType::DarkRough; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="widesharp") || (numericVariant(v) && v.toDouble()==3)) { out=GalleryBrushType::WideSharp; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="wideblurry") || (numericVariant(v) && v.toDouble()==4)) { out=GalleryBrushType::WideBlurry; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="sparkle") || (numericVariant(v) && v.toDouble()==5)) { out=GalleryBrushType::Sparkle; return true; }
    return r.fail("Unknown GalleryBrushType value");
}
template<> bool enumValue<GalleryTexture>(Reader &r,const QVariant &v,GalleryTexture &out) {
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="canvas") || (numericVariant(v) && v.toDouble()==0)) { out=GalleryTexture::Canvas; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="brick") || (numericVariant(v) && v.toDouble()==1)) { out=GalleryTexture::Brick; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="burlap") || (numericVariant(v) && v.toDouble()==2)) { out=GalleryTexture::Burlap; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="sandstone") || (numericVariant(v) && v.toDouble()==3)) { out=GalleryTexture::Sandstone; return true; }
    return r.fail("Unknown GalleryTexture value");
}
template<> bool enumValue<GalleryLightDirection>(Reader &r,const QVariant &v,GalleryLightDirection &out) {
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="bottom") || (numericVariant(v) && v.toDouble()==0)) { out=GalleryLightDirection::Bottom; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="bottomleft") || (numericVariant(v) && v.toDouble()==1)) { out=GalleryLightDirection::BottomLeft; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="left") || (numericVariant(v) && v.toDouble()==2)) { out=GalleryLightDirection::Left; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="topleft") || (numericVariant(v) && v.toDouble()==3)) { out=GalleryLightDirection::TopLeft; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="top") || (numericVariant(v) && v.toDouble()==4)) { out=GalleryLightDirection::Top; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="topright") || (numericVariant(v) && v.toDouble()==5)) { out=GalleryLightDirection::TopRight; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="right") || (numericVariant(v) && v.toDouble()==6)) { out=GalleryLightDirection::Right; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="bottomright") || (numericVariant(v) && v.toDouble()==7)) { out=GalleryLightDirection::BottomRight; return true; }
    return r.fail("Unknown GalleryLightDirection value");
}
template<> bool enumValue<GalleryStrokeDirection>(Reader &r,const QVariant &v,GalleryStrokeDirection &out) {
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="rightdiagonal") || (numericVariant(v) && v.toDouble()==0)) { out=GalleryStrokeDirection::RightDiagonal; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="horizontal") || (numericVariant(v) && v.toDouble()==1)) { out=GalleryStrokeDirection::Horizontal; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="leftdiagonal") || (numericVariant(v) && v.toDouble()==2)) { out=GalleryStrokeDirection::LeftDiagonal; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="vertical") || (numericVariant(v) && v.toDouble()==3)) { out=GalleryStrokeDirection::Vertical; return true; }
    return r.fail("Unknown GalleryStrokeDirection value");
}
template<> bool enumValue<GalleryGlassTexture>(Reader &r,const QVariant &v,GalleryGlassTexture &out) {
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="frosted") || (numericVariant(v) && v.toDouble()==0)) { out=GalleryGlassTexture::Frosted; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="blocks") || (numericVariant(v) && v.toDouble()==1)) { out=GalleryGlassTexture::Blocks; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="canvas") || (numericVariant(v) && v.toDouble()==2)) { out=GalleryGlassTexture::Canvas; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="tinylens") || (numericVariant(v) && v.toDouble()==3)) { out=GalleryGlassTexture::TinyLens; return true; }
    return r.fail("Unknown GalleryGlassTexture value");
}
template<> bool enumValue<GalleryPatternType>(Reader &r,const QVariant &v,GalleryPatternType &out) {
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="dot") || (numericVariant(v) && v.toDouble()==0)) { out=GalleryPatternType::Dot; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="circle") || (numericVariant(v) && v.toDouble()==1)) { out=GalleryPatternType::Circle; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="line") || (numericVariant(v) && v.toDouble()==2)) { out=GalleryPatternType::Line; return true; }
    return r.fail("Unknown GalleryPatternType value");
}
template<> bool enumValue<GalleryGrainType>(Reader &r,const QVariant &v,GalleryGrainType &out) {
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="regular") || (numericVariant(v) && v.toDouble()==0)) { out=GalleryGrainType::Regular; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="soft") || (numericVariant(v) && v.toDouble()==1)) { out=GalleryGrainType::Soft; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="sprinkles") || (numericVariant(v) && v.toDouble()==2)) { out=GalleryGrainType::Sprinkles; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="clumped") || (numericVariant(v) && v.toDouble()==3)) { out=GalleryGrainType::Clumped; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="contrasty") || (numericVariant(v) && v.toDouble()==4)) { out=GalleryGrainType::Contrasty; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="enlarged") || (numericVariant(v) && v.toDouble()==5)) { out=GalleryGrainType::Enlarged; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="stippled") || (numericVariant(v) && v.toDouble()==6)) { out=GalleryGrainType::Stippled; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="horizontal") || (numericVariant(v) && v.toDouble()==7)) { out=GalleryGrainType::Horizontal; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="vertical") || (numericVariant(v) && v.toDouble()==8)) { out=GalleryGrainType::Vertical; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="speckle") || (numericVariant(v) && v.toDouble()==9)) { out=GalleryGrainType::Speckle; return true; }
    return r.fail("Unknown GalleryGrainType value");
}
template<> bool enumValue<EyeKind>(Reader &r,const QVariant &v,EyeKind &out) {
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="red") || (numericVariant(v) && v.toDouble()==0)) { out=EyeKind::Red; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="pet") || (numericVariant(v) && v.toDouble()==1)) { out=EyeKind::Pet; return true; }
    return r.fail("Unknown EyeKind value");
}
template<> bool enumValue<NoiseDistribution>(Reader &r,const QVariant &v,NoiseDistribution &out) {
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="uniform") || (numericVariant(v) && v.toDouble()==0)) { out=NoiseDistribution::Uniform; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="gaussian") || (numericVariant(v) && v.toDouble()==1)) { out=NoiseDistribution::Gaussian; return true; }
    return r.fail("Unknown NoiseDistribution value");
}
template<> bool enumValue<MixerBand>(Reader &r,const QVariant &v,MixerBand &out) {
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="red") || (numericVariant(v) && v.toDouble()==0)) { out=MixerBand::Red; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="orange") || (numericVariant(v) && v.toDouble()==1)) { out=MixerBand::Orange; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="yellow") || (numericVariant(v) && v.toDouble()==2)) { out=MixerBand::Yellow; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="green") || (numericVariant(v) && v.toDouble()==3)) { out=MixerBand::Green; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="aqua") || (numericVariant(v) && v.toDouble()==4)) { out=MixerBand::Aqua; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="blue") || (numericVariant(v) && v.toDouble()==5)) { out=MixerBand::Blue; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="purple") || (numericVariant(v) && v.toDouble()==6)) { out=MixerBand::Purple; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="magenta") || (numericVariant(v) && v.toDouble()==7)) { out=MixerBand::Magenta; return true; }
    return r.fail("Unknown MixerBand value");
}
template<> bool enumValue<HdrToningMethod>(Reader &r,const QVariant &v,HdrToningMethod &out) {
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="exposuregamma") || (numericVariant(v) && v.toDouble()==0)) { out=HdrToningMethod::ExposureGamma; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="highlightcompression") || (numericVariant(v) && v.toDouble()==1)) { out=HdrToningMethod::HighlightCompression; return true; }
    return r.fail("Unknown HdrToningMethod value");
}
template<> bool enumValue<FlareLens>(Reader &r,const QVariant &v,FlareLens &out) {
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="zoom") || (numericVariant(v) && v.toDouble()==0)) { out=FlareLens::Zoom; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="prime35") || (numericVariant(v) && v.toDouble()==1)) { out=FlareLens::Prime35; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="prime105") || (numericVariant(v) && v.toDouble()==2)) { out=FlareLens::Prime105; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="movieprime") || (numericVariant(v) && v.toDouble()==3)) { out=FlareLens::MoviePrime; return true; }
    return r.fail("Unknown FlareLens value");
}
template<> bool enumValue<LightKind>(Reader &r,const QVariant &v,LightKind &out) {
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="spot") || (numericVariant(v) && v.toDouble()==0)) { out=LightKind::Spot; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="point") || (numericVariant(v) && v.toDouble()==1)) { out=LightKind::Point; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="infinite") || (numericVariant(v) && v.toDouble()==2)) { out=LightKind::Infinite; return true; }
    return r.fail("Unknown LightKind value");
}
template<> bool enumValue<TextureChannel>(Reader &r,const QVariant &v,TextureChannel &out) {
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="none") || (numericVariant(v) && v.toDouble()==0)) { out=TextureChannel::None; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="red") || (numericVariant(v) && v.toDouble()==1)) { out=TextureChannel::Red; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="green") || (numericVariant(v) && v.toDouble()==2)) { out=TextureChannel::Green; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="blue") || (numericVariant(v) && v.toDouble()==3)) { out=TextureChannel::Blue; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="alpha") || (numericVariant(v) && v.toDouble()==4)) { out=TextureChannel::Alpha; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="luminance") || (numericVariant(v) && v.toDouble()==5)) { out=TextureChannel::Luminance; return true; }
    return r.fail("Unknown TextureChannel value");
}
template<> bool enumValue<FlameType>(Reader &r,const QVariant &v,FlameType &out) {
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="onealongpath") || (numericVariant(v) && v.toDouble()==0)) { out=FlameType::OneAlongPath; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="multiplealongpath") || (numericVariant(v) && v.toDouble()==1)) { out=FlameType::MultipleAlongPath; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="pathdirections") || (numericVariant(v) && v.toDouble()==2)) { out=FlameType::PathDirections; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="variouslengths") || (numericVariant(v) && v.toDouble()==3)) { out=FlameType::VariousLengths; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="candlelight") || (numericVariant(v) && v.toDouble()==4)) { out=FlameType::CandleLight; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="onedirection") || (numericVariant(v) && v.toDouble()==5)) { out=FlameType::OneDirection; return true; }
    return r.fail("Unknown FlameType value");
}
template<> bool enumValue<FlameStyle>(Reader &r,const QVariant &v,FlameStyle &out) {
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="normal") || (numericVariant(v) && v.toDouble()==0)) { out=FlameStyle::Normal; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="violent") || (numericVariant(v) && v.toDouble()==1)) { out=FlameStyle::Violent; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="flat") || (numericVariant(v) && v.toDouble()==2)) { out=FlameStyle::Flat; return true; }
    return r.fail("Unknown FlameStyle value");
}
template<> bool enumValue<FlameShape>(Reader &r,const QVariant &v,FlameShape &out) {
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="parallel") || (numericVariant(v) && v.toDouble()==0)) { out=FlameShape::Parallel; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="tocenter") || (numericVariant(v) && v.toDouble()==1)) { out=FlameShape::ToCenter; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="spread") || (numericVariant(v) && v.toDouble()==2)) { out=FlameShape::Spread; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="oval") || (numericVariant(v) && v.toDouble()==3)) { out=FlameShape::Oval; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="pointed") || (numericVariant(v) && v.toDouble()==4)) { out=FlameShape::Pointed; return true; }
    return r.fail("Unknown FlameShape value");
}
template<> bool enumValue<PictureFrameStyle>(Reader &r,const QVariant &v,PictureFrameStyle &out) {
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="vinewithflowers") || (numericVariant(v) && v.toDouble()==0)) { out=PictureFrameStyle::VineWithFlowers; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="vinewithleaves") || (numericVariant(v) && v.toDouble()==1)) { out=PictureFrameStyle::VineWithLeaves; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="ivy") || (numericVariant(v) && v.toDouble()==2)) { out=PictureFrameStyle::Ivy; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="roses") || (numericVariant(v) && v.toDouble()==3)) { out=PictureFrameStyle::Roses; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="daisies") || (numericVariant(v) && v.toDouble()==4)) { out=PictureFrameStyle::Daisies; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="berries") || (numericVariant(v) && v.toDouble()==5)) { out=PictureFrameStyle::Berries; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="bamboo") || (numericVariant(v) && v.toDouble()==6)) { out=PictureFrameStyle::Bamboo; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="waves") || (numericVariant(v) && v.toDouble()==7)) { out=PictureFrameStyle::Waves; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="zigzag") || (numericVariant(v) && v.toDouble()==8)) { out=PictureFrameStyle::Zigzag; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="dots") || (numericVariant(v) && v.toDouble()==9)) { out=PictureFrameStyle::Dots; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="rope") || (numericVariant(v) && v.toDouble()==10)) { out=PictureFrameStyle::Rope; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="scallops") || (numericVariant(v) && v.toDouble()==11)) { out=PictureFrameStyle::Scallops; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="doubleline") || (numericVariant(v) && v.toDouble()==12)) { out=PictureFrameStyle::DoubleLine; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="snowflakes") || (numericVariant(v) && v.toDouble()==13)) { out=PictureFrameStyle::Snowflakes; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="stars") || (numericVariant(v) && v.toDouble()==14)) { out=PictureFrameStyle::Stars; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="hearts") || (numericVariant(v) && v.toDouble()==15)) { out=PictureFrameStyle::Hearts; return true; }
    return r.fail("Unknown PictureFrameStyle value");
}
template<> bool enumValue<RadialBlurMethod>(Reader &r,const QVariant &v,RadialBlurMethod &out) {
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="spin") || (numericVariant(v) && v.toDouble()==0)) { out=RadialBlurMethod::Spin; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="zoom") || (numericVariant(v) && v.toDouble()==1)) { out=RadialBlurMethod::Zoom; return true; }
    return r.fail("Unknown RadialBlurMethod value");
}
template<> bool enumValue<FilterBlurQuality>(Reader &r,const QVariant &v,FilterBlurQuality &out) {
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="low") || (numericVariant(v) && v.toDouble()==0)) { out=FilterBlurQuality::Low; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="medium") || (numericVariant(v) && v.toDouble()==1)) { out=FilterBlurQuality::Medium; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="high") || (numericVariant(v) && v.toDouble()==2)) { out=FilterBlurQuality::High; return true; }
    return r.fail("Unknown FilterBlurQuality value");
}
template<> bool enumValue<SmartBlurMode>(Reader &r,const QVariant &v,SmartBlurMode &out) {
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="normal") || (numericVariant(v) && v.toDouble()==0)) { out=SmartBlurMode::Normal; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="edgeonly") || (numericVariant(v) && v.toDouble()==1)) { out=SmartBlurMode::EdgeOnly; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="overlayedge") || (numericVariant(v) && v.toDouble()==2)) { out=SmartBlurMode::OverlayEdge; return true; }
    return r.fail("Unknown SmartBlurMode value");
}
template<> bool enumValue<BlurShape>(Reader &r,const QVariant &v,BlurShape &out) {
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="circle") || (numericVariant(v) && v.toDouble()==0)) { out=BlurShape::Circle; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="ring") || (numericVariant(v) && v.toDouble()==1)) { out=BlurShape::Ring; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="square") || (numericVariant(v) && v.toDouble()==2)) { out=BlurShape::Square; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="diamond") || (numericVariant(v) && v.toDouble()==3)) { out=BlurShape::Diamond; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="triangle") || (numericVariant(v) && v.toDouble()==4)) { out=BlurShape::Triangle; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="hexagon") || (numericVariant(v) && v.toDouble()==5)) { out=BlurShape::Hexagon; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="star") || (numericVariant(v) && v.toDouble()==6)) { out=BlurShape::Star; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="heart") || (numericVariant(v) && v.toDouble()==7)) { out=BlurShape::Heart; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="cross") || (numericVariant(v) && v.toDouble()==8)) { out=BlurShape::Cross; return true; }
    return r.fail("Unknown BlurShape value");
}
template<> bool enumValue<LensDepthSource>(Reader &r,const QVariant &v,LensDepthSource &out) {
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="none") || (numericVariant(v) && v.toDouble()==0)) { out=LensDepthSource::None; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="transparency") || (numericVariant(v) && v.toDouble()==1)) { out=LensDepthSource::Transparency; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="map") || (numericVariant(v) && v.toDouble()==2)) { out=LensDepthSource::Map; return true; }
    return r.fail("Unknown LensDepthSource value");
}
template<> bool enumValue<WhiteBalancePreset>(Reader &r,const QVariant &v,WhiteBalancePreset &out) {
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="daylight") || (numericVariant(v) && v.toDouble()==0)) { out=WhiteBalancePreset::Daylight; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="cloudy") || (numericVariant(v) && v.toDouble()==1)) { out=WhiteBalancePreset::Cloudy; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="shade") || (numericVariant(v) && v.toDouble()==2)) { out=WhiteBalancePreset::Shade; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="tungsten") || (numericVariant(v) && v.toDouble()==3)) { out=WhiteBalancePreset::Tungsten; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="fluorescent") || (numericVariant(v) && v.toDouble()==4)) { out=WhiteBalancePreset::Fluorescent; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="flash") || (numericVariant(v) && v.toDouble()==5)) { out=WhiteBalancePreset::Flash; return true; }
    return r.fail("Unknown WhiteBalancePreset value");
}
template<> bool enumValue<MorphologyShape>(Reader &r,const QVariant &v,MorphologyShape &out) {
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="square") || (numericVariant(v) && v.toDouble()==0)) { out=MorphologyShape::Square; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="round") || (numericVariant(v) && v.toDouble()==1)) { out=MorphologyShape::Round; return true; }
    return r.fail("Unknown MorphologyShape value");
}
template<> bool enumValue<FilterColorModel>(Reader &r,const QVariant &v,FilterColorModel &out) {
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="rgb") || (numericVariant(v) && v.toDouble()==0)) { out=FilterColorModel::Rgb; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="hsb") || (numericVariant(v) && v.toDouble()==1)) { out=FilterColorModel::Hsb; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="hsl") || (numericVariant(v) && v.toDouble()==2)) { out=FilterColorModel::Hsl; return true; }
    return r.fail("Unknown FilterColorModel value");
}
template<> bool enumValue<MeshInterpolation>(Reader &r,const QVariant &v,MeshInterpolation &out) {
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="nearest") || (numericVariant(v) && v.toDouble()==0)) { out=MeshInterpolation::Nearest; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="bilinear") || (numericVariant(v) && v.toDouble()==1)) { out=MeshInterpolation::Bilinear; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="bicubic") || (numericVariant(v) && v.toDouble()==2)) { out=MeshInterpolation::Bicubic; return true; }
    return r.fail("Unknown MeshInterpolation value");
}
template<> bool enumValue<LiquifyTool>(Reader &r,const QVariant &v,LiquifyTool &out) {
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="forwardwarp") || (numericVariant(v) && v.toDouble()==0)) { out=LiquifyTool::ForwardWarp; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="reconstruct") || (numericVariant(v) && v.toDouble()==1)) { out=LiquifyTool::Reconstruct; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="smooth") || (numericVariant(v) && v.toDouble()==2)) { out=LiquifyTool::Smooth; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="twirlclockwise") || (numericVariant(v) && v.toDouble()==3)) { out=LiquifyTool::TwirlClockwise; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="twirlcounterclockwise") || (numericVariant(v) && v.toDouble()==4)) { out=LiquifyTool::TwirlCounterclockwise; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="pucker") || (numericVariant(v) && v.toDouble()==5)) { out=LiquifyTool::Pucker; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="bloat") || (numericVariant(v) && v.toDouble()==6)) { out=LiquifyTool::Bloat; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="pushleft") || (numericVariant(v) && v.toDouble()==7)) { out=LiquifyTool::PushLeft; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="freeze") || (numericVariant(v) && v.toDouble()==8)) { out=LiquifyTool::Freeze; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="thaw") || (numericVariant(v) && v.toDouble()==9)) { out=LiquifyTool::Thaw; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="lassomask") || (numericVariant(v) && v.toDouble()==10)) { out=LiquifyTool::LassoMask; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="reconstructall") || (numericVariant(v) && v.toDouble()==11)) { out=LiquifyTool::ReconstructAll; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="freezeall") || (numericVariant(v) && v.toDouble()==12)) { out=LiquifyTool::FreezeAll; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="thawall") || (numericVariant(v) && v.toDouble()==13)) { out=LiquifyTool::ThawAll; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="invertfreeze") || (numericVariant(v) && v.toDouble()==14)) { out=LiquifyTool::InvertFreeze; return true; }
    return r.fail("Unknown LiquifyTool value");
}
template<> bool enumValue<WideAngleModel>(Reader &r,const QVariant &v,WideAngleModel &out) {
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="auto") || (numericVariant(v) && v.toDouble()==0)) { out=WideAngleModel::Auto; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="fisheye") || (numericVariant(v) && v.toDouble()==1)) { out=WideAngleModel::Fisheye; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="perspective") || (numericVariant(v) && v.toDouble()==2)) { out=WideAngleModel::Perspective; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="fullspherical") || (numericVariant(v) && v.toDouble()==3)) { out=WideAngleModel::FullSpherical; return true; }
    return r.fail("Unknown WideAngleModel value");
}
template<> bool enumValue<WideAngleOrientation>(Reader &r,const QVariant &v,WideAngleOrientation &out) {
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="free") || (numericVariant(v) && v.toDouble()==0)) { out=WideAngleOrientation::Free; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="horizontal") || (numericVariant(v) && v.toDouble()==1)) { out=WideAngleOrientation::Horizontal; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="vertical") || (numericVariant(v) && v.toDouble()==2)) { out=WideAngleOrientation::Vertical; return true; }
    return r.fail("Unknown WideAngleOrientation value");
}
template<> bool enumValue<PuppetMode>(Reader &r,const QVariant &v,PuppetMode &out) {
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="rigid") || (numericVariant(v) && v.toDouble()==0)) { out=PuppetMode::Rigid; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="normal") || (numericVariant(v) && v.toDouble()==1)) { out=PuppetMode::Normal; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="distort") || (numericVariant(v) && v.toDouble()==2)) { out=PuppetMode::Distort; return true; }
    return r.fail("Unknown PuppetMode value");
}
template<> bool enumValue<PuppetDensity>(Reader &r,const QVariant &v,PuppetDensity &out) {
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="fewer") || (numericVariant(v) && v.toDouble()==0)) { out=PuppetDensity::Fewer; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="normal") || (numericVariant(v) && v.toDouble()==1)) { out=PuppetDensity::Normal; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="more") || (numericVariant(v) && v.toDouble()==2)) { out=PuppetDensity::More; return true; }
    return r.fail("Unknown PuppetDensity value");
}
template<> bool enumValue<PerspectiveStraighten>(Reader &r,const QVariant &v,PerspectiveStraighten &out) {
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="none") || (numericVariant(v) && v.toDouble()==0)) { out=PerspectiveStraighten::None; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="horizontal") || (numericVariant(v) && v.toDouble()==1)) { out=PerspectiveStraighten::Horizontal; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="vertical") || (numericVariant(v) && v.toDouble()==2)) { out=PerspectiveStraighten::Vertical; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="auto") || (numericVariant(v) && v.toDouble()==3)) { out=PerspectiveStraighten::Auto; return true; }
    return r.fail("Unknown PerspectiveStraighten value");
}
template<> bool enumValue<VanishingEdge>(Reader &r,const QVariant &v,VanishingEdge &out) {
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="top") || (numericVariant(v) && v.toDouble()==0)) { out=VanishingEdge::Top; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="right") || (numericVariant(v) && v.toDouble()==1)) { out=VanishingEdge::Right; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="bottom") || (numericVariant(v) && v.toDouble()==2)) { out=VanishingEdge::Bottom; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="left") || (numericVariant(v) && v.toDouble()==3)) { out=VanishingEdge::Left; return true; }
    return r.fail("Unknown VanishingEdge value");
}
template<> bool enumValue<DiagnosticColorSpace>(Reader &r,const QVariant &v,DiagnosticColorSpace &out) {
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="srgb") || (numericVariant(v) && v.toDouble()==0)) { out=DiagnosticColorSpace::Srgb; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="displayp3") || (numericVariant(v) && v.toDouble()==1)) { out=DiagnosticColorSpace::DisplayP3; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="adobergb") || (numericVariant(v) && v.toDouble()==2)) { out=DiagnosticColorSpace::AdobeRgb; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="prophoto") || (numericVariant(v) && v.toDouble()==3)) { out=DiagnosticColorSpace::ProPhoto; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="rec2020") || (numericVariant(v) && v.toDouble()==4)) { out=DiagnosticColorSpace::Rec2020; return true; }
    return r.fail("Unknown DiagnosticColorSpace value");
}
template<> bool enumValue<LightcraftLook>(Reader &r,const QVariant &v,LightcraftLook &out) {
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="color") || (numericVariant(v) && v.toDouble()==0)) { out=LightcraftLook::Color; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="neutral") || (numericVariant(v) && v.toDouble()==1)) { out=LightcraftLook::Neutral; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="vivid") || (numericVariant(v) && v.toDouble()==2)) { out=LightcraftLook::Vivid; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="landscape") || (numericVariant(v) && v.toDouble()==3)) { out=LightcraftLook::Landscape; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="portrait") || (numericVariant(v) && v.toDouble()==4)) { out=LightcraftLook::Portrait; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="mono") || (numericVariant(v) && v.toDouble()==5)) { out=LightcraftLook::Mono; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="filmwarmprint") || (numericVariant(v) && v.toDouble()==6)) { out=LightcraftLook::FilmWarmPrint; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="filmcoolfade") || (numericVariant(v) && v.toDouble()==7)) { out=LightcraftLook::FilmCoolFade; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="filmgoldenhour") || (numericVariant(v) && v.toDouble()==8)) { out=LightcraftLook::FilmGoldenHour; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="filmfadedslide") || (numericVariant(v) && v.toDouble()==9)) { out=LightcraftLook::FilmFadedSlide; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="cinetealamber") || (numericVariant(v) && v.toDouble()==10)) { out=LightcraftLook::CineTealAmber; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="cinenightblue") || (numericVariant(v) && v.toDouble()==11)) { out=LightcraftLook::CineNightBlue; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="cinedesertheat") || (numericVariant(v) && v.toDouble()==12)) { out=LightcraftLook::CineDesertHeat; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="cineneondusk") || (numericVariant(v) && v.toDouble()==13)) { out=LightcraftLook::CineNeonDusk; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="mutedmattesoft") || (numericVariant(v) && v.toDouble()==14)) { out=LightcraftLook::MutedMatteSoft; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="mutedbleached") || (numericVariant(v) && v.toDouble()==15)) { out=LightcraftLook::MutedBleached; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="mutedpastelhaze") || (numericVariant(v) && v.toDouble()==16)) { out=LightcraftLook::MutedPastelHaze; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="mutedquietgreen") || (numericVariant(v) && v.toDouble()==17)) { out=LightcraftLook::MutedQuietGreen; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="bwmonorich") || (numericVariant(v) && v.toDouble()==18)) { out=LightcraftLook::BwMonoRich; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="bwredfilter") || (numericVariant(v) && v.toDouble()==19)) { out=LightcraftLook::BwRedFilter; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="bwsoft") || (numericVariant(v) && v.toDouble()==20)) { out=LightcraftLook::BwSoft; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="bwsepia") || (numericVariant(v) && v.toDouble()==21)) { out=LightcraftLook::BwSepia; return true; }
    return r.fail("Unknown LightcraftLook value");
}
template<> bool enumValue<LightcraftPreset>(Reader &r,const QVariant &v,LightcraftPreset &out) {
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="warmglow") || (numericVariant(v) && v.toDouble()==0)) { out=LightcraftPreset::WarmGlow; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="coolmorning") || (numericVariant(v) && v.toDouble()==1)) { out=LightcraftPreset::CoolMorning; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="tealorange") || (numericVariant(v) && v.toDouble()==2)) { out=LightcraftPreset::TealOrange; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="vividpop") || (numericVariant(v) && v.toDouble()==3)) { out=LightcraftPreset::VividPop; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="softpastel") || (numericVariant(v) && v.toDouble()==4)) { out=LightcraftPreset::SoftPastel; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="fadedmatte") || (numericVariant(v) && v.toDouble()==5)) { out=LightcraftPreset::FadedMatte; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="warmfilm") || (numericVariant(v) && v.toDouble()==6)) { out=LightcraftPreset::WarmFilm; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="mutedfilm") || (numericVariant(v) && v.toDouble()==7)) { out=LightcraftPreset::MutedFilm; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="bwhighcontrast") || (numericVariant(v) && v.toDouble()==8)) { out=LightcraftPreset::BwHighContrast; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="bwsoft") || (numericVariant(v) && v.toDouble()==9)) { out=LightcraftPreset::BwSoft; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="bwselenium") || (numericVariant(v) && v.toDouble()==10)) { out=LightcraftPreset::BwSelenium; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="crisplandscape") || (numericVariant(v) && v.toDouble()==11)) { out=LightcraftPreset::CrispLandscape; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="goldenhour") || (numericVariant(v) && v.toDouble()==12)) { out=LightcraftPreset::GoldenHour; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="bluehour") || (numericVariant(v) && v.toDouble()==13)) { out=LightcraftPreset::BlueHour; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="softskin") || (numericVariant(v) && v.toDouble()==14)) { out=LightcraftPreset::SoftSkin; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="brightairy") || (numericVariant(v) && v.toDouble()==15)) { out=LightcraftPreset::BrightAiry; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="moody") || (numericVariant(v) && v.toDouble()==16)) { out=LightcraftPreset::Moody; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="cinematic") || (numericVariant(v) && v.toDouble()==17)) { out=LightcraftPreset::Cinematic; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="moodyportrait") || (numericVariant(v) && v.toDouble()==18)) { out=LightcraftPreset::MoodyPortrait; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="goldenglow") || (numericVariant(v) && v.toDouble()==19)) { out=LightcraftPreset::GoldenGlow; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="cleanstudio") || (numericVariant(v) && v.toDouble()==20)) { out=LightcraftPreset::CleanStudio; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="crispvista") || (numericVariant(v) && v.toDouble()==21)) { out=LightcraftPreset::CrispVista; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="deepsky") || (numericVariant(v) && v.toDouble()==22)) { out=LightcraftPreset::DeepSky; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="lushgreens") || (numericVariant(v) && v.toDouble()==23)) { out=LightcraftPreset::LushGreens; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="desertwarmth") || (numericVariant(v) && v.toDouble()==24)) { out=LightcraftPreset::DesertWarmth; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="mistymorning") || (numericVariant(v) && v.toDouble()==25)) { out=LightcraftPreset::MistyMorning; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="grittystreet") || (numericVariant(v) && v.toDouble()==26)) { out=LightcraftPreset::GrittyStreet; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="neonnight") || (numericVariant(v) && v.toDouble()==27)) { out=LightcraftPreset::NeonNight; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="concretecool") || (numericVariant(v) && v.toDouble()==28)) { out=LightcraftPreset::ConcreteCool; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="fadedurban") || (numericVariant(v) && v.toDouble()==29)) { out=LightcraftPreset::FadedUrban; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="freshbright") || (numericVariant(v) && v.toDouble()==30)) { out=LightcraftPreset::FreshBright; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="warmtable") || (numericVariant(v) && v.toDouble()==31)) { out=LightcraftPreset::WarmTable; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="darkmoodyfood") || (numericVariant(v) && v.toDouble()==32)) { out=LightcraftPreset::DarkMoodyFood; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="autumngold") || (numericVariant(v) && v.toDouble()==33)) { out=LightcraftPreset::AutumnGold; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="winterblue") || (numericVariant(v) && v.toDouble()==34)) { out=LightcraftPreset::WinterBlue; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="springfresh") || (numericVariant(v) && v.toDouble()==35)) { out=LightcraftPreset::SpringFresh; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="summerhaze") || (numericVariant(v) && v.toDouble()==36)) { out=LightcraftPreset::SummerHaze; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="instant70s") || (numericVariant(v) && v.toDouble()==37)) { out=LightcraftPreset::Instant70s; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="crossprocess") || (numericVariant(v) && v.toDouble()==38)) { out=LightcraftPreset::CrossProcess; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="bleachbypass") || (numericVariant(v) && v.toDouble()==39)) { out=LightcraftPreset::BleachBypass; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="bwsepia") || (numericVariant(v) && v.toDouble()==40)) { out=LightcraftPreset::BwSepia; return true; }
    return r.fail("Unknown LightcraftPreset value");
}
template<> bool enumValue<ErrorCode>(Reader &r,const QVariant &v,ErrorCode &out) {
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="invaliddimensions") || (numericVariant(v) && v.toDouble()==0)) { out=ErrorCode::InvalidDimensions; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="invalidbuffer") || (numericVariant(v) && v.toDouble()==1)) { out=ErrorCode::InvalidBuffer; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="invalidpixel") || (numericVariant(v) && v.toDouble()==2)) { out=ErrorCode::InvalidPixel; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="invalidparameter") || (numericVariant(v) && v.toDouble()==3)) { out=ErrorCode::InvalidParameter; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="sizemismatch") || (numericVariant(v) && v.toDouble()==4)) { out=ErrorCode::SizeMismatch; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="allocationfailure") || (numericVariant(v) && v.toDouble()==5)) { out=ErrorCode::AllocationFailure; return true; }
    return r.fail("Unknown ErrorCode value");
}
template<> bool enumValue<LensFilterEdge>(Reader &r,const QVariant &v,LensFilterEdge &out) {
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="extension") || (numericVariant(v) && v.toDouble()==0)) { out=LensFilterEdge::Extension; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="transparency") || (numericVariant(v) && v.toDouble()==1)) { out=LensFilterEdge::Transparency; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="color") || (numericVariant(v) && v.toDouble()==2)) { out=LensFilterEdge::Color; return true; }
    return r.fail("Unknown LensFilterEdge value");
}
template<> bool enumValue<MezzotintPattern>(Reader &r,const QVariant &v,MezzotintPattern &out) {
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="finedots") || (numericVariant(v) && v.toDouble()==0)) { out=MezzotintPattern::FineDots; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="mediumdots") || (numericVariant(v) && v.toDouble()==1)) { out=MezzotintPattern::MediumDots; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="grainydots") || (numericVariant(v) && v.toDouble()==2)) { out=MezzotintPattern::GrainyDots; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="coarsedots") || (numericVariant(v) && v.toDouble()==3)) { out=MezzotintPattern::CoarseDots; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="shortlines") || (numericVariant(v) && v.toDouble()==4)) { out=MezzotintPattern::ShortLines; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="mediumlines") || (numericVariant(v) && v.toDouble()==5)) { out=MezzotintPattern::MediumLines; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="longlines") || (numericVariant(v) && v.toDouble()==6)) { out=MezzotintPattern::LongLines; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="shortstrokes") || (numericVariant(v) && v.toDouble()==7)) { out=MezzotintPattern::ShortStrokes; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="mediumstrokes") || (numericVariant(v) && v.toDouble()==8)) { out=MezzotintPattern::MediumStrokes; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="longstrokes") || (numericVariant(v) && v.toDouble()==9)) { out=MezzotintPattern::LongStrokes; return true; }
    return r.fail("Unknown MezzotintPattern value");
}
template<> bool enumValue<DiffuseMode>(Reader &r,const QVariant &v,DiffuseMode &out) {
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="normal") || (numericVariant(v) && v.toDouble()==0)) { out=DiffuseMode::Normal; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="darkenonly") || (numericVariant(v) && v.toDouble()==1)) { out=DiffuseMode::DarkenOnly; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="lightenonly") || (numericVariant(v) && v.toDouble()==2)) { out=DiffuseMode::LightenOnly; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="anisotropic") || (numericVariant(v) && v.toDouble()==3)) { out=DiffuseMode::Anisotropic; return true; }
    return r.fail("Unknown DiffuseMode value");
}
template<> bool enumValue<ExtrudeShape>(Reader &r,const QVariant &v,ExtrudeShape &out) {
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="blocks") || (numericVariant(v) && v.toDouble()==0)) { out=ExtrudeShape::Blocks; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="pyramids") || (numericVariant(v) && v.toDouble()==1)) { out=ExtrudeShape::Pyramids; return true; }
    return r.fail("Unknown ExtrudeShape value");
}
template<> bool enumValue<TileFill>(Reader &r,const QVariant &v,TileFill &out) {
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="background") || (numericVariant(v) && v.toDouble()==0)) { out=TileFill::Background; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="foreground") || (numericVariant(v) && v.toDouble()==1)) { out=TileFill::Foreground; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="inverse") || (numericVariant(v) && v.toDouble()==2)) { out=TileFill::Inverse; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="unaltered") || (numericVariant(v) && v.toDouble()==3)) { out=TileFill::Unaltered; return true; }
    return r.fail("Unknown TileFill value");
}
template<> bool enumValue<WindMethod>(Reader &r,const QVariant &v,WindMethod &out) {
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="wind") || (numericVariant(v) && v.toDouble()==0)) { out=WindMethod::Wind; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="blast") || (numericVariant(v) && v.toDouble()==1)) { out=WindMethod::Blast; return true; }
    if ((v.metaType().id()==QMetaType::QString && token(v.toString())=="stagger") || (numericVariant(v) && v.toDouble()==2)) { out=WindMethod::Stagger; return true; }
    return r.fail("Unknown WindMethod value");
}
template<> bool fields<BlendOptions>(Reader &r,const QVariantMap &p,BlendOptions &o) {
    return r.keys(p,{"mode","opacity","space"})
        && member(r,p,"mode",o.mode)
        && member(r,p,"opacity",o.opacity)
        && member(r,p,"space",o.space);
}
template<> bool fields<MaskPoint>(Reader &r,const QVariantMap &p,MaskPoint &o) {
    return r.keys(p,{"x","y"})
        && member(r,p,"x",o.x)
        && member(r,p,"y",o.y);
}
template<> bool fields<LinearMaskOptions>(Reader &r,const QVariantMap &p,LinearMaskOptions &o) {
    return r.keys(p,{"start","end"})
        && member(r,p,"start",o.start)
        && member(r,p,"end",o.end);
}
template<> bool fields<RadialMaskOptions>(Reader &r,const QVariantMap &p,RadialMaskOptions &o) {
    return r.keys(p,{"center","radiusX","radiusY","angle","feather","invert"})
        && member(r,p,"center",o.center)
        && member(r,p,"radiusX",o.radiusX)
        && member(r,p,"radiusY",o.radiusY)
        && member(r,p,"angle",o.angle)
        && member(r,p,"feather",o.feather)
        && member(r,p,"invert",o.invert);
}
template<> bool fields<BrushStroke>(Reader &r,const QVariantMap &p,BrushStroke &o) {
    return r.keys(p,{"points","radius","feather","flow","density","erase","autoMask"})
        && member(r,p,"points",o.points)
        && member(r,p,"radius",o.radius)
        && member(r,p,"feather",o.feather)
        && member(r,p,"flow",o.flow)
        && member(r,p,"density",o.density)
        && member(r,p,"erase",o.erase)
        && member(r,p,"autoMask",o.autoMask);
}
template<> bool fields<LuminanceRangeOptions>(Reader &r,const QVariantMap &p,LuminanceRangeOptions &o) {
    return r.keys(p,{"low","high","lowFeather","highFeather","exposure"})
        && member(r,p,"low",o.low)
        && member(r,p,"high",o.high)
        && member(r,p,"lowFeather",o.lowFeather)
        && member(r,p,"highFeather",o.highFeather)
        && member(r,p,"exposure",o.exposure);
}
template<> bool fields<ColorRangeOptions>(Reader &r,const QVariantMap &p,ColorRangeOptions &o) {
    return r.keys(p,{"samples","range","exposure"})
        && member(r,p,"samples",o.samples)
        && member(r,p,"range",o.range)
        && member(r,p,"exposure",o.exposure);
}
template<> bool fields<LocalAdjustmentOptions>(Reader &r,const QVariantMap &p,LocalAdjustmentOptions &o) {
    return r.keys(p,{"exposure","temperature","tint","contrast","highlights","shadows","whites","blacks","texture","clarity","dehaze","hue","saturation","sharpness","noise","moire","defringe","colorHue","colorSaturation","amount"})
        && member(r,p,"exposure",o.exposure)
        && member(r,p,"temperature",o.temperature)
        && member(r,p,"tint",o.tint)
        && member(r,p,"contrast",o.contrast)
        && member(r,p,"highlights",o.highlights)
        && member(r,p,"shadows",o.shadows)
        && member(r,p,"whites",o.whites)
        && member(r,p,"blacks",o.blacks)
        && member(r,p,"texture",o.texture)
        && member(r,p,"clarity",o.clarity)
        && member(r,p,"dehaze",o.dehaze)
        && member(r,p,"hue",o.hue)
        && member(r,p,"saturation",o.saturation)
        && member(r,p,"sharpness",o.sharpness)
        && member(r,p,"noise",o.noise)
        && member(r,p,"moire",o.moire)
        && member(r,p,"defringe",o.defringe)
        && member(r,p,"colorHue",o.colorHue)
        && member(r,p,"colorSaturation",o.colorSaturation)
        && member(r,p,"amount",o.amount);
}
template<> bool fields<LocalAdjustment>(Reader &r,const QVariantMap &p,LocalAdjustment &o) {
    return r.keys(p,{"mask","options"})
        && member(r,p,"mask",o.mask)
        && member(r,p,"options",o.options);
}
template<> bool fields<GradientStop>(Reader &r,const QVariantMap &p,GradientStop &o) {
    return r.keys(p,{"position","color"})
        && member(r,p,"position",o.position)
        && member(r,p,"color",o.color);
}
template<> bool fields<GradientMapOptions>(Reader &r,const QVariantMap &p,GradientMapOptions &o) {
    return r.keys(p,{"stops","reverse","dither","amount"})
        && member(r,p,"stops",o.stops)
        && member(r,p,"reverse",o.reverse)
        && member(r,p,"dither",o.dither)
        && member(r,p,"amount",o.amount);
}
template<> bool fields<ColorLut>(Reader &r,const QVariantMap &p,ColorLut &o) {
    return r.keys(p,{"kind","size","values","domainMin","domainMax"})
        && member(r,p,"kind",o.kind)
        && member(r,p,"size",o.size)
        && member(r,p,"values",o.values)
        && member(r,p,"domainMin",o.domainMin)
        && member(r,p,"domainMax",o.domainMax);
}
template<> bool fields<ColorLookupOptions>(Reader &r,const QVariantMap &p,ColorLookupOptions &o) {
    return r.keys(p,{"interpolation","amount","dither"})
        && member(r,p,"interpolation",o.interpolation)
        && member(r,p,"amount",o.amount)
        && member(r,p,"dither",o.dither);
}
template<> bool fields<CameraRawWheel>(Reader &r,const QVariantMap &p,CameraRawWheel &o) {
    return r.keys(p,{"hue","saturation","luminance"})
        && member(r,p,"hue",o.hue)
        && member(r,p,"saturation",o.saturation)
        && member(r,p,"luminance",o.luminance);
}
template<> bool fields<CameraRawOptions>(Reader &r,const QVariantMap &p,CameraRawOptions &o) {
    return r.keys(p,{"temperature","tint","exposure","contrast","highlights","shadows","whites","blacks","texture","clarity","dehaze","vibrance","saturation","curveHighlights","curveLights","curveDarks","curveShadows","curveSplits","pointCurve","pointCurveRed","pointCurveGreen","pointCurveBlue","hslHue","hslSaturation","hslLuminance","gradeShadows","gradeMidtones","gradeHighlights","gradeGlobal","gradeBlending","gradeBalance","sharpenAmount","sharpenRadius","sharpenDetail","sharpenMasking","noiseLuminance","noiseLuminanceDetail","noiseColor","noiseColorDetail","grainAmount","grainSize","grainRoughness","vignetteAmount","vignetteMidpoint","vignetteRoundness","vignetteFeather","vignetteHighlights","vignetteStyle","seed","pixelScale"})
        && member(r,p,"temperature",o.temperature)
        && member(r,p,"tint",o.tint)
        && member(r,p,"exposure",o.exposure)
        && member(r,p,"contrast",o.contrast)
        && member(r,p,"highlights",o.highlights)
        && member(r,p,"shadows",o.shadows)
        && member(r,p,"whites",o.whites)
        && member(r,p,"blacks",o.blacks)
        && member(r,p,"texture",o.texture)
        && member(r,p,"clarity",o.clarity)
        && member(r,p,"dehaze",o.dehaze)
        && member(r,p,"vibrance",o.vibrance)
        && member(r,p,"saturation",o.saturation)
        && member(r,p,"curveHighlights",o.curveHighlights)
        && member(r,p,"curveLights",o.curveLights)
        && member(r,p,"curveDarks",o.curveDarks)
        && member(r,p,"curveShadows",o.curveShadows)
        && member(r,p,"curveSplits",o.curveSplits)
        && member(r,p,"pointCurve",o.pointCurve)
        && member(r,p,"pointCurveRed",o.pointCurveRed)
        && member(r,p,"pointCurveGreen",o.pointCurveGreen)
        && member(r,p,"pointCurveBlue",o.pointCurveBlue)
        && member(r,p,"hslHue",o.hslHue)
        && member(r,p,"hslSaturation",o.hslSaturation)
        && member(r,p,"hslLuminance",o.hslLuminance)
        && member(r,p,"gradeShadows",o.gradeShadows)
        && member(r,p,"gradeMidtones",o.gradeMidtones)
        && member(r,p,"gradeHighlights",o.gradeHighlights)
        && member(r,p,"gradeGlobal",o.gradeGlobal)
        && member(r,p,"gradeBlending",o.gradeBlending)
        && member(r,p,"gradeBalance",o.gradeBalance)
        && member(r,p,"sharpenAmount",o.sharpenAmount)
        && member(r,p,"sharpenRadius",o.sharpenRadius)
        && member(r,p,"sharpenDetail",o.sharpenDetail)
        && member(r,p,"sharpenMasking",o.sharpenMasking)
        && member(r,p,"noiseLuminance",o.noiseLuminance)
        && member(r,p,"noiseLuminanceDetail",o.noiseLuminanceDetail)
        && member(r,p,"noiseColor",o.noiseColor)
        && member(r,p,"noiseColorDetail",o.noiseColorDetail)
        && member(r,p,"grainAmount",o.grainAmount)
        && member(r,p,"grainSize",o.grainSize)
        && member(r,p,"grainRoughness",o.grainRoughness)
        && member(r,p,"vignetteAmount",o.vignetteAmount)
        && member(r,p,"vignetteMidpoint",o.vignetteMidpoint)
        && member(r,p,"vignetteRoundness",o.vignetteRoundness)
        && member(r,p,"vignetteFeather",o.vignetteFeather)
        && member(r,p,"vignetteHighlights",o.vignetteHighlights)
        && member(r,p,"vignetteStyle",o.vignetteStyle)
        && member(r,p,"seed",o.seed)
        && member(r,p,"pixelScale",o.pixelScale);
}
template<> bool fields<SpherizeOptions>(Reader &r,const QVariantMap &p,SpherizeOptions &o) {
    return r.keys(p,{"amount","mode"})
        && member(r,p,"amount",o.amount)
        && member(r,p,"mode",o.mode);
}
template<> bool fields<WaveOptions>(Reader &r,const QVariantMap &p,WaveOptions &o) {
    return r.keys(p,{"generators","wavelengthMin","wavelengthMax","amplitudeMin","amplitudeMax","type","edge","seed"})
        && member(r,p,"generators",o.generators)
        && member(r,p,"wavelengthMin",o.wavelengthMin)
        && member(r,p,"wavelengthMax",o.wavelengthMax)
        && member(r,p,"amplitudeMin",o.amplitudeMin)
        && member(r,p,"amplitudeMax",o.amplitudeMax)
        && member(r,p,"type",o.type)
        && member(r,p,"edge",o.edge)
        && member(r,p,"seed",o.seed);
}
template<> bool fields<RippleOptions>(Reader &r,const QVariantMap &p,RippleOptions &o) {
    return r.keys(p,{"amount","size"})
        && member(r,p,"amount",o.amount)
        && member(r,p,"size",o.size);
}
template<> bool fields<DisplaceOptions>(Reader &r,const QVariantMap &p,DisplaceOptions &o) {
    return r.keys(p,{"horizontal","vertical","stretch","edge"})
        && member(r,p,"horizontal",o.horizontal)
        && member(r,p,"vertical",o.vertical)
        && member(r,p,"stretch",o.stretch)
        && member(r,p,"edge",o.edge);
}
template<> bool fields<ShearPoint>(Reader &r,const QVariantMap &p,ShearPoint &o) {
    return r.keys(p,{"position","offset"})
        && member(r,p,"position",o.position)
        && member(r,p,"offset",o.offset);
}
template<> bool fields<ZigZagOptions>(Reader &r,const QVariantMap &p,ZigZagOptions &o) {
    return r.keys(p,{"amount","ridges","style"})
        && member(r,p,"amount",o.amount)
        && member(r,p,"ridges",o.ridges)
        && member(r,p,"style",o.style);
}
template<> bool fields<DevelopEffectsOptions>(Reader &r,const QVariantMap &p,DevelopEffectsOptions &o) {
    return r.keys(p,{"texture","clarity","dehaze"})
        && member(r,p,"texture",o.texture)
        && member(r,p,"clarity",o.clarity)
        && member(r,p,"dehaze",o.dehaze);
}
template<> bool fields<PostCropVignetteOptions>(Reader &r,const QVariantMap &p,PostCropVignetteOptions &o) {
    return r.keys(p,{"amount","midpoint","roundness","feather","highlights","style"})
        && member(r,p,"amount",o.amount)
        && member(r,p,"midpoint",o.midpoint)
        && member(r,p,"roundness",o.roundness)
        && member(r,p,"feather",o.feather)
        && member(r,p,"highlights",o.highlights)
        && member(r,p,"style",o.style);
}
template<> bool fields<FilmGrainOptions>(Reader &r,const QVariantMap &p,FilmGrainOptions &o) {
    return r.keys(p,{"amount","size","roughness","seed"})
        && member(r,p,"amount",o.amount)
        && member(r,p,"size",o.size)
        && member(r,p,"roughness",o.roughness)
        && member(r,p,"seed",o.seed);
}
template<> bool fields<DevelopDetailOptions>(Reader &r,const QVariantMap &p,DevelopDetailOptions &o) {
    return r.keys(p,{"sharpenAmount","sharpenMasking","luminanceNoise","luminanceDetail","colorNoise","colorDetail","colorSmoothness"})
        && member(r,p,"sharpenAmount",o.sharpenAmount)
        && member(r,p,"sharpenMasking",o.sharpenMasking)
        && member(r,p,"luminanceNoise",o.luminanceNoise)
        && member(r,p,"luminanceDetail",o.luminanceDetail)
        && member(r,p,"colorNoise",o.colorNoise)
        && member(r,p,"colorDetail",o.colorDetail)
        && member(r,p,"colorSmoothness",o.colorSmoothness);
}
template<> bool fields<HueSaturationOptions>(Reader &r,const QVariantMap &p,HueSaturationOptions &o) {
    return r.keys(p,{"hue","saturation","lightness","range","falloff","colorize"})
        && member(r,p,"hue",o.hue)
        && member(r,p,"saturation",o.saturation)
        && member(r,p,"lightness",o.lightness)
        && member(r,p,"range",o.range)
        && member(r,p,"falloff",o.falloff)
        && member(r,p,"colorize",o.colorize);
}
template<> bool fields<VibranceOptions>(Reader &r,const QVariantMap &p,VibranceOptions &o) {
    return r.keys(p,{"vibrance","saturation"})
        && member(r,p,"vibrance",o.vibrance)
        && member(r,p,"saturation",o.saturation);
}
template<> bool fields<ToneBalance>(Reader &r,const QVariantMap &p,ToneBalance &o) {
    return r.keys(p,{"cyanRed","magentaGreen","yellowBlue"})
        && member(r,p,"cyanRed",o.cyanRed)
        && member(r,p,"magentaGreen",o.magentaGreen)
        && member(r,p,"yellowBlue",o.yellowBlue);
}
template<> bool fields<ColorBalanceOptions>(Reader &r,const QVariantMap &p,ColorBalanceOptions &o) {
    return r.keys(p,{"shadows","midtones","highlights","preserveLuminosity"})
        && member(r,p,"shadows",o.shadows)
        && member(r,p,"midtones",o.midtones)
        && member(r,p,"highlights",o.highlights)
        && member(r,p,"preserveLuminosity",o.preserveLuminosity);
}
template<> bool fields<BlackWhiteOptions>(Reader &r,const QVariantMap &p,BlackWhiteOptions &o) {
    return r.keys(p,{"weights","tint","tintHue","tintSaturation"})
        && member(r,p,"weights",o.weights)
        && member(r,p,"tint",o.tint)
        && member(r,p,"tintHue",o.tintHue)
        && member(r,p,"tintSaturation",o.tintSaturation);
}
template<> bool fields<PhotoFilterOptions>(Reader &r,const QVariantMap &p,PhotoFilterOptions &o) {
    return r.keys(p,{"color","density","preserveLuminosity"})
        && member(r,p,"color",o.color)
        && member(r,p,"density",o.density)
        && member(r,p,"preserveLuminosity",o.preserveLuminosity);
}
template<> bool fields<ChannelMix>(Reader &r,const QVariantMap &p,ChannelMix &o) {
    return r.keys(p,{"coefficients","constant"})
        && member(r,p,"coefficients",o.coefficients)
        && member(r,p,"constant",o.constant);
}
template<> bool fields<ChannelMixerOptions>(Reader &r,const QVariantMap &p,ChannelMixerOptions &o) {
    return r.keys(p,{"red","green","blue","monochrome","gray"})
        && member(r,p,"red",o.red)
        && member(r,p,"green",o.green)
        && member(r,p,"blue",o.blue)
        && member(r,p,"monochrome",o.monochrome)
        && member(r,p,"gray",o.gray);
}
template<> bool fields<SelectiveColorAdjustment>(Reader &r,const QVariantMap &p,SelectiveColorAdjustment &o) {
    return r.keys(p,{"cyan","magenta","yellow","black"})
        && member(r,p,"cyan",o.cyan)
        && member(r,p,"magenta",o.magenta)
        && member(r,p,"yellow",o.yellow)
        && member(r,p,"black",o.black);
}
template<> bool fields<SelectiveColorOptions>(Reader &r,const QVariantMap &p,SelectiveColorOptions &o) {
    return r.keys(p,{"ranges","method"})
        && member(r,p,"ranges",o.ranges)
        && member(r,p,"method",o.method);
}
template<> bool fields<ReplaceColorOptions>(Reader &r,const QVariantMap &p,ReplaceColorOptions &o) {
    return r.keys(p,{"target","fuzziness","softness","hue","saturation","lightness"})
        && member(r,p,"target",o.target)
        && member(r,p,"fuzziness",o.fuzziness)
        && member(r,p,"softness",o.softness)
        && member(r,p,"hue",o.hue)
        && member(r,p,"saturation",o.saturation)
        && member(r,p,"lightness",o.lightness);
}
template<> bool fields<MatchColorOptions>(Reader &r,const QVariantMap &p,MatchColorOptions &o) {
    return r.keys(p,{"luminance","colorIntensity","fade","neutralize"})
        && member(r,p,"luminance",o.luminance)
        && member(r,p,"colorIntensity",o.colorIntensity)
        && member(r,p,"fade",o.fade)
        && member(r,p,"neutralize",o.neutralize);
}
template<> bool fields<GalleryColoredPencilOptions>(Reader &r,const QVariantMap &p,GalleryColoredPencilOptions &o) {
    return r.keys(p,{"pencilWidth","strokePressure","paperBrightness","background","seed"})
        && member(r,p,"pencilWidth",o.pencilWidth)
        && member(r,p,"strokePressure",o.strokePressure)
        && member(r,p,"paperBrightness",o.paperBrightness)
        && member(r,p,"background",o.background)
        && member(r,p,"seed",o.seed);
}
template<> bool fields<GalleryCutoutOptions>(Reader &r,const QVariantMap &p,GalleryCutoutOptions &o) {
    return r.keys(p,{"numberOfLevels","edgeSimplicity","edgeFidelity"})
        && member(r,p,"numberOfLevels",o.numberOfLevels)
        && member(r,p,"edgeSimplicity",o.edgeSimplicity)
        && member(r,p,"edgeFidelity",o.edgeFidelity);
}
template<> bool fields<GalleryDryBrushOptions>(Reader &r,const QVariantMap &p,GalleryDryBrushOptions &o) {
    return r.keys(p,{"brushSize","brushDetail","texture","seed"})
        && member(r,p,"brushSize",o.brushSize)
        && member(r,p,"brushDetail",o.brushDetail)
        && member(r,p,"texture",o.texture)
        && member(r,p,"seed",o.seed);
}
template<> bool fields<GalleryFilmGrainOptions>(Reader &r,const QVariantMap &p,GalleryFilmGrainOptions &o) {
    return r.keys(p,{"grain","highlightArea","intensity","seed"})
        && member(r,p,"grain",o.grain)
        && member(r,p,"highlightArea",o.highlightArea)
        && member(r,p,"intensity",o.intensity)
        && member(r,p,"seed",o.seed);
}
template<> bool fields<GalleryFrescoOptions>(Reader &r,const QVariantMap &p,GalleryFrescoOptions &o) {
    return r.keys(p,{"brushSize","brushDetail","texture","seed"})
        && member(r,p,"brushSize",o.brushSize)
        && member(r,p,"brushDetail",o.brushDetail)
        && member(r,p,"texture",o.texture)
        && member(r,p,"seed",o.seed);
}
template<> bool fields<GalleryNeonGlowOptions>(Reader &r,const QVariantMap &p,GalleryNeonGlowOptions &o) {
    return r.keys(p,{"glowSize","glowBrightness","glowColor","foreground","background"})
        && member(r,p,"glowSize",o.glowSize)
        && member(r,p,"glowBrightness",o.glowBrightness)
        && member(r,p,"glowColor",o.glowColor)
        && member(r,p,"foreground",o.foreground)
        && member(r,p,"background",o.background);
}
template<> bool fields<GalleryPaintDaubsOptions>(Reader &r,const QVariantMap &p,GalleryPaintDaubsOptions &o) {
    return r.keys(p,{"brushSize","sharpness","brushType","seed"})
        && member(r,p,"brushSize",o.brushSize)
        && member(r,p,"sharpness",o.sharpness)
        && member(r,p,"brushType",o.brushType)
        && member(r,p,"seed",o.seed);
}
template<> bool fields<GalleryPaletteKnifeOptions>(Reader &r,const QVariantMap &p,GalleryPaletteKnifeOptions &o) {
    return r.keys(p,{"strokeSize","strokeDetail","softness"})
        && member(r,p,"strokeSize",o.strokeSize)
        && member(r,p,"strokeDetail",o.strokeDetail)
        && member(r,p,"softness",o.softness);
}
template<> bool fields<GalleryPlasticWrapOptions>(Reader &r,const QVariantMap &p,GalleryPlasticWrapOptions &o) {
    return r.keys(p,{"highlightStrength","detail","smoothness"})
        && member(r,p,"highlightStrength",o.highlightStrength)
        && member(r,p,"detail",o.detail)
        && member(r,p,"smoothness",o.smoothness);
}
template<> bool fields<GalleryPosterEdgesOptions>(Reader &r,const QVariantMap &p,GalleryPosterEdgesOptions &o) {
    return r.keys(p,{"edgeThickness","edgeIntensity","posterization"})
        && member(r,p,"edgeThickness",o.edgeThickness)
        && member(r,p,"edgeIntensity",o.edgeIntensity)
        && member(r,p,"posterization",o.posterization);
}
template<> bool fields<GalleryRoughPastelsOptions>(Reader &r,const QVariantMap &p,GalleryRoughPastelsOptions &o) {
    return r.keys(p,{"strokeLength","strokeDetail","texture","scaling","relief","light","invert","seed"})
        && member(r,p,"strokeLength",o.strokeLength)
        && member(r,p,"strokeDetail",o.strokeDetail)
        && member(r,p,"texture",o.texture)
        && member(r,p,"scaling",o.scaling)
        && member(r,p,"relief",o.relief)
        && member(r,p,"light",o.light)
        && member(r,p,"invert",o.invert)
        && member(r,p,"seed",o.seed);
}
template<> bool fields<GallerySmudgeStickOptions>(Reader &r,const QVariantMap &p,GallerySmudgeStickOptions &o) {
    return r.keys(p,{"strokeLength","highlightArea","intensity"})
        && member(r,p,"strokeLength",o.strokeLength)
        && member(r,p,"highlightArea",o.highlightArea)
        && member(r,p,"intensity",o.intensity);
}
template<> bool fields<GallerySpongeOptions>(Reader &r,const QVariantMap &p,GallerySpongeOptions &o) {
    return r.keys(p,{"brushSize","definition","smoothness","seed"})
        && member(r,p,"brushSize",o.brushSize)
        && member(r,p,"definition",o.definition)
        && member(r,p,"smoothness",o.smoothness)
        && member(r,p,"seed",o.seed);
}
template<> bool fields<GalleryUnderpaintingOptions>(Reader &r,const QVariantMap &p,GalleryUnderpaintingOptions &o) {
    return r.keys(p,{"brushSize","textureCoverage","texture","scaling","relief","light","invert","seed"})
        && member(r,p,"brushSize",o.brushSize)
        && member(r,p,"textureCoverage",o.textureCoverage)
        && member(r,p,"texture",o.texture)
        && member(r,p,"scaling",o.scaling)
        && member(r,p,"relief",o.relief)
        && member(r,p,"light",o.light)
        && member(r,p,"invert",o.invert)
        && member(r,p,"seed",o.seed);
}
template<> bool fields<GalleryWatercolorOptions>(Reader &r,const QVariantMap &p,GalleryWatercolorOptions &o) {
    return r.keys(p,{"brushDetail","shadowIntensity","texture","seed"})
        && member(r,p,"brushDetail",o.brushDetail)
        && member(r,p,"shadowIntensity",o.shadowIntensity)
        && member(r,p,"texture",o.texture)
        && member(r,p,"seed",o.seed);
}
template<> bool fields<GalleryAccentedEdgesOptions>(Reader &r,const QVariantMap &p,GalleryAccentedEdgesOptions &o) {
    return r.keys(p,{"edgeWidth","edgeBrightness","smoothness"})
        && member(r,p,"edgeWidth",o.edgeWidth)
        && member(r,p,"edgeBrightness",o.edgeBrightness)
        && member(r,p,"smoothness",o.smoothness);
}
template<> bool fields<GalleryAngledStrokesOptions>(Reader &r,const QVariantMap &p,GalleryAngledStrokesOptions &o) {
    return r.keys(p,{"directionBalance","strokeLength","sharpness","seed"})
        && member(r,p,"directionBalance",o.directionBalance)
        && member(r,p,"strokeLength",o.strokeLength)
        && member(r,p,"sharpness",o.sharpness)
        && member(r,p,"seed",o.seed);
}
template<> bool fields<GalleryCrosshatchOptions>(Reader &r,const QVariantMap &p,GalleryCrosshatchOptions &o) {
    return r.keys(p,{"strokeLength","sharpness","strength","seed"})
        && member(r,p,"strokeLength",o.strokeLength)
        && member(r,p,"sharpness",o.sharpness)
        && member(r,p,"strength",o.strength)
        && member(r,p,"seed",o.seed);
}
template<> bool fields<GalleryDarkStrokesOptions>(Reader &r,const QVariantMap &p,GalleryDarkStrokesOptions &o) {
    return r.keys(p,{"balance","blackIntensity","whiteIntensity"})
        && member(r,p,"balance",o.balance)
        && member(r,p,"blackIntensity",o.blackIntensity)
        && member(r,p,"whiteIntensity",o.whiteIntensity);
}
template<> bool fields<GalleryInkOutlinesOptions>(Reader &r,const QVariantMap &p,GalleryInkOutlinesOptions &o) {
    return r.keys(p,{"strokeLength","darkIntensity","lightIntensity"})
        && member(r,p,"strokeLength",o.strokeLength)
        && member(r,p,"darkIntensity",o.darkIntensity)
        && member(r,p,"lightIntensity",o.lightIntensity);
}
template<> bool fields<GallerySpatterOptions>(Reader &r,const QVariantMap &p,GallerySpatterOptions &o) {
    return r.keys(p,{"sprayRadius","smoothness","seed"})
        && member(r,p,"sprayRadius",o.sprayRadius)
        && member(r,p,"smoothness",o.smoothness)
        && member(r,p,"seed",o.seed);
}
template<> bool fields<GallerySprayedStrokesOptions>(Reader &r,const QVariantMap &p,GallerySprayedStrokesOptions &o) {
    return r.keys(p,{"strokeLength","sprayRadius","strokeDirection","seed"})
        && member(r,p,"strokeLength",o.strokeLength)
        && member(r,p,"sprayRadius",o.sprayRadius)
        && member(r,p,"strokeDirection",o.strokeDirection)
        && member(r,p,"seed",o.seed);
}
template<> bool fields<GallerySumiEOptions>(Reader &r,const QVariantMap &p,GallerySumiEOptions &o) {
    return r.keys(p,{"strokeWidth","strokePressure","contrast"})
        && member(r,p,"strokeWidth",o.strokeWidth)
        && member(r,p,"strokePressure",o.strokePressure)
        && member(r,p,"contrast",o.contrast);
}
template<> bool fields<GalleryDiffuseGlowOptions>(Reader &r,const QVariantMap &p,GalleryDiffuseGlowOptions &o) {
    return r.keys(p,{"graininess","glowAmount","clearAmount","background","seed"})
        && member(r,p,"graininess",o.graininess)
        && member(r,p,"glowAmount",o.glowAmount)
        && member(r,p,"clearAmount",o.clearAmount)
        && member(r,p,"background",o.background)
        && member(r,p,"seed",o.seed);
}
template<> bool fields<GalleryGlassOptions>(Reader &r,const QVariantMap &p,GalleryGlassOptions &o) {
    return r.keys(p,{"distortion","smoothness","texture","scaling","invert","seed"})
        && member(r,p,"distortion",o.distortion)
        && member(r,p,"smoothness",o.smoothness)
        && member(r,p,"texture",o.texture)
        && member(r,p,"scaling",o.scaling)
        && member(r,p,"invert",o.invert)
        && member(r,p,"seed",o.seed);
}
template<> bool fields<GalleryOceanRippleOptions>(Reader &r,const QVariantMap &p,GalleryOceanRippleOptions &o) {
    return r.keys(p,{"rippleSize","rippleMagnitude","seed"})
        && member(r,p,"rippleSize",o.rippleSize)
        && member(r,p,"rippleMagnitude",o.rippleMagnitude)
        && member(r,p,"seed",o.seed);
}
template<> bool fields<GalleryBasReliefOptions>(Reader &r,const QVariantMap &p,GalleryBasReliefOptions &o) {
    return r.keys(p,{"detail","smoothness","light","foreground","background"})
        && member(r,p,"detail",o.detail)
        && member(r,p,"smoothness",o.smoothness)
        && member(r,p,"light",o.light)
        && member(r,p,"foreground",o.foreground)
        && member(r,p,"background",o.background);
}
template<> bool fields<GalleryChalkCharcoalOptions>(Reader &r,const QVariantMap &p,GalleryChalkCharcoalOptions &o) {
    return r.keys(p,{"charcoalArea","chalkArea","strokePressure","foreground","background","seed"})
        && member(r,p,"charcoalArea",o.charcoalArea)
        && member(r,p,"chalkArea",o.chalkArea)
        && member(r,p,"strokePressure",o.strokePressure)
        && member(r,p,"foreground",o.foreground)
        && member(r,p,"background",o.background)
        && member(r,p,"seed",o.seed);
}
template<> bool fields<GalleryCharcoalOptions>(Reader &r,const QVariantMap &p,GalleryCharcoalOptions &o) {
    return r.keys(p,{"charcoalThickness","detail","lightDarkBalance","foreground","background","seed"})
        && member(r,p,"charcoalThickness",o.charcoalThickness)
        && member(r,p,"detail",o.detail)
        && member(r,p,"lightDarkBalance",o.lightDarkBalance)
        && member(r,p,"foreground",o.foreground)
        && member(r,p,"background",o.background)
        && member(r,p,"seed",o.seed);
}
template<> bool fields<GalleryChromeOptions>(Reader &r,const QVariantMap &p,GalleryChromeOptions &o) {
    return r.keys(p,{"detail","smoothness"})
        && member(r,p,"detail",o.detail)
        && member(r,p,"smoothness",o.smoothness);
}
template<> bool fields<GalleryConteCrayonOptions>(Reader &r,const QVariantMap &p,GalleryConteCrayonOptions &o) {
    return r.keys(p,{"foregroundLevel","backgroundLevel","texture","scaling","relief","light","invert","foreground","background","seed"})
        && member(r,p,"foregroundLevel",o.foregroundLevel)
        && member(r,p,"backgroundLevel",o.backgroundLevel)
        && member(r,p,"texture",o.texture)
        && member(r,p,"scaling",o.scaling)
        && member(r,p,"relief",o.relief)
        && member(r,p,"light",o.light)
        && member(r,p,"invert",o.invert)
        && member(r,p,"foreground",o.foreground)
        && member(r,p,"background",o.background)
        && member(r,p,"seed",o.seed);
}
template<> bool fields<GalleryGraphicPenOptions>(Reader &r,const QVariantMap &p,GalleryGraphicPenOptions &o) {
    return r.keys(p,{"strokeLength","lightDarkBalance","strokeDirection","foreground","background","seed"})
        && member(r,p,"strokeLength",o.strokeLength)
        && member(r,p,"lightDarkBalance",o.lightDarkBalance)
        && member(r,p,"strokeDirection",o.strokeDirection)
        && member(r,p,"foreground",o.foreground)
        && member(r,p,"background",o.background)
        && member(r,p,"seed",o.seed);
}
template<> bool fields<GalleryHalftonePatternOptions>(Reader &r,const QVariantMap &p,GalleryHalftonePatternOptions &o) {
    return r.keys(p,{"size","contrast","patternType","foreground","background"})
        && member(r,p,"size",o.size)
        && member(r,p,"contrast",o.contrast)
        && member(r,p,"patternType",o.patternType)
        && member(r,p,"foreground",o.foreground)
        && member(r,p,"background",o.background);
}
template<> bool fields<GalleryNotePaperOptions>(Reader &r,const QVariantMap &p,GalleryNotePaperOptions &o) {
    return r.keys(p,{"imageBalance","graininess","relief","foreground","background","seed"})
        && member(r,p,"imageBalance",o.imageBalance)
        && member(r,p,"graininess",o.graininess)
        && member(r,p,"relief",o.relief)
        && member(r,p,"foreground",o.foreground)
        && member(r,p,"background",o.background)
        && member(r,p,"seed",o.seed);
}
template<> bool fields<GalleryPhotocopyOptions>(Reader &r,const QVariantMap &p,GalleryPhotocopyOptions &o) {
    return r.keys(p,{"detail","darkness","foreground","background"})
        && member(r,p,"detail",o.detail)
        && member(r,p,"darkness",o.darkness)
        && member(r,p,"foreground",o.foreground)
        && member(r,p,"background",o.background);
}
template<> bool fields<GalleryPlasterOptions>(Reader &r,const QVariantMap &p,GalleryPlasterOptions &o) {
    return r.keys(p,{"imageBalance","smoothness","light","foreground","background"})
        && member(r,p,"imageBalance",o.imageBalance)
        && member(r,p,"smoothness",o.smoothness)
        && member(r,p,"light",o.light)
        && member(r,p,"foreground",o.foreground)
        && member(r,p,"background",o.background);
}
template<> bool fields<GalleryReticulationOptions>(Reader &r,const QVariantMap &p,GalleryReticulationOptions &o) {
    return r.keys(p,{"density","foregroundLevel","backgroundLevel","foreground","background","seed"})
        && member(r,p,"density",o.density)
        && member(r,p,"foregroundLevel",o.foregroundLevel)
        && member(r,p,"backgroundLevel",o.backgroundLevel)
        && member(r,p,"foreground",o.foreground)
        && member(r,p,"background",o.background)
        && member(r,p,"seed",o.seed);
}
template<> bool fields<GalleryStampOptions>(Reader &r,const QVariantMap &p,GalleryStampOptions &o) {
    return r.keys(p,{"lightDarkBalance","smoothness","foreground","background"})
        && member(r,p,"lightDarkBalance",o.lightDarkBalance)
        && member(r,p,"smoothness",o.smoothness)
        && member(r,p,"foreground",o.foreground)
        && member(r,p,"background",o.background);
}
template<> bool fields<GalleryTornEdgesOptions>(Reader &r,const QVariantMap &p,GalleryTornEdgesOptions &o) {
    return r.keys(p,{"imageBalance","smoothness","contrast","foreground","background","seed"})
        && member(r,p,"imageBalance",o.imageBalance)
        && member(r,p,"smoothness",o.smoothness)
        && member(r,p,"contrast",o.contrast)
        && member(r,p,"foreground",o.foreground)
        && member(r,p,"background",o.background)
        && member(r,p,"seed",o.seed);
}
template<> bool fields<GalleryWaterPaperOptions>(Reader &r,const QVariantMap &p,GalleryWaterPaperOptions &o) {
    return r.keys(p,{"fiberLength","brightness","contrast","seed"})
        && member(r,p,"fiberLength",o.fiberLength)
        && member(r,p,"brightness",o.brightness)
        && member(r,p,"contrast",o.contrast)
        && member(r,p,"seed",o.seed);
}
template<> bool fields<GalleryGlowingEdgesOptions>(Reader &r,const QVariantMap &p,GalleryGlowingEdgesOptions &o) {
    return r.keys(p,{"edgeWidth","edgeBrightness","smoothness"})
        && member(r,p,"edgeWidth",o.edgeWidth)
        && member(r,p,"edgeBrightness",o.edgeBrightness)
        && member(r,p,"smoothness",o.smoothness);
}
template<> bool fields<GalleryCraquelureOptions>(Reader &r,const QVariantMap &p,GalleryCraquelureOptions &o) {
    return r.keys(p,{"crackSpacing","crackDepth","crackBrightness","seed"})
        && member(r,p,"crackSpacing",o.crackSpacing)
        && member(r,p,"crackDepth",o.crackDepth)
        && member(r,p,"crackBrightness",o.crackBrightness)
        && member(r,p,"seed",o.seed);
}
template<> bool fields<GalleryGrainOptions>(Reader &r,const QVariantMap &p,GalleryGrainOptions &o) {
    return r.keys(p,{"intensity","contrast","grainType","foreground","background","seed"})
        && member(r,p,"intensity",o.intensity)
        && member(r,p,"contrast",o.contrast)
        && member(r,p,"grainType",o.grainType)
        && member(r,p,"foreground",o.foreground)
        && member(r,p,"background",o.background)
        && member(r,p,"seed",o.seed);
}
template<> bool fields<GalleryMosaicTilesOptions>(Reader &r,const QVariantMap &p,GalleryMosaicTilesOptions &o) {
    return r.keys(p,{"tileSize","groutWidth","lightenGrout","seed"})
        && member(r,p,"tileSize",o.tileSize)
        && member(r,p,"groutWidth",o.groutWidth)
        && member(r,p,"lightenGrout",o.lightenGrout)
        && member(r,p,"seed",o.seed);
}
template<> bool fields<GalleryPatchworkOptions>(Reader &r,const QVariantMap &p,GalleryPatchworkOptions &o) {
    return r.keys(p,{"squareSize","relief","seed"})
        && member(r,p,"squareSize",o.squareSize)
        && member(r,p,"relief",o.relief)
        && member(r,p,"seed",o.seed);
}
template<> bool fields<GalleryStainedGlassOptions>(Reader &r,const QVariantMap &p,GalleryStainedGlassOptions &o) {
    return r.keys(p,{"cellSize","borderThickness","lightIntensity","foreground","seed"})
        && member(r,p,"cellSize",o.cellSize)
        && member(r,p,"borderThickness",o.borderThickness)
        && member(r,p,"lightIntensity",o.lightIntensity)
        && member(r,p,"foreground",o.foreground)
        && member(r,p,"seed",o.seed);
}
template<> bool fields<GalleryTexturizerOptions>(Reader &r,const QVariantMap &p,GalleryTexturizerOptions &o) {
    return r.keys(p,{"texture","scaling","relief","light","invert","seed"})
        && member(r,p,"texture",o.texture)
        && member(r,p,"scaling",o.scaling)
        && member(r,p,"relief",o.relief)
        && member(r,p,"light",o.light)
        && member(r,p,"invert",o.invert)
        && member(r,p,"seed",o.seed);
}
template<> bool fields<SpotOptions>(Reader &r,const QVariantMap &p,SpotOptions &o) {
    return r.keys(p,{"points","size","feather","opacity","sourceOffset"})
        && member(r,p,"points",o.points)
        && member(r,p,"size",o.size)
        && member(r,p,"feather",o.feather)
        && member(r,p,"opacity",o.opacity)
        && member(r,p,"sourceOffset",o.sourceOffset);
}
template<> bool fields<SpotSourceOptions>(Reader &r,const QVariantMap &p,SpotSourceOptions &o) {
    return r.keys(p,{"target","size","avoidOffset"})
        && member(r,p,"target",o.target)
        && member(r,p,"size",o.size)
        && member(r,p,"avoidOffset",o.avoidOffset);
}
template<> bool fields<EyeOptions>(Reader &r,const QVariantMap &p,EyeOptions &o) {
    return r.keys(p,{"center","radiusX","radiusY","pupilSize","darken","catchlight"})
        && member(r,p,"center",o.center)
        && member(r,p,"radiusX",o.radiusX)
        && member(r,p,"radiusY",o.radiusY)
        && member(r,p,"pupilSize",o.pupilSize)
        && member(r,p,"darken",o.darken)
        && member(r,p,"catchlight",o.catchlight);
}
template<> bool fields<VisualizeSpotsOptions>(Reader &r,const QVariantMap &p,VisualizeSpotsOptions &o) {
    return r.keys(p,{"sensitivity"})
        && member(r,p,"sensitivity",o.sensitivity);
}
template<> bool fields<DustDetectionOptions>(Reader &r,const QVariantMap &p,DustDetectionOptions &o) {
    return r.keys(p,{"sensitivity","maxSpots"})
        && member(r,p,"sensitivity",o.sensitivity)
        && member(r,p,"maxSpots",o.maxSpots);
}
template<> bool fields<SharpenOptions>(Reader &r,const QVariantMap &p,SharpenOptions &o) {
    return r.keys(p,{"sigma","amount","threshold"})
        && member(r,p,"sigma",o.sigma)
        && member(r,p,"amount",o.amount)
        && member(r,p,"threshold",o.threshold);
}
template<> bool fields<NoiseOptions>(Reader &r,const QVariantMap &p,NoiseOptions &o) {
    return r.keys(p,{"amount","seed","distribution","monochromatic"})
        && member(r,p,"amount",o.amount)
        && member(r,p,"seed",o.seed)
        && member(r,p,"distribution",o.distribution)
        && member(r,p,"monochromatic",o.monochromatic);
}
template<> bool fields<ColorAdjustment>(Reader &r,const QVariantMap &p,ColorAdjustment &o) {
    return r.keys(p,{"exposure","brightness","contrast","saturation","gamma","temperature","tint","opacity"})
        && member(r,p,"exposure",o.exposure)
        && member(r,p,"brightness",o.brightness)
        && member(r,p,"contrast",o.contrast)
        && member(r,p,"saturation",o.saturation)
        && member(r,p,"gamma",o.gamma)
        && member(r,p,"temperature",o.temperature)
        && member(r,p,"tint",o.tint)
        && member(r,p,"opacity",o.opacity);
}
template<> bool fields<MixerAdjustment>(Reader &r,const QVariantMap &p,MixerAdjustment &o) {
    return r.keys(p,{"hue","saturation","luminance"})
        && member(r,p,"hue",o.hue)
        && member(r,p,"saturation",o.saturation)
        && member(r,p,"luminance",o.luminance);
}
template<> bool fields<ColorWheel>(Reader &r,const QVariantMap &p,ColorWheel &o) {
    return r.keys(p,{"hue","saturation","luminance"})
        && member(r,p,"hue",o.hue)
        && member(r,p,"saturation",o.saturation)
        && member(r,p,"luminance",o.luminance);
}
template<> bool fields<ColorGradingOptions>(Reader &r,const QVariantMap &p,ColorGradingOptions &o) {
    return r.keys(p,{"shadows","midtones","highlights","global","blending","balance"})
        && member(r,p,"shadows",o.shadows)
        && member(r,p,"midtones",o.midtones)
        && member(r,p,"highlights",o.highlights)
        && member(r,p,"global",o.global)
        && member(r,p,"blending",o.blending)
        && member(r,p,"balance",o.balance);
}
template<> bool fields<PointColorOptions>(Reader &r,const QVariantMap &p,PointColorOptions &o) {
    return r.keys(p,{"lightness","chroma","hue","hueShift","saturationShift","luminanceShift","variance","range","hueRange","saturationRange","luminanceRange"})
        && member(r,p,"lightness",o.lightness)
        && member(r,p,"chroma",o.chroma)
        && member(r,p,"hue",o.hue)
        && member(r,p,"hueShift",o.hueShift)
        && member(r,p,"saturationShift",o.saturationShift)
        && member(r,p,"luminanceShift",o.luminanceShift)
        && member(r,p,"variance",o.variance)
        && member(r,p,"range",o.range)
        && member(r,p,"hueRange",o.hueRange)
        && member(r,p,"saturationRange",o.saturationRange)
        && member(r,p,"luminanceRange",o.luminanceRange);
}
template<> bool fields<DevelopColorOptions>(Reader &r,const QVariantMap &p,DevelopColorOptions &o) {
    return r.keys(p,{"vibrance","saturation","mixer","points","blackAndWhite","blackAndWhiteMix","grading"})
        && member(r,p,"vibrance",o.vibrance)
        && member(r,p,"saturation",o.saturation)
        && member(r,p,"mixer",o.mixer)
        && member(r,p,"points",o.points)
        && member(r,p,"blackAndWhite",o.blackAndWhite)
        && member(r,p,"blackAndWhiteMix",o.blackAndWhiteMix)
        && member(r,p,"grading",o.grading);
}
template<> bool fields<CalibrationOptions>(Reader &r,const QVariantMap &p,CalibrationOptions &o) {
    return r.keys(p,{"shadowsTint","primaryHue","primarySaturation"})
        && member(r,p,"shadowsTint",o.shadowsTint)
        && member(r,p,"primaryHue",o.primaryHue)
        && member(r,p,"primarySaturation",o.primarySaturation);
}
template<> bool fields<HdrRgba>(Reader &r,const QVariantMap &p,HdrRgba &o) {
    return r.keys(p,{"r","g","b","a"})
        && member(r,p,"r",o.r)
        && member(r,p,"g",o.g)
        && member(r,p,"b",o.b)
        && member(r,p,"a",o.a);
}
template<> bool fields<HdrToningOptions>(Reader &r,const QVariantMap &p,HdrToningOptions &o) {
    return r.keys(p,{"method","exposure","gamma"})
        && member(r,p,"method",o.method)
        && member(r,p,"exposure",o.exposure)
        && member(r,p,"gamma",o.gamma);
}
template<> bool fields<CloudsOptions>(Reader &r,const QVariantMap &p,CloudsOptions &o) {
    return r.keys(p,{"foreground","background","seed"})
        && member(r,p,"foreground",o.foreground)
        && member(r,p,"background",o.background)
        && member(r,p,"seed",o.seed);
}
template<> bool fields<FibersOptions>(Reader &r,const QVariantMap &p,FibersOptions &o) {
    return r.keys(p,{"variance","strength","seed","foreground","background"})
        && member(r,p,"variance",o.variance)
        && member(r,p,"strength",o.strength)
        && member(r,p,"seed",o.seed)
        && member(r,p,"foreground",o.foreground)
        && member(r,p,"background",o.background);
}
template<> bool fields<LensFlareOptions>(Reader &r,const QVariantMap &p,LensFlareOptions &o) {
    return r.keys(p,{"brightness","centerX","centerY","lens"})
        && member(r,p,"brightness",o.brightness)
        && member(r,p,"centerX",o.centerX)
        && member(r,p,"centerY",o.centerY)
        && member(r,p,"lens",o.lens);
}
template<> bool fields<FilterLight>(Reader &r,const QVariantMap &p,FilterLight &o) {
    return r.keys(p,{"kind","x","y","z","targetX","targetY","angle","elevation","color","intensity","cone","hotspot","radius"})
        && member(r,p,"kind",o.kind)
        && member(r,p,"x",o.x)
        && member(r,p,"y",o.y)
        && member(r,p,"z",o.z)
        && member(r,p,"targetX",o.targetX)
        && member(r,p,"targetY",o.targetY)
        && member(r,p,"angle",o.angle)
        && member(r,p,"elevation",o.elevation)
        && member(r,p,"color",o.color)
        && member(r,p,"intensity",o.intensity)
        && member(r,p,"cone",o.cone)
        && member(r,p,"hotspot",o.hotspot)
        && member(r,p,"radius",o.radius);
}
template<> bool fields<LightingEffectsOptions>(Reader &r,const QVariantMap &p,LightingEffectsOptions &o) {
    return r.keys(p,{"lights","gloss","metallic","exposure","ambience","texture","height","whiteIsHigh"})
        && member(r,p,"lights",o.lights)
        && member(r,p,"gloss",o.gloss)
        && member(r,p,"metallic",o.metallic)
        && member(r,p,"exposure",o.exposure)
        && member(r,p,"ambience",o.ambience)
        && member(r,p,"texture",o.texture)
        && member(r,p,"height",o.height)
        && member(r,p,"whiteIsHigh",o.whiteIsHigh);
}
template<> bool fields<RelightOptions>(Reader &r,const QVariantMap &p,RelightOptions &o) {
    return r.keys(p,{"angle","elevation","intensity","ambient","warmth","softness"})
        && member(r,p,"angle",o.angle)
        && member(r,p,"elevation",o.elevation)
        && member(r,p,"intensity",o.intensity)
        && member(r,p,"ambient",o.ambient)
        && member(r,p,"warmth",o.warmth)
        && member(r,p,"softness",o.softness);
}
template<> bool fields<FlameOptions>(Reader &r,const QVariantMap &p,FlameOptions &o) {
    return r.keys(p,{"type","length","width","angle","interval","randomizeLength","adjustIntervalForLoops","color","turbulence","jaggedness","opacity","flameLines","bottomAlignment","style","shape","randomizeShapes","seed","quality","paths"})
        && member(r,p,"type",o.type)
        && member(r,p,"length",o.length)
        && member(r,p,"width",o.width)
        && member(r,p,"angle",o.angle)
        && member(r,p,"interval",o.interval)
        && member(r,p,"randomizeLength",o.randomizeLength)
        && member(r,p,"adjustIntervalForLoops",o.adjustIntervalForLoops)
        && member(r,p,"color",o.color)
        && member(r,p,"turbulence",o.turbulence)
        && member(r,p,"jaggedness",o.jaggedness)
        && member(r,p,"opacity",o.opacity)
        && member(r,p,"flameLines",o.flameLines)
        && member(r,p,"bottomAlignment",o.bottomAlignment)
        && member(r,p,"style",o.style)
        && member(r,p,"shape",o.shape)
        && member(r,p,"randomizeShapes",o.randomizeShapes)
        && member(r,p,"seed",o.seed)
        && member(r,p,"quality",o.quality)
        && member(r,p,"paths",o.paths);
}
template<> bool fields<TreeOptions>(Reader &r,const QVariantMap &p,TreeOptions &o) {
    return r.keys(p,{"baseTreeType","lightDirection","leavesAmount","leavesSize","branchesHeight","branchesThickness","defaultLeaves","customBranchColor","flatShading","leavesColor","branchesColor","seed","x","y","size"})
        && member(r,p,"baseTreeType",o.baseTreeType)
        && member(r,p,"lightDirection",o.lightDirection)
        && member(r,p,"leavesAmount",o.leavesAmount)
        && member(r,p,"leavesSize",o.leavesSize)
        && member(r,p,"branchesHeight",o.branchesHeight)
        && member(r,p,"branchesThickness",o.branchesThickness)
        && member(r,p,"defaultLeaves",o.defaultLeaves)
        && member(r,p,"customBranchColor",o.customBranchColor)
        && member(r,p,"flatShading",o.flatShading)
        && member(r,p,"leavesColor",o.leavesColor)
        && member(r,p,"branchesColor",o.branchesColor)
        && member(r,p,"seed",o.seed)
        && member(r,p,"x",o.x)
        && member(r,p,"y",o.y)
        && member(r,p,"size",o.size);
}
template<> bool fields<PictureFrameOptions>(Reader &r,const QVariantMap &p,PictureFrameOptions &o) {
    return r.keys(p,{"style","margin","size","arrangement","thickness","fade","lines","vineColor","flowerColor","leafColor","seed"})
        && member(r,p,"style",o.style)
        && member(r,p,"margin",o.margin)
        && member(r,p,"size",o.size)
        && member(r,p,"arrangement",o.arrangement)
        && member(r,p,"thickness",o.thickness)
        && member(r,p,"fade",o.fade)
        && member(r,p,"lines",o.lines)
        && member(r,p,"vineColor",o.vineColor)
        && member(r,p,"flowerColor",o.flowerColor)
        && member(r,p,"leafColor",o.leafColor)
        && member(r,p,"seed",o.seed);
}
template<> bool fields<BrightnessContrastOptions>(Reader &r,const QVariantMap &p,BrightnessContrastOptions &o) {
    return r.keys(p,{"brightness","contrast"})
        && member(r,p,"brightness",o.brightness)
        && member(r,p,"contrast",o.contrast);
}
template<> bool fields<LevelChannel>(Reader &r,const QVariantMap &p,LevelChannel &o) {
    return r.keys(p,{"inputBlack","inputWhite","gamma","outputBlack","outputWhite"})
        && member(r,p,"inputBlack",o.inputBlack)
        && member(r,p,"inputWhite",o.inputWhite)
        && member(r,p,"gamma",o.gamma)
        && member(r,p,"outputBlack",o.outputBlack)
        && member(r,p,"outputWhite",o.outputWhite);
}
template<> bool fields<LevelsOptions>(Reader &r,const QVariantMap &p,LevelsOptions &o) {
    return r.keys(p,{"master","red","green","blue"})
        && member(r,p,"master",o.master)
        && member(r,p,"red",o.red)
        && member(r,p,"green",o.green)
        && member(r,p,"blue",o.blue);
}
template<> bool fields<CurvePoint>(Reader &r,const QVariantMap &p,CurvePoint &o) {
    return r.keys(p,{"input","output"})
        && member(r,p,"input",o.input)
        && member(r,p,"output",o.output);
}
template<> bool fields<CurveChannel>(Reader &r,const QVariantMap &p,CurveChannel &o) {
    return r.keys(p,{"points"})
        && member(r,p,"points",o.points);
}
template<> bool fields<CurvesOptions>(Reader &r,const QVariantMap &p,CurvesOptions &o) {
    return r.keys(p,{"master","red","green","blue"})
        && member(r,p,"master",o.master)
        && member(r,p,"red",o.red)
        && member(r,p,"green",o.green)
        && member(r,p,"blue",o.blue);
}
template<> bool fields<ExposureOptions>(Reader &r,const QVariantMap &p,ExposureOptions &o) {
    return r.keys(p,{"stops","offset","gamma"})
        && member(r,p,"stops",o.stops)
        && member(r,p,"offset",o.offset)
        && member(r,p,"gamma",o.gamma);
}
template<> bool fields<ShadowsHighlightsOptions>(Reader &r,const QVariantMap &p,ShadowsHighlightsOptions &o) {
    return r.keys(p,{"shadows","highlights","shadowWidth","highlightWidth"})
        && member(r,p,"shadows",o.shadows)
        && member(r,p,"highlights",o.highlights)
        && member(r,p,"shadowWidth",o.shadowWidth)
        && member(r,p,"highlightWidth",o.highlightWidth);
}
template<> bool fields<AutoToneOptions>(Reader &r,const QVariantMap &p,AutoToneOptions &o) {
    return r.keys(p,{"blackClip","whiteClip"})
        && member(r,p,"blackClip",o.blackClip)
        && member(r,p,"whiteClip",o.whiteClip);
}
template<> bool fields<MotionBlurOptions>(Reader &r,const QVariantMap &p,MotionBlurOptions &o) {
    return r.keys(p,{"angle","distance"})
        && member(r,p,"angle",o.angle)
        && member(r,p,"distance",o.distance);
}
template<> bool fields<RadialBlurOptions>(Reader &r,const QVariantMap &p,RadialBlurOptions &o) {
    return r.keys(p,{"amount","method","centerX","centerY"})
        && member(r,p,"amount",o.amount)
        && member(r,p,"method",o.method)
        && member(r,p,"centerX",o.centerX)
        && member(r,p,"centerY",o.centerY);
}
template<> bool fields<SurfaceBlurOptions>(Reader &r,const QVariantMap &p,SurfaceBlurOptions &o) {
    return r.keys(p,{"radius","threshold"})
        && member(r,p,"radius",o.radius)
        && member(r,p,"threshold",o.threshold);
}
template<> bool fields<UnsharpMaskOptions>(Reader &r,const QVariantMap &p,UnsharpMaskOptions &o) {
    return r.keys(p,{"amount","radius","threshold"})
        && member(r,p,"amount",o.amount)
        && member(r,p,"radius",o.radius)
        && member(r,p,"threshold",o.threshold);
}
template<> bool fields<SmartSharpenOptions>(Reader &r,const QVariantMap &p,SmartSharpenOptions &o) {
    return r.keys(p,{"amount","radius","reduceNoise"})
        && member(r,p,"amount",o.amount)
        && member(r,p,"radius",o.radius)
        && member(r,p,"reduceNoise",o.reduceNoise);
}
template<> bool fields<SmartBlurOptions>(Reader &r,const QVariantMap &p,SmartBlurOptions &o) {
    return r.keys(p,{"radius","threshold","quality","mode"})
        && member(r,p,"radius",o.radius)
        && member(r,p,"threshold",o.threshold)
        && member(r,p,"quality",o.quality)
        && member(r,p,"mode",o.mode);
}
template<> bool fields<ShapeBlurOptions>(Reader &r,const QVariantMap &p,ShapeBlurOptions &o) {
    return r.keys(p,{"radius","shape"})
        && member(r,p,"radius",o.radius)
        && member(r,p,"shape",o.shape);
}
template<> bool fields<LensBlurOptions>(Reader &r,const QVariantMap &p,LensBlurOptions &o) {
    return r.keys(p,{"radius","blades","curvature","rotation","depth","focalDistance","invertDepth","brightness","threshold","noise","distribution","monochromatic","seed","depthMap"})
        && member(r,p,"radius",o.radius)
        && member(r,p,"blades",o.blades)
        && member(r,p,"curvature",o.curvature)
        && member(r,p,"rotation",o.rotation)
        && member(r,p,"depth",o.depth)
        && member(r,p,"focalDistance",o.focalDistance)
        && member(r,p,"invertDepth",o.invertDepth)
        && member(r,p,"brightness",o.brightness)
        && member(r,p,"threshold",o.threshold)
        && member(r,p,"noise",o.noise)
        && member(r,p,"distribution",o.distribution)
        && member(r,p,"monochromatic",o.monochromatic)
        && member(r,p,"seed",o.seed)
        && member(r,p,"depthMap",o.depthMap);
}
template<> bool fields<TiltShiftOptions>(Reader &r,const QVariantMap &p,TiltShiftOptions &o) {
    return r.keys(p,{"blur","centerX","centerY","angle","focus","transition"})
        && member(r,p,"blur",o.blur)
        && member(r,p,"centerX",o.centerX)
        && member(r,p,"centerY",o.centerY)
        && member(r,p,"angle",o.angle)
        && member(r,p,"focus",o.focus)
        && member(r,p,"transition",o.transition);
}
template<> bool fields<IrisBlurPin>(Reader &r,const QVariantMap &p,IrisBlurPin &o) {
    return r.keys(p,{"x","y","radiusX","radiusY","angle","roundness","feather","blur"})
        && member(r,p,"x",o.x)
        && member(r,p,"y",o.y)
        && member(r,p,"radiusX",o.radiusX)
        && member(r,p,"radiusY",o.radiusY)
        && member(r,p,"angle",o.angle)
        && member(r,p,"roundness",o.roundness)
        && member(r,p,"feather",o.feather)
        && member(r,p,"blur",o.blur);
}
template<> bool fields<FieldBlurPin>(Reader &r,const QVariantMap &p,FieldBlurPin &o) {
    return r.keys(p,{"x","y","blur"})
        && member(r,p,"x",o.x)
        && member(r,p,"y",o.y)
        && member(r,p,"blur",o.blur);
}
template<> bool fields<SpinBlurPin>(Reader &r,const QVariantMap &p,SpinBlurPin &o) {
    return r.keys(p,{"x","y","radiusX","radiusY","angle","blurAngle"})
        && member(r,p,"x",o.x)
        && member(r,p,"y",o.y)
        && member(r,p,"radiusX",o.radiusX)
        && member(r,p,"radiusY",o.radiusY)
        && member(r,p,"angle",o.angle)
        && member(r,p,"blurAngle",o.blurAngle);
}
template<> bool fields<BlurPathPoint>(Reader &r,const QVariantMap &p,BlurPathPoint &o) {
    return r.keys(p,{"x","y"})
        && member(r,p,"x",o.x)
        && member(r,p,"y",o.y);
}
template<> bool fields<BlurPath>(Reader &r,const QVariantMap &p,BlurPath &o) {
    return r.keys(p,{"points","speed","taper"})
        && member(r,p,"points",o.points)
        && member(r,p,"speed",o.speed)
        && member(r,p,"taper",o.taper);
}
template<> bool fields<WhiteBalanceOptions>(Reader &r,const QVariantMap &p,WhiteBalanceOptions &o) {
    return r.keys(p,{"temperature","tint","asShotTemperature","asShotTint"})
        && member(r,p,"temperature",o.temperature)
        && member(r,p,"tint",o.tint)
        && member(r,p,"asShotTemperature",o.asShotTemperature)
        && member(r,p,"asShotTint",o.asShotTint);
}
template<> bool fields<DevelopLightOptions>(Reader &r,const QVariantMap &p,DevelopLightOptions &o) {
    return r.keys(p,{"exposure","contrast","highlights","shadows","whites","blacks"})
        && member(r,p,"exposure",o.exposure)
        && member(r,p,"contrast",o.contrast)
        && member(r,p,"highlights",o.highlights)
        && member(r,p,"shadows",o.shadows)
        && member(r,p,"whites",o.whites)
        && member(r,p,"blacks",o.blacks);
}
template<> bool fields<DevelopCurveOptions>(Reader &r,const QVariantMap &p,DevelopCurveOptions &o) {
    return r.keys(p,{"shadows","darks","lights","highlights","splitShadows","splitMid","splitHighlights","master","red","green","blue","refineSaturation"})
        && member(r,p,"shadows",o.shadows)
        && member(r,p,"darks",o.darks)
        && member(r,p,"lights",o.lights)
        && member(r,p,"highlights",o.highlights)
        && member(r,p,"splitShadows",o.splitShadows)
        && member(r,p,"splitMid",o.splitMid)
        && member(r,p,"splitHighlights",o.splitHighlights)
        && member(r,p,"master",o.master)
        && member(r,p,"red",o.red)
        && member(r,p,"green",o.green)
        && member(r,p,"blue",o.blue)
        && member(r,p,"refineSaturation",o.refineSaturation);
}
template<> bool fields<FilterNoiseOptions>(Reader &r,const QVariantMap &p,FilterNoiseOptions &o) {
    return r.keys(p,{"amount","distribution","monochromatic","seed"})
        && member(r,p,"amount",o.amount)
        && member(r,p,"distribution",o.distribution)
        && member(r,p,"monochromatic",o.monochromatic)
        && member(r,p,"seed",o.seed);
}
template<> bool fields<DustAndScratchesOptions>(Reader &r,const QVariantMap &p,DustAndScratchesOptions &o) {
    return r.keys(p,{"radius","threshold"})
        && member(r,p,"radius",o.radius)
        && member(r,p,"threshold",o.threshold);
}
template<> bool fields<MorphologyOptions>(Reader &r,const QVariantMap &p,MorphologyOptions &o) {
    return r.keys(p,{"radius","shape","affectAlpha"})
        && member(r,p,"radius",o.radius)
        && member(r,p,"shape",o.shape)
        && member(r,p,"affectAlpha",o.affectAlpha);
}
template<> bool fields<ReduceNoiseOptions>(Reader &r,const QVariantMap &p,ReduceNoiseOptions &o) {
    return r.keys(p,{"strength","preserveDetails","reduceColorNoise","sharpenDetails","removeJpegArtifact"})
        && member(r,p,"strength",o.strength)
        && member(r,p,"preserveDetails",o.preserveDetails)
        && member(r,p,"reduceColorNoise",o.reduceColorNoise)
        && member(r,p,"sharpenDetails",o.sharpenDetails)
        && member(r,p,"removeJpegArtifact",o.removeJpegArtifact);
}
template<> bool fields<CustomFilterOptions>(Reader &r,const QVariantMap &p,CustomFilterOptions &o) {
    return r.keys(p,{"kernel","scale","offset"})
        && member(r,p,"kernel",o.kernel)
        && member(r,p,"scale",o.scale)
        && member(r,p,"offset",o.offset);
}
template<> bool fields<GuidedBlurOptions>(Reader &r,const QVariantMap &p,GuidedBlurOptions &o) {
    return r.keys(p,{"radius","epsilon"})
        && member(r,p,"radius",o.radius)
        && member(r,p,"epsilon",o.epsilon);
}
template<> bool fields<LiquifyPoint>(Reader &r,const QVariantMap &p,LiquifyPoint &o) {
    return r.keys(p,{"position","pressure"})
        && member(r,p,"position",o.position)
        && member(r,p,"pressure",o.pressure);
}
template<> bool fields<LiquifyStroke>(Reader &r,const QVariantMap &p,LiquifyStroke &o) {
    return r.keys(p,{"tool","size","density","pressure","rate","points","amount"})
        && member(r,p,"tool",o.tool)
        && member(r,p,"size",o.size)
        && member(r,p,"density",o.density)
        && member(r,p,"pressure",o.pressure)
        && member(r,p,"rate",o.rate)
        && member(r,p,"points",o.points)
        && member(r,p,"amount",o.amount);
}
template<> bool fields<LiquifyOptions>(Reader &r,const QVariantMap &p,LiquifyOptions &o) {
    return r.keys(p,{"cell","strokes"})
        && member(r,p,"cell",o.cell)
        && member(r,p,"strokes",o.strokes);
}
template<> bool fields<WideAngleConstraint>(Reader &r,const QVariantMap &p,WideAngleConstraint &o) {
    return r.keys(p,{"a","b","orientation"})
        && member(r,p,"a",o.a)
        && member(r,p,"b",o.b)
        && member(r,p,"orientation",o.orientation);
}
template<> bool fields<AdaptiveWideAngleOptions>(Reader &r,const QVariantMap &p,AdaptiveWideAngleOptions &o) {
    return r.keys(p,{"model","focalLength","cropFactor","scale","constraints","meshCells","solverIterations","interpolation"})
        && member(r,p,"model",o.model)
        && member(r,p,"focalLength",o.focalLength)
        && member(r,p,"cropFactor",o.cropFactor)
        && member(r,p,"scale",o.scale)
        && member(r,p,"constraints",o.constraints)
        && member(r,p,"meshCells",o.meshCells)
        && member(r,p,"solverIterations",o.solverIterations)
        && member(r,p,"interpolation",o.interpolation);
}
template<> bool fields<PuppetPin>(Reader &r,const QVariantMap &p,PuppetPin &o) {
    return r.keys(p,{"source","destination","rotation","depth"})
        && member(r,p,"source",o.source)
        && member(r,p,"destination",o.destination)
        && member(r,p,"rotation",o.rotation)
        && member(r,p,"depth",o.depth);
}
template<> bool fields<PuppetWarpOptions>(Reader &r,const QVariantMap &p,PuppetWarpOptions &o) {
    return r.keys(p,{"pins","mode","density","expansion","iterations","solverIterations","interpolation"})
        && member(r,p,"pins",o.pins)
        && member(r,p,"mode",o.mode)
        && member(r,p,"density",o.density)
        && member(r,p,"expansion",o.expansion)
        && member(r,p,"iterations",o.iterations)
        && member(r,p,"solverIterations",o.solverIterations)
        && member(r,p,"interpolation",o.interpolation);
}
template<> bool fields<PerspectivePlane>(Reader &r,const QVariantMap &p,PerspectivePlane &o) {
    return r.keys(p,{"source","destination"})
        && member(r,p,"source",o.source)
        && member(r,p,"destination",o.destination);
}
template<> bool fields<PerspectiveWarpOptions>(Reader &r,const QVariantMap &p,PerspectiveWarpOptions &o) {
    return r.keys(p,{"planes","straighten","cell","interpolation"})
        && member(r,p,"planes",o.planes)
        && member(r,p,"straighten",o.straighten)
        && member(r,p,"cell",o.cell)
        && member(r,p,"interpolation",o.interpolation);
}
template<> bool fields<VanishingPlane>(Reader &r,const QVariantMap &p,VanishingPlane &o) {
    return r.keys(p,{"corners"})
        && member(r,p,"corners",o.corners);
}
template<> bool fields<VanishingTearOff>(Reader &r,const QVariantMap &p,VanishingTearOff &o) {
    return r.keys(p,{"from","edge","angle","depth"})
        && member(r,p,"from",o.from)
        && member(r,p,"edge",o.edge)
        && member(r,p,"angle",o.angle)
        && member(r,p,"depth",o.depth);
}
template<> bool fields<VanishingPaste>(Reader &r,const QVariantMap &p,VanishingPaste &o) {
    return r.keys(p,{"image","plane","at","width","opacity"})
        && member(r,p,"image",o.image)
        && member(r,p,"plane",o.plane)
        && member(r,p,"at",o.at)
        && member(r,p,"width",o.width)
        && member(r,p,"opacity",o.opacity);
}
template<> bool fields<VanishingCloneStroke>(Reader &r,const QVariantMap &p,VanishingCloneStroke &o) {
    return r.keys(p,{"source","points","size","hardness","opacity"})
        && member(r,p,"source",o.source)
        && member(r,p,"points",o.points)
        && member(r,p,"size",o.size)
        && member(r,p,"hardness",o.hardness)
        && member(r,p,"opacity",o.opacity);
}
template<> bool fields<VanishingPointOptions>(Reader &r,const QVariantMap &p,VanishingPointOptions &o) {
    return r.keys(p,{"planes","tearOffs","focalLength","pastes","clones"})
        && member(r,p,"planes",o.planes)
        && member(r,p,"tearOffs",o.tearOffs)
        && member(r,p,"focalLength",o.focalLength)
        && member(r,p,"pastes",o.pastes)
        && member(r,p,"clones",o.clones);
}
template<> bool fields<MaskOverlayOptions>(Reader &r,const QVariantMap &p,MaskOverlayOptions &o) {
    return r.keys(p,{"color","opacity"})
        && member(r,p,"color",o.color)
        && member(r,p,"opacity",o.opacity);
}
template<> bool fields<GamutWarningOptions>(Reader &r,const QVariantMap &p,GamutWarningOptions &o) {
    return r.keys(p,{"proofSpace","linearRec2020"})
        && member(r,p,"proofSpace",o.proofSpace)
        && member(r,p,"linearRec2020",o.linearRec2020);
}
template<> bool fields<DevelopOptions>(Reader &r,const QVariantMap &p,DevelopOptions &o) {
    return r.keys(p,{"optics","whiteBalance","detail","effects","calibration","light","color","vignette","curve","grain"})
        && member(r,p,"optics",o.optics)
        && member(r,p,"whiteBalance",o.whiteBalance)
        && member(r,p,"detail",o.detail)
        && member(r,p,"effects",o.effects)
        && member(r,p,"calibration",o.calibration)
        && member(r,p,"light",o.light)
        && member(r,p,"color",o.color)
        && member(r,p,"vignette",o.vignette)
        && member(r,p,"curve",o.curve)
        && member(r,p,"grain",o.grain);
}
template<> bool fields<LensFilterProfile>(Reader &r,const QVariantMap &p,LensFilterProfile &o) {
    return r.keys(p,{"k1","k2","p1","p2","vignette","caRed","caBlue"})
        && member(r,p,"k1",o.k1)
        && member(r,p,"k2",o.k2)
        && member(r,p,"p1",o.p1)
        && member(r,p,"p2",o.p2)
        && member(r,p,"vignette",o.vignette)
        && member(r,p,"caRed",o.caRed)
        && member(r,p,"caBlue",o.caBlue);
}
template<> bool fields<LensCorrectionOptions>(Reader &r,const QVariantMap &p,LensCorrectionOptions &o) {
    return r.keys(p,{"profile","correctDistortion","correctVignette","correctCa","distortion","redCyan","blueYellow","vignetteAmount","vignetteMidpoint","vertical","horizontal","angle","scale","edge","edgeColor"})
        && member(r,p,"profile",o.profile)
        && member(r,p,"correctDistortion",o.correctDistortion)
        && member(r,p,"correctVignette",o.correctVignette)
        && member(r,p,"correctCa",o.correctCa)
        && member(r,p,"distortion",o.distortion)
        && member(r,p,"redCyan",o.redCyan)
        && member(r,p,"blueYellow",o.blueYellow)
        && member(r,p,"vignetteAmount",o.vignetteAmount)
        && member(r,p,"vignetteMidpoint",o.vignetteMidpoint)
        && member(r,p,"vertical",o.vertical)
        && member(r,p,"horizontal",o.horizontal)
        && member(r,p,"angle",o.angle)
        && member(r,p,"scale",o.scale)
        && member(r,p,"edge",o.edge)
        && member(r,p,"edgeColor",o.edgeColor);
}
template<> bool fields<EmbeddedLensWarp>(Reader &r,const QVariantMap &p,EmbeddedLensWarp &o) {
    return r.keys(p,{"planes","centerX","centerY","radius"})
        && member(r,p,"planes",o.planes)
        && member(r,p,"centerX",o.centerX)
        && member(r,p,"centerY",o.centerY)
        && member(r,p,"radius",o.radius);
}
template<> bool fields<EmbeddedLensVignette>(Reader &r,const QVariantMap &p,EmbeddedLensVignette &o) {
    return r.keys(p,{"coefficients","centerX","centerY","radius"})
        && member(r,p,"coefficients",o.coefficients)
        && member(r,p,"centerX",o.centerX)
        && member(r,p,"centerY",o.centerY)
        && member(r,p,"radius",o.radius);
}
template<> bool fields<OpticsOptions>(Reader &r,const QVariantMap &p,OpticsOptions &o) {
    return r.keys(p,{"distortion","vignetting","vignettingMidpoint","redChromaticAberration","blueChromaticAberration","autoChromaticAberration","purpleDefringe","greenDefringe","purpleHueLow","purpleHueHigh","greenHueLow","greenHueHigh","embeddedWarp","embeddedVignette","profileDistortion","profileVignetting"})
        && member(r,p,"distortion",o.distortion)
        && member(r,p,"vignetting",o.vignetting)
        && member(r,p,"vignettingMidpoint",o.vignettingMidpoint)
        && member(r,p,"redChromaticAberration",o.redChromaticAberration)
        && member(r,p,"blueChromaticAberration",o.blueChromaticAberration)
        && member(r,p,"autoChromaticAberration",o.autoChromaticAberration)
        && member(r,p,"purpleDefringe",o.purpleDefringe)
        && member(r,p,"greenDefringe",o.greenDefringe)
        && member(r,p,"purpleHueLow",o.purpleHueLow)
        && member(r,p,"purpleHueHigh",o.purpleHueHigh)
        && member(r,p,"greenHueLow",o.greenHueLow)
        && member(r,p,"greenHueHigh",o.greenHueHigh)
        && member(r,p,"embeddedWarp",o.embeddedWarp)
        && member(r,p,"embeddedVignette",o.embeddedVignette)
        && member(r,p,"profileDistortion",o.profileDistortion)
        && member(r,p,"profileVignetting",o.profileVignetting);
}
template<> bool fields<ColorHalftoneOptions>(Reader &r,const QVariantMap &p,ColorHalftoneOptions &o) {
    return r.keys(p,{"maximumRadius","angles"})
        && member(r,p,"maximumRadius",o.maximumRadius)
        && member(r,p,"angles",o.angles);
}
template<> bool fields<CrystallizeOptions>(Reader &r,const QVariantMap &p,CrystallizeOptions &o) {
    return r.keys(p,{"cellSize","seed"})
        && member(r,p,"cellSize",o.cellSize)
        && member(r,p,"seed",o.seed);
}
template<> bool fields<MezzotintOptions>(Reader &r,const QVariantMap &p,MezzotintOptions &o) {
    return r.keys(p,{"pattern","seed"})
        && member(r,p,"pattern",o.pattern)
        && member(r,p,"seed",o.seed);
}
template<> bool fields<PointillizeOptions>(Reader &r,const QVariantMap &p,PointillizeOptions &o) {
    return r.keys(p,{"cellSize","seed","background"})
        && member(r,p,"cellSize",o.cellSize)
        && member(r,p,"seed",o.seed)
        && member(r,p,"background",o.background);
}
template<> bool fields<EmbossOptions>(Reader &r,const QVariantMap &p,EmbossOptions &o) {
    return r.keys(p,{"angle","height","amount"})
        && member(r,p,"angle",o.angle)
        && member(r,p,"height",o.height)
        && member(r,p,"amount",o.amount);
}
template<> bool fields<DiffuseOptions>(Reader &r,const QVariantMap &p,DiffuseOptions &o) {
    return r.keys(p,{"mode","seed"})
        && member(r,p,"mode",o.mode)
        && member(r,p,"seed",o.seed);
}
template<> bool fields<ExtrudeOptions>(Reader &r,const QVariantMap &p,ExtrudeOptions &o) {
    return r.keys(p,{"shape","size","depth","levelBased","solidFrontFaces","maskIncompleteBlocks","seed"})
        && member(r,p,"shape",o.shape)
        && member(r,p,"size",o.size)
        && member(r,p,"depth",o.depth)
        && member(r,p,"levelBased",o.levelBased)
        && member(r,p,"solidFrontFaces",o.solidFrontFaces)
        && member(r,p,"maskIncompleteBlocks",o.maskIncompleteBlocks)
        && member(r,p,"seed",o.seed);
}
template<> bool fields<OilPaintOptions>(Reader &r,const QVariantMap &p,OilPaintOptions &o) {
    return r.keys(p,{"stylization","cleanliness","scale","bristleDetail","lighting","angle","shine"})
        && member(r,p,"stylization",o.stylization)
        && member(r,p,"cleanliness",o.cleanliness)
        && member(r,p,"scale",o.scale)
        && member(r,p,"bristleDetail",o.bristleDetail)
        && member(r,p,"lighting",o.lighting)
        && member(r,p,"angle",o.angle)
        && member(r,p,"shine",o.shine);
}
template<> bool fields<TilesOptions>(Reader &r,const QVariantMap &p,TilesOptions &o) {
    return r.keys(p,{"count","maximumOffset","fill","foreground","background","seed"})
        && member(r,p,"count",o.count)
        && member(r,p,"maximumOffset",o.maximumOffset)
        && member(r,p,"fill",o.fill)
        && member(r,p,"foreground",o.foreground)
        && member(r,p,"background",o.background)
        && member(r,p,"seed",o.seed);
}
template<> bool fields<TraceContourOptions>(Reader &r,const QVariantMap &p,TraceContourOptions &o) {
    return r.keys(p,{"level","upper"})
        && member(r,p,"level",o.level)
        && member(r,p,"upper",o.upper);
}
template<> bool fields<WindOptions>(Reader &r,const QVariantMap &p,WindOptions &o) {
    return r.keys(p,{"method","fromRight","seed"})
        && member(r,p,"method",o.method)
        && member(r,p,"fromRight",o.fromRight)
        && member(r,p,"seed",o.seed);
}
bool galleryValue(Reader &r,const QVariant &v,GalleryEffect &out) {
    const auto map=v.toMap(); const auto operation=canonicalOperation(map.value("operation",map.value("type")).toString());
    if(!r.keys(map,{"operation","type","parameters","options"})) return false;
    const auto parameters=map.value("parameters",map.value("options",QVariantMap{}));
    if(parameters.metaType().id()!=QMetaType::QVariantMap) return r.fail("Gallery parameters must be an object");
    auto p=parameters.toMap();
    if (operation=="coloredPencil") { GalleryColoredPencilOptions o; if (!read(r,QVariant(nested(p)),o)) return false; out=std::move(o); return true; }
    if (operation=="cutout") { GalleryCutoutOptions o; if (!read(r,QVariant(nested(p)),o)) return false; out=std::move(o); return true; }
    if (operation=="dryBrush") { GalleryDryBrushOptions o; if (!read(r,QVariant(nested(p)),o)) return false; out=std::move(o); return true; }
    if (operation=="galleryFilmGrain") { GalleryFilmGrainOptions o; if (!read(r,QVariant(nested(p)),o)) return false; out=std::move(o); return true; }
    if (operation=="fresco") { GalleryFrescoOptions o; if (!read(r,QVariant(nested(p)),o)) return false; out=std::move(o); return true; }
    if (operation=="neonGlow") { GalleryNeonGlowOptions o; if (!read(r,QVariant(nested(p)),o)) return false; out=std::move(o); return true; }
    if (operation=="paintDaubs") { GalleryPaintDaubsOptions o; if (!read(r,QVariant(nested(p)),o)) return false; out=std::move(o); return true; }
    if (operation=="paletteKnife") { GalleryPaletteKnifeOptions o; if (!read(r,QVariant(nested(p)),o)) return false; out=std::move(o); return true; }
    if (operation=="plasticWrap") { GalleryPlasticWrapOptions o; if (!read(r,QVariant(nested(p)),o)) return false; out=std::move(o); return true; }
    if (operation=="posterEdges") { GalleryPosterEdgesOptions o; if (!read(r,QVariant(nested(p)),o)) return false; out=std::move(o); return true; }
    if (operation=="roughPastels") { GalleryRoughPastelsOptions o; if (!read(r,QVariant(nested(p)),o)) return false; out=std::move(o); return true; }
    if (operation=="smudgeStick") { GallerySmudgeStickOptions o; if (!read(r,QVariant(nested(p)),o)) return false; out=std::move(o); return true; }
    if (operation=="sponge") { GallerySpongeOptions o; if (!read(r,QVariant(nested(p)),o)) return false; out=std::move(o); return true; }
    if (operation=="underpainting") { GalleryUnderpaintingOptions o; if (!read(r,QVariant(nested(p)),o)) return false; out=std::move(o); return true; }
    if (operation=="watercolor") { GalleryWatercolorOptions o; if (!read(r,QVariant(nested(p)),o)) return false; out=std::move(o); return true; }
    if (operation=="accentedEdges") { GalleryAccentedEdgesOptions o; if (!read(r,QVariant(nested(p)),o)) return false; out=std::move(o); return true; }
    if (operation=="angledStrokes") { GalleryAngledStrokesOptions o; if (!read(r,QVariant(nested(p)),o)) return false; out=std::move(o); return true; }
    if (operation=="crosshatch") { GalleryCrosshatchOptions o; if (!read(r,QVariant(nested(p)),o)) return false; out=std::move(o); return true; }
    if (operation=="darkStrokes") { GalleryDarkStrokesOptions o; if (!read(r,QVariant(nested(p)),o)) return false; out=std::move(o); return true; }
    if (operation=="inkOutlines") { GalleryInkOutlinesOptions o; if (!read(r,QVariant(nested(p)),o)) return false; out=std::move(o); return true; }
    if (operation=="spatter") { GallerySpatterOptions o; if (!read(r,QVariant(nested(p)),o)) return false; out=std::move(o); return true; }
    if (operation=="sprayedStrokes") { GallerySprayedStrokesOptions o; if (!read(r,QVariant(nested(p)),o)) return false; out=std::move(o); return true; }
    if (operation=="sumiE") { GallerySumiEOptions o; if (!read(r,QVariant(nested(p)),o)) return false; out=std::move(o); return true; }
    if (operation=="diffuseGlow") { GalleryDiffuseGlowOptions o; if (!read(r,QVariant(nested(p)),o)) return false; out=std::move(o); return true; }
    if (operation=="glass") { GalleryGlassOptions o; if (!read(r,QVariant(nested(p)),o)) return false; out=std::move(o); return true; }
    if (operation=="oceanRipple") { GalleryOceanRippleOptions o; if (!read(r,QVariant(nested(p)),o)) return false; out=std::move(o); return true; }
    if (operation=="basRelief") { GalleryBasReliefOptions o; if (!read(r,QVariant(nested(p)),o)) return false; out=std::move(o); return true; }
    if (operation=="chalkCharcoal") { GalleryChalkCharcoalOptions o; if (!read(r,QVariant(nested(p)),o)) return false; out=std::move(o); return true; }
    if (operation=="charcoal") { GalleryCharcoalOptions o; if (!read(r,QVariant(nested(p)),o)) return false; out=std::move(o); return true; }
    if (operation=="chrome") { GalleryChromeOptions o; if (!read(r,QVariant(nested(p)),o)) return false; out=std::move(o); return true; }
    if (operation=="conteCrayon") { GalleryConteCrayonOptions o; if (!read(r,QVariant(nested(p)),o)) return false; out=std::move(o); return true; }
    if (operation=="graphicPen") { GalleryGraphicPenOptions o; if (!read(r,QVariant(nested(p)),o)) return false; out=std::move(o); return true; }
    if (operation=="halftonePattern") { GalleryHalftonePatternOptions o; if (!read(r,QVariant(nested(p)),o)) return false; out=std::move(o); return true; }
    if (operation=="notePaper") { GalleryNotePaperOptions o; if (!read(r,QVariant(nested(p)),o)) return false; out=std::move(o); return true; }
    if (operation=="photocopy") { GalleryPhotocopyOptions o; if (!read(r,QVariant(nested(p)),o)) return false; out=std::move(o); return true; }
    if (operation=="plaster") { GalleryPlasterOptions o; if (!read(r,QVariant(nested(p)),o)) return false; out=std::move(o); return true; }
    if (operation=="reticulation") { GalleryReticulationOptions o; if (!read(r,QVariant(nested(p)),o)) return false; out=std::move(o); return true; }
    if (operation=="stamp") { GalleryStampOptions o; if (!read(r,QVariant(nested(p)),o)) return false; out=std::move(o); return true; }
    if (operation=="tornEdges") { GalleryTornEdgesOptions o; if (!read(r,QVariant(nested(p)),o)) return false; out=std::move(o); return true; }
    if (operation=="waterPaper") { GalleryWaterPaperOptions o; if (!read(r,QVariant(nested(p)),o)) return false; out=std::move(o); return true; }
    if (operation=="glowingEdges") { GalleryGlowingEdgesOptions o; if (!read(r,QVariant(nested(p)),o)) return false; out=std::move(o); return true; }
    if (operation=="craquelure") { GalleryCraquelureOptions o; if (!read(r,QVariant(nested(p)),o)) return false; out=std::move(o); return true; }
    if (operation=="grain") { GalleryGrainOptions o; if (!read(r,QVariant(nested(p)),o)) return false; out=std::move(o); return true; }
    if (operation=="mosaicTiles") { GalleryMosaicTilesOptions o; if (!read(r,QVariant(nested(p)),o)) return false; out=std::move(o); return true; }
    if (operation=="patchwork") { GalleryPatchworkOptions o; if (!read(r,QVariant(nested(p)),o)) return false; out=std::move(o); return true; }
    if (operation=="stainedGlass") { GalleryStainedGlassOptions o; if (!read(r,QVariant(nested(p)),o)) return false; out=std::move(o); return true; }
    if (operation=="texturizer") { GalleryTexturizerOptions o; if (!read(r,QVariant(nested(p)),o)) return false; out=std::move(o); return true; }
    return r.fail("Gallery stack entries require a supported Gallery operation and parameters");
}
const QMap<QString,QString> &aliases() {
    static const QMap<QString,QString> result {
        {"edit.perspectiveWarp","perspectiveWarp"},
        {"edit.puppetWarp","puppetWarp"},
        {"filter.adaptiveWideAngle","adaptiveWideAngle"},
        {"filter.blur.average","average"},
        {"filter.blur.blur","blur"},
        {"filter.blur.blurMore","blurMore"},
        {"filter.blur.boxBlur","boxBlur"},
        {"filter.blur.gaussianBlur","filterGaussianBlur"},
        {"filter.blur.lensBlur","lensBlur"},
        {"filter.blur.motionBlur","motionBlur"},
        {"filter.blur.radialBlur","radialBlur"},
        {"filter.blur.shapeBlur","shapeBlur"},
        {"filter.blur.smartBlur","smartBlur"},
        {"filter.blur.surfaceBlur","surfaceBlur"},
        {"filter.blurGallery.fieldBlur","fieldBlur"},
        {"filter.blurGallery.irisBlur","irisBlur"},
        {"filter.blurGallery.pathBlur","pathBlur"},
        {"filter.blurGallery.spinBlur","spinBlur"},
        {"filter.blurGallery.tiltShift","tiltShift"},
        {"filter.cameraRaw","cameraRawFilter"},
        {"filter.distort.displace","displace"},
        {"filter.distort.pinch","pinch"},
        {"filter.distort.polarCoordinates","polarCoordinates"},
        {"filter.distort.ripple","ripple"},
        {"filter.distort.shear","shear"},
        {"filter.distort.spherize","spherize"},
        {"filter.distort.twirl","twirl"},
        {"filter.distort.wave","wave"},
        {"filter.distort.zigZag","zigZag"},
        {"filter.gallery.accentedEdges","accentedEdges"},
        {"filter.gallery.angledStrokes","angledStrokes"},
        {"filter.gallery.basRelief","basRelief"},
        {"filter.gallery.chalkCharcoal","chalkCharcoal"},
        {"filter.gallery.charcoal","charcoal"},
        {"filter.gallery.chrome","chrome"},
        {"filter.gallery.coloredPencil","coloredPencil"},
        {"filter.gallery.conteCrayon","conteCrayon"},
        {"filter.gallery.craquelure","craquelure"},
        {"filter.gallery.crosshatch","crosshatch"},
        {"filter.gallery.cutout","cutout"},
        {"filter.gallery.darkStrokes","darkStrokes"},
        {"filter.gallery.diffuseGlow","diffuseGlow"},
        {"filter.gallery.dryBrush","dryBrush"},
        {"filter.gallery.filmGrain","galleryFilmGrain"},
        {"filter.gallery.fresco","fresco"},
        {"filter.gallery.glass","glass"},
        {"filter.gallery.glowingEdges","glowingEdges"},
        {"filter.gallery.grain","grain"},
        {"filter.gallery.graphicPen","graphicPen"},
        {"filter.gallery.halftonePattern","halftonePattern"},
        {"filter.gallery.inkOutlines","inkOutlines"},
        {"filter.gallery.mosaicTiles","mosaicTiles"},
        {"filter.gallery.neonGlow","neonGlow"},
        {"filter.gallery.notePaper","notePaper"},
        {"filter.gallery.oceanRipple","oceanRipple"},
        {"filter.gallery.paintDaubs","paintDaubs"},
        {"filter.gallery.paletteKnife","paletteKnife"},
        {"filter.gallery.patchwork","patchwork"},
        {"filter.gallery.photocopy","photocopy"},
        {"filter.gallery.plaster","plaster"},
        {"filter.gallery.plasticWrap","plasticWrap"},
        {"filter.gallery.posterEdges","posterEdges"},
        {"filter.gallery.reticulation","reticulation"},
        {"filter.gallery.roughPastels","roughPastels"},
        {"filter.gallery.smudgeStick","smudgeStick"},
        {"filter.gallery.spatter","spatter"},
        {"filter.gallery.sponge","sponge"},
        {"filter.gallery.sprayedStrokes","sprayedStrokes"},
        {"filter.gallery.stainedGlass","stainedGlass"},
        {"filter.gallery.stamp","stamp"},
        {"filter.gallery.sumiE","sumiE"},
        {"filter.gallery.texturizer","texturizer"},
        {"filter.gallery.tornEdges","tornEdges"},
        {"filter.gallery.underpainting","underpainting"},
        {"filter.gallery.waterPaper","waterPaper"},
        {"filter.gallery.watercolor","watercolor"},
        {"filter.lensCorrection","lensCorrection"},
        {"filter.liquify","liquify"},
        {"filter.noise.addNoise","filterAddNoise"},
        {"filter.noise.despeckle","despeckle"},
        {"filter.noise.dustAndScratches","dustAndScratches"},
        {"filter.noise.median","median"},
        {"filter.noise.reduceNoise","reduceNoise"},
        {"filter.other.custom","customFilter"},
        {"filter.other.highPass","highPass"},
        {"filter.other.hsbHsl","hsbHsl"},
        {"filter.other.maximum","maximum"},
        {"filter.other.minimum","minimum"},
        {"filter.other.offset","offset"},
        {"filter.pixelate.colorHalftone","colorHalftone"},
        {"filter.pixelate.crystallize","crystallize"},
        {"filter.pixelate.facet","facet"},
        {"filter.pixelate.fragment","fragment"},
        {"filter.pixelate.mezzotint","mezzotint"},
        {"filter.pixelate.mosaic","mosaic"},
        {"filter.pixelate.pointillize","pointillize"},
        {"filter.render.clouds","clouds"},
        {"filter.render.differenceClouds","differenceClouds"},
        {"filter.render.fibers","fibers"},
        {"filter.render.flame","flame"},
        {"filter.render.lensFlare","lensFlare"},
        {"filter.render.lightingEffects","lightingEffects"},
        {"filter.render.pictureFrame","pictureFrame"},
        {"filter.render.relight","relight"},
        {"filter.render.tree","tree"},
        {"filter.sharpen.sharpen","filterSharpen"},
        {"filter.sharpen.sharpenEdges","sharpenEdges"},
        {"filter.sharpen.sharpenMore","sharpenMore"},
        {"filter.sharpen.smartSharpen","smartSharpen"},
        {"filter.sharpen.unsharpMask","unsharpMask"},
        {"filter.stylize.diffuse","diffuse"},
        {"filter.stylize.emboss","emboss"},
        {"filter.stylize.extrude","extrude"},
        {"filter.stylize.findEdges","findEdges"},
        {"filter.stylize.oilPaint","oilPaint"},
        {"filter.stylize.solarize","solarize"},
        {"filter.stylize.tiles","tiles"},
        {"filter.stylize.traceContour","traceContour"},
        {"filter.stylize.wind","wind"},
        {"filter.vanishingPoint","vanishingPoint"},
        {"filter.video.deInterlace","deInterlace"},
        {"filter.video.ntscColors","ntscColors"},
        {"layer.smartObjects.perspectiveWarp","perspectiveWarp"},
        {"layer.smartObjects.puppetWarp","puppetWarp"},
        {"look.Color","lightcraftLookColor"},
        {"look.Neutral","lightcraftLookNeutral"},
        {"look.Vivid","lightcraftLookVivid"},
        {"look.Landscape","lightcraftLookLandscape"},
        {"look.Portrait","lightcraftLookPortrait"},
        {"look.Mono","lightcraftLookMono"},
        {"look.FilmWarmPrint","lightcraftLookFilmWarmPrint"},
        {"look.FilmCoolFade","lightcraftLookFilmCoolFade"},
        {"look.FilmGoldenHour","lightcraftLookFilmGoldenHour"},
        {"look.FilmFadedSlide","lightcraftLookFilmFadedSlide"},
        {"look.CineTealAmber","lightcraftLookCineTealAmber"},
        {"look.CineNightBlue","lightcraftLookCineNightBlue"},
        {"look.CineDesertHeat","lightcraftLookCineDesertHeat"},
        {"look.CineNeonDusk","lightcraftLookCineNeonDusk"},
        {"look.MutedMatteSoft","lightcraftLookMutedMatteSoft"},
        {"look.MutedBleached","lightcraftLookMutedBleached"},
        {"look.MutedPastelHaze","lightcraftLookMutedPastelHaze"},
        {"look.MutedQuietGreen","lightcraftLookMutedQuietGreen"},
        {"look.BwMonoRich","lightcraftLookBwMonoRich"},
        {"look.BwRedFilter","lightcraftLookBwRedFilter"},
        {"look.BwSoft","lightcraftLookBwSoft"},
        {"look.BwSepia","lightcraftLookBwSepia"},
        {"preset.WarmGlow","lightcraftPresetWarmGlow"},
        {"preset.CoolMorning","lightcraftPresetCoolMorning"},
        {"preset.TealOrange","lightcraftPresetTealOrange"},
        {"preset.VividPop","lightcraftPresetVividPop"},
        {"preset.SoftPastel","lightcraftPresetSoftPastel"},
        {"preset.FadedMatte","lightcraftPresetFadedMatte"},
        {"preset.WarmFilm","lightcraftPresetWarmFilm"},
        {"preset.MutedFilm","lightcraftPresetMutedFilm"},
        {"preset.BwHighContrast","lightcraftPresetBwHighContrast"},
        {"preset.BwSoft","lightcraftPresetBwSoft"},
        {"preset.BwSelenium","lightcraftPresetBwSelenium"},
        {"preset.CrispLandscape","lightcraftPresetCrispLandscape"},
        {"preset.GoldenHour","lightcraftPresetGoldenHour"},
        {"preset.BlueHour","lightcraftPresetBlueHour"},
        {"preset.SoftSkin","lightcraftPresetSoftSkin"},
        {"preset.BrightAiry","lightcraftPresetBrightAiry"},
        {"preset.Moody","lightcraftPresetMoody"},
        {"preset.Cinematic","lightcraftPresetCinematic"},
        {"preset.MoodyPortrait","lightcraftPresetMoodyPortrait"},
        {"preset.GoldenGlow","lightcraftPresetGoldenGlow"},
        {"preset.CleanStudio","lightcraftPresetCleanStudio"},
        {"preset.CrispVista","lightcraftPresetCrispVista"},
        {"preset.DeepSky","lightcraftPresetDeepSky"},
        {"preset.LushGreens","lightcraftPresetLushGreens"},
        {"preset.DesertWarmth","lightcraftPresetDesertWarmth"},
        {"preset.MistyMorning","lightcraftPresetMistyMorning"},
        {"preset.GrittyStreet","lightcraftPresetGrittyStreet"},
        {"preset.NeonNight","lightcraftPresetNeonNight"},
        {"preset.ConcreteCool","lightcraftPresetConcreteCool"},
        {"preset.FadedUrban","lightcraftPresetFadedUrban"},
        {"preset.FreshBright","lightcraftPresetFreshBright"},
        {"preset.WarmTable","lightcraftPresetWarmTable"},
        {"preset.DarkMoodyFood","lightcraftPresetDarkMoodyFood"},
        {"preset.AutumnGold","lightcraftPresetAutumnGold"},
        {"preset.WinterBlue","lightcraftPresetWinterBlue"},
        {"preset.SpringFresh","lightcraftPresetSpringFresh"},
        {"preset.SummerHaze","lightcraftPresetSummerHaze"},
        {"preset.Instant70s","lightcraftPresetInstant70s"},
        {"preset.CrossProcess","lightcraftPresetCrossProcess"},
        {"preset.BleachBypass","lightcraftPresetBleachBypass"},
        {"preset.BwSepia","lightcraftPresetBwSepia"},
        {"brightness-contrast","brightnessContrast"},
        {"black-white","blackAndWhite"},
        {"hue-saturation","hueSaturation"},
        {"shadows-highlights","shadowsHighlights"},
        {"gradient-map","gradientMap"},
        {"color-lookup","colorLookup"},
        {"white-balance","whiteBalance"},
        {"color-balance","colorBalance"},
        {"channel-mixer","channelMixer"},
        {"selective-color","selectiveColor"},
        {"auto-tone","autoLevels"},
        {"linear-mask","linearMask"},
        {"radial-mask","radialMask"},
        {"luminance-range","luminanceRangeMask"},
        {"color-range","colorRangeMask"},
        {"brush-mask","brushMask"},
    }; return result;
}
QVariantMap normalizedParameters(const QString &operation, QVariantMap p) {
    p=nested(p);
    if (p.contains("options") && p.value("options").metaType().id()==QMetaType::QVariantMap) { auto options=p.take("options").toMap(); for(auto it=p.cbegin();it!=p.cend();++it) options[it.key()]=it.value(); p=options; }
    auto rename=[&](const char *before,const char *after) { if(p.contains(before) && !p.contains(after)) p[after]=p.take(before); };
    if (operation=="filterGaussianBlur") rename("radius","sigma");
    if (operation=="colorHalftone") rename("radius","maximumRadius");
    if (operation=="cameraRawFilter") { rename("hslSat","hslSaturation"); rename("hslLum","hslLuminance"); }
    if (operation=="liquify") rename("meshSize","cell");
    if (operation=="lensBlur") {
        rename("bladeCurvature","curvature"); rename("invert","invertDepth"); rename("depthMapSource","depth");
        if(p.contains("shape")) { const auto shape=token(p.take("shape").toString()); const QMap<QString,int> blades{{"triangle",3},{"square",4},{"pentagon",5},{"hexagon",6},{"heptagon",7},{"octagon",8}}; p["blades"]=blades.value(shape,-1); }
    }
    if (operation=="puppetWarp" && p.contains("pins")) {
        auto pins=p.value("pins").toList(); for(auto &pin:pins) { auto m=pin.toMap(); if(m.contains("src")) m["source"]=m.take("src"); if(m.contains("dst")) m["destination"]=m.take("dst"); if(m.contains("rotate")) m["rotation"]=m.take("rotate"); pin=m; } p["pins"]=pins;
    }
    if (operation=="perspectiveWarp" && p.contains("planes")) {
        auto planes=p.value("planes").toList(); for(auto &plane:planes) { auto m=plane.toMap(); if(m.contains("src")) m["source"]=m.take("src"); if(m.contains("dst")) m["destination"]=m.take("dst"); plane=m; } p["planes"]=planes;
    }
    return p;
}
GA::Result<Image> dispatch(Reader &r,ImageView source,const QString &op,const QVariantMap &p) {
    auto failure=[&]() -> GA::Result<Image> { return std::unexpected(Error{ErrorCode::InvalidParameter,"Invalid adapter parameters"}); };
    if(op=="blend") {
        ImageView a0={};
        if (!member(r,p,"source",a0)) return failure();
        if (!p.contains("source")) { r.fail("Supply parameter: source"); return failure(); }
        BlendOptions o1{};
        auto options=p; options.remove("source");
        if (!read(r,QVariant(options),o1)) return failure();
        return GA::blend(source,a0,o1);
    }
    if(op=="applyLocalAdjustments" && !p.contains("adjustments")) {
        LocalAdjustment a; auto options=p; options.remove("mask");
        if(!p.contains("mask") || !maskMember(r,p,"mask",a.mask) || !read(r,QVariant(options),a.options)) { r.fail("Supply a local adjustment mask"); return failure(); }
        const std::array<LocalAdjustment,1> items{a}; return GA::applyLocalAdjustments(source,items);
    }
    if(op=="applyLocalAdjustments") {
        std::span<const LocalAdjustment> a0={};
        if (!member(r,p,"adjustments",a0)) return failure();
        if (!r.keys(p,{"adjustments"})) return failure();
        return GA::applyLocalAdjustments(source,a0);
    }
    if(op=="gradientMap") {
        GradientMapOptions o0{};
        auto options=p;
        if (!read(r,QVariant(options),o0)) return failure();
        return GA::gradientMap(source,o0);
    }
    if(op=="colorLookup") {
        ColorLut o0{};
        if (!p.contains("lut") || !member(r,p,"lut",o0)) { r.fail("Supply a parsed LUT"); return failure(); }
        ColorLookupOptions o1{};
        auto options=p; options.remove("lut");
        if (!read(r,QVariant(options),o1)) return failure();
        return GA::colorLookup(source,o0,o1);
    }
    if(op=="cameraRawFilter") {
        CameraRawOptions o0{};
        auto options=p;
        if (!read(r,QVariant(options),o0)) return failure();
        return GA::cameraRawFilter(source,o0);
    }
    if(op=="offset") {
        std::int32_t a0={};
        if (!member(r,p,"horizontal",a0)) return failure();
        if (!p.contains("horizontal")) { r.fail("Supply parameter: horizontal"); return failure(); }
        std::int32_t a1={};
        if (!member(r,p,"vertical",a1)) return failure();
        if (!p.contains("vertical")) { r.fail("Supply parameter: vertical"); return failure(); }
        FilterEdge a2=FilterEdge::Wrap;
        if (!member(r,p,"edge",a2)) return failure();
        if (!r.keys(p,{"horizontal","vertical","edge"})) return failure();
        return GA::offset(source,a0,a1,a2);
    }
    if(op=="twirl") {
        double a0=50;
        if (!member(r,p,"angle",a0)) return failure();
        if (!r.keys(p,{"angle"})) return failure();
        return GA::twirl(source,a0);
    }
    if(op=="pinch") {
        double a0=50;
        if (!member(r,p,"amount",a0)) return failure();
        if (!r.keys(p,{"amount"})) return failure();
        return GA::pinch(source,a0);
    }
    if(op=="spherize") {
        SpherizeOptions o0{};
        auto options=p;
        if (!read(r,QVariant(options),o0)) return failure();
        return GA::spherize(source,o0);
    }
    if(op=="wave") {
        WaveOptions o0{};
        auto options=p;
        if (!read(r,QVariant(options),o0)) return failure();
        return GA::wave(source,o0);
    }
    if(op=="ripple") {
        RippleOptions o0{};
        auto options=p;
        if (!read(r,QVariant(options),o0)) return failure();
        return GA::ripple(source,o0);
    }
    if(op=="polarCoordinates") {
        PolarMode a0=PolarMode::RectangularToPolar;
        if (!member(r,p,"mode",a0)) return failure();
        if (!r.keys(p,{"mode"})) return failure();
        return GA::polarCoordinates(source,a0);
    }
    if(op=="displace") {
        ImageView a0={};
        if (!member(r,p,"map",a0)) return failure();
        if (!p.contains("map")) { r.fail("Supply parameter: map"); return failure(); }
        DisplaceOptions o1{};
        auto options=p; options.remove("map");
        if (!read(r,QVariant(options),o1)) return failure();
        return GA::displace(source,a0,o1);
    }
    if(op=="shear") {
        std::span<const ShearPoint> a0={};
        if (!member(r,p,"points",a0)) return failure();
        FilterEdge a1=FilterEdge::Wrap;
        if (!member(r,p,"edge",a1)) return failure();
        if (!r.keys(p,{"points","edge"})) return failure();
        return GA::shear(source,a0,a1);
    }
    if(op=="zigZag") {
        ZigZagOptions o0{};
        auto options=p;
        if (!read(r,QVariant(options),o0)) return failure();
        return GA::zigZag(source,o0);
    }
    if(op=="developEffects") {
        DevelopEffectsOptions o0{};
        auto options=p;
        if (!read(r,QVariant(options),o0)) return failure();
        return GA::developEffects(source,o0);
    }
    if(op=="postCropVignette") {
        PostCropVignetteOptions o0{};
        auto options=p;
        if (!read(r,QVariant(options),o0)) return failure();
        return GA::postCropVignette(source,o0);
    }
    if(op=="filmGrain") {
        FilmGrainOptions o0{};
        auto options=p;
        if (!read(r,QVariant(options),o0)) return failure();
        return GA::filmGrain(source,o0);
    }
    if(op=="developDetail") {
        DevelopDetailOptions o0{};
        auto options=p;
        if (!read(r,QVariant(options),o0)) return failure();
        return GA::developDetail(source,o0);
    }
    if(op=="hueSaturation") {
        HueSaturationOptions o0{};
        auto options=p;
        if (!read(r,QVariant(options),o0)) return failure();
        return GA::hueSaturation(source,o0);
    }
    if(op=="vibrance") {
        VibranceOptions o0{};
        auto options=p;
        if (!read(r,QVariant(options),o0)) return failure();
        return GA::vibrance(source,o0);
    }
    if(op=="colorBalance") {
        ColorBalanceOptions o0{};
        auto options=p;
        if (!read(r,QVariant(options),o0)) return failure();
        return GA::colorBalance(source,o0);
    }
    if(op=="blackAndWhite") {
        BlackWhiteOptions o0{};
        auto options=p;
        if (!read(r,QVariant(options),o0)) return failure();
        return GA::blackAndWhite(source,o0);
    }
    if(op=="photoFilter") {
        PhotoFilterOptions o0{};
        auto options=p;
        if (!read(r,QVariant(options),o0)) return failure();
        return GA::photoFilter(source,o0);
    }
    if(op=="channelMixer") {
        ChannelMixerOptions o0{};
        auto options=p;
        if (!read(r,QVariant(options),o0)) return failure();
        return GA::channelMixer(source,o0);
    }
    if(op=="selectiveColor") {
        SelectiveColorOptions o0{};
        auto options=p;
        if (!read(r,QVariant(options),o0)) return failure();
        return GA::selectiveColor(source,o0);
    }
    if(op=="desaturate") {
        if (!r.keys(p,{})) return failure();
        return GA::desaturate(source);
    }
    if(op=="replaceColor") {
        ReplaceColorOptions o0{};
        auto options=p;
        if (!read(r,QVariant(options),o0)) return failure();
        return GA::replaceColor(source,o0);
    }
    if(op=="matchColor") {
        ImageView a0={};
        if (!member(r,p,"reference",a0)) return failure();
        if (!p.contains("reference")) { r.fail("Supply parameter: reference"); return failure(); }
        MatchColorOptions o1{};
        auto options=p; options.remove("reference");
        if (!read(r,QVariant(options),o1)) return failure();
        return GA::matchColor(source,a0,o1);
    }
    if(op=="coloredPencil") {
        GalleryColoredPencilOptions o0{};
        auto options=p;
        if (!read(r,QVariant(options),o0)) return failure();
        return GA::coloredPencil(source,o0);
    }
    if(op=="cutout") {
        GalleryCutoutOptions o0{};
        auto options=p;
        if (!read(r,QVariant(options),o0)) return failure();
        return GA::cutout(source,o0);
    }
    if(op=="dryBrush") {
        GalleryDryBrushOptions o0{};
        auto options=p;
        if (!read(r,QVariant(options),o0)) return failure();
        return GA::dryBrush(source,o0);
    }
    if(op=="galleryFilmGrain") {
        GalleryFilmGrainOptions o0{};
        auto options=p;
        if (!read(r,QVariant(options),o0)) return failure();
        return GA::galleryFilmGrain(source,o0);
    }
    if(op=="fresco") {
        GalleryFrescoOptions o0{};
        auto options=p;
        if (!read(r,QVariant(options),o0)) return failure();
        return GA::fresco(source,o0);
    }
    if(op=="neonGlow") {
        GalleryNeonGlowOptions o0{};
        auto options=p;
        if (!read(r,QVariant(options),o0)) return failure();
        return GA::neonGlow(source,o0);
    }
    if(op=="paintDaubs") {
        GalleryPaintDaubsOptions o0{};
        auto options=p;
        if (!read(r,QVariant(options),o0)) return failure();
        return GA::paintDaubs(source,o0);
    }
    if(op=="paletteKnife") {
        GalleryPaletteKnifeOptions o0{};
        auto options=p;
        if (!read(r,QVariant(options),o0)) return failure();
        return GA::paletteKnife(source,o0);
    }
    if(op=="plasticWrap") {
        GalleryPlasticWrapOptions o0{};
        auto options=p;
        if (!read(r,QVariant(options),o0)) return failure();
        return GA::plasticWrap(source,o0);
    }
    if(op=="posterEdges") {
        GalleryPosterEdgesOptions o0{};
        auto options=p;
        if (!read(r,QVariant(options),o0)) return failure();
        return GA::posterEdges(source,o0);
    }
    if(op=="roughPastels") {
        GalleryRoughPastelsOptions o0{};
        auto options=p;
        if (!read(r,QVariant(options),o0)) return failure();
        return GA::roughPastels(source,o0);
    }
    if(op=="smudgeStick") {
        GallerySmudgeStickOptions o0{};
        auto options=p;
        if (!read(r,QVariant(options),o0)) return failure();
        return GA::smudgeStick(source,o0);
    }
    if(op=="sponge") {
        GallerySpongeOptions o0{};
        auto options=p;
        if (!read(r,QVariant(options),o0)) return failure();
        return GA::sponge(source,o0);
    }
    if(op=="underpainting") {
        GalleryUnderpaintingOptions o0{};
        auto options=p;
        if (!read(r,QVariant(options),o0)) return failure();
        return GA::underpainting(source,o0);
    }
    if(op=="watercolor") {
        GalleryWatercolorOptions o0{};
        auto options=p;
        if (!read(r,QVariant(options),o0)) return failure();
        return GA::watercolor(source,o0);
    }
    if(op=="accentedEdges") {
        GalleryAccentedEdgesOptions o0{};
        auto options=p;
        if (!read(r,QVariant(options),o0)) return failure();
        return GA::accentedEdges(source,o0);
    }
    if(op=="angledStrokes") {
        GalleryAngledStrokesOptions o0{};
        auto options=p;
        if (!read(r,QVariant(options),o0)) return failure();
        return GA::angledStrokes(source,o0);
    }
    if(op=="crosshatch") {
        GalleryCrosshatchOptions o0{};
        auto options=p;
        if (!read(r,QVariant(options),o0)) return failure();
        return GA::crosshatch(source,o0);
    }
    if(op=="darkStrokes") {
        GalleryDarkStrokesOptions o0{};
        auto options=p;
        if (!read(r,QVariant(options),o0)) return failure();
        return GA::darkStrokes(source,o0);
    }
    if(op=="inkOutlines") {
        GalleryInkOutlinesOptions o0{};
        auto options=p;
        if (!read(r,QVariant(options),o0)) return failure();
        return GA::inkOutlines(source,o0);
    }
    if(op=="spatter") {
        GallerySpatterOptions o0{};
        auto options=p;
        if (!read(r,QVariant(options),o0)) return failure();
        return GA::spatter(source,o0);
    }
    if(op=="sprayedStrokes") {
        GallerySprayedStrokesOptions o0{};
        auto options=p;
        if (!read(r,QVariant(options),o0)) return failure();
        return GA::sprayedStrokes(source,o0);
    }
    if(op=="sumiE") {
        GallerySumiEOptions o0{};
        auto options=p;
        if (!read(r,QVariant(options),o0)) return failure();
        return GA::sumiE(source,o0);
    }
    if(op=="diffuseGlow") {
        GalleryDiffuseGlowOptions o0{};
        auto options=p;
        if (!read(r,QVariant(options),o0)) return failure();
        return GA::diffuseGlow(source,o0);
    }
    if(op=="glass") {
        GalleryGlassOptions o0{};
        auto options=p;
        if (!read(r,QVariant(options),o0)) return failure();
        return GA::glass(source,o0);
    }
    if(op=="oceanRipple") {
        GalleryOceanRippleOptions o0{};
        auto options=p;
        if (!read(r,QVariant(options),o0)) return failure();
        return GA::oceanRipple(source,o0);
    }
    if(op=="basRelief") {
        GalleryBasReliefOptions o0{};
        auto options=p;
        if (!read(r,QVariant(options),o0)) return failure();
        return GA::basRelief(source,o0);
    }
    if(op=="chalkCharcoal") {
        GalleryChalkCharcoalOptions o0{};
        auto options=p;
        if (!read(r,QVariant(options),o0)) return failure();
        return GA::chalkCharcoal(source,o0);
    }
    if(op=="charcoal") {
        GalleryCharcoalOptions o0{};
        auto options=p;
        if (!read(r,QVariant(options),o0)) return failure();
        return GA::charcoal(source,o0);
    }
    if(op=="chrome") {
        GalleryChromeOptions o0{};
        auto options=p;
        if (!read(r,QVariant(options),o0)) return failure();
        return GA::chrome(source,o0);
    }
    if(op=="conteCrayon") {
        GalleryConteCrayonOptions o0{};
        auto options=p;
        if (!read(r,QVariant(options),o0)) return failure();
        return GA::conteCrayon(source,o0);
    }
    if(op=="graphicPen") {
        GalleryGraphicPenOptions o0{};
        auto options=p;
        if (!read(r,QVariant(options),o0)) return failure();
        return GA::graphicPen(source,o0);
    }
    if(op=="halftonePattern") {
        GalleryHalftonePatternOptions o0{};
        auto options=p;
        if (!read(r,QVariant(options),o0)) return failure();
        return GA::halftonePattern(source,o0);
    }
    if(op=="notePaper") {
        GalleryNotePaperOptions o0{};
        auto options=p;
        if (!read(r,QVariant(options),o0)) return failure();
        return GA::notePaper(source,o0);
    }
    if(op=="photocopy") {
        GalleryPhotocopyOptions o0{};
        auto options=p;
        if (!read(r,QVariant(options),o0)) return failure();
        return GA::photocopy(source,o0);
    }
    if(op=="plaster") {
        GalleryPlasterOptions o0{};
        auto options=p;
        if (!read(r,QVariant(options),o0)) return failure();
        return GA::plaster(source,o0);
    }
    if(op=="reticulation") {
        GalleryReticulationOptions o0{};
        auto options=p;
        if (!read(r,QVariant(options),o0)) return failure();
        return GA::reticulation(source,o0);
    }
    if(op=="stamp") {
        GalleryStampOptions o0{};
        auto options=p;
        if (!read(r,QVariant(options),o0)) return failure();
        return GA::stamp(source,o0);
    }
    if(op=="tornEdges") {
        GalleryTornEdgesOptions o0{};
        auto options=p;
        if (!read(r,QVariant(options),o0)) return failure();
        return GA::tornEdges(source,o0);
    }
    if(op=="waterPaper") {
        GalleryWaterPaperOptions o0{};
        auto options=p;
        if (!read(r,QVariant(options),o0)) return failure();
        return GA::waterPaper(source,o0);
    }
    if(op=="glowingEdges") {
        GalleryGlowingEdgesOptions o0{};
        auto options=p;
        if (!read(r,QVariant(options),o0)) return failure();
        return GA::glowingEdges(source,o0);
    }
    if(op=="craquelure") {
        GalleryCraquelureOptions o0{};
        auto options=p;
        if (!read(r,QVariant(options),o0)) return failure();
        return GA::craquelure(source,o0);
    }
    if(op=="grain") {
        GalleryGrainOptions o0{};
        auto options=p;
        if (!read(r,QVariant(options),o0)) return failure();
        return GA::grain(source,o0);
    }
    if(op=="mosaicTiles") {
        GalleryMosaicTilesOptions o0{};
        auto options=p;
        if (!read(r,QVariant(options),o0)) return failure();
        return GA::mosaicTiles(source,o0);
    }
    if(op=="patchwork") {
        GalleryPatchworkOptions o0{};
        auto options=p;
        if (!read(r,QVariant(options),o0)) return failure();
        return GA::patchwork(source,o0);
    }
    if(op=="stainedGlass") {
        GalleryStainedGlassOptions o0{};
        auto options=p;
        if (!read(r,QVariant(options),o0)) return failure();
        return GA::stainedGlass(source,o0);
    }
    if(op=="texturizer") {
        GalleryTexturizerOptions o0{};
        auto options=p;
        if (!read(r,QVariant(options),o0)) return failure();
        return GA::texturizer(source,o0);
    }
    if(op=="filterGallery") {
        std::span<const GalleryEffect> a0={};
        if (!member(r,p,"effects",a0)) return failure();
        if (!r.keys(p,{"effects"})) return failure();
        return GA::filterGallery(source,a0);
    }
    if(op=="cloneSpot") {
        SpotOptions o0{};
        auto options=p;
        if (!read(r,QVariant(options),o0)) return failure();
        return GA::cloneSpot(source,o0);
    }
    if(op=="healSpot") {
        SpotOptions o0{};
        auto options=p;
        if (!read(r,QVariant(options),o0)) return failure();
        return GA::healSpot(source,o0);
    }
    if(op=="removeSpot") {
        SpotOptions o0{};
        auto options=p;
        if (!read(r,QVariant(options),o0)) return failure();
        return GA::removeSpot(source,o0);
    }
    if(op=="redEye") {
        EyeOptions o0{};
        auto options=p;
        if (!read(r,QVariant(options),o0)) return failure();
        return GA::redEye(source,o0);
    }
    if(op=="petEye") {
        EyeOptions o0{};
        auto options=p;
        if (!read(r,QVariant(options),o0)) return failure();
        return GA::petEye(source,o0);
    }
    if(op=="visualizeSpots") {
        VisualizeSpotsOptions o0{};
        auto options=p;
        if (!read(r,QVariant(options),o0)) return failure();
        return GA::visualizeSpots(source,o0);
    }
    if(op=="gaussianBlur") {
        double a0={};
        if (!member(r,p,"sigma",a0)) return failure();
        if (!p.contains("sigma")) { r.fail("Supply parameter: sigma"); return failure(); }
        if (!r.keys(p,{"sigma"})) return failure();
        return GA::gaussianBlur(source,a0);
    }
    if(op=="sharpen") {
        SharpenOptions o0{};
        auto options=p;
        if (!read(r,QVariant(options),o0)) return failure();
        return GA::sharpen(source,o0);
    }
    if(op=="addNoise") {
        NoiseOptions o0{};
        auto options=p;
        if (!read(r,QVariant(options),o0)) return failure();
        return GA::addNoise(source,o0);
    }
    if(op=="adjust") {
        ColorAdjustment o0{};
        auto options=p;
        if (!read(r,QVariant(options),o0)) return failure();
        return GA::adjust(source,o0);
    }
    if(op=="developColor") {
        DevelopColorOptions o0{};
        auto options=p;
        if (!read(r,QVariant(options),o0)) return failure();
        return GA::developColor(source,o0);
    }
    if(op=="calibrate") {
        CalibrationOptions o0{};
        auto options=p;
        if (!read(r,QVariant(options),o0)) return failure();
        return GA::calibrate(source,o0);
    }
    if(op=="hdrToning") {
        HdrToningOptions o0{};
        auto options=p;
        if (!read(r,QVariant(options),o0)) return failure();
        return GA::hdrToning(source,o0);
    }
    if(op=="clouds") {
        CloudsOptions o0{};
        auto options=p;
        if (!read(r,QVariant(options),o0)) return failure();
        return GA::clouds(source,o0);
    }
    if(op=="differenceClouds") {
        CloudsOptions o0{};
        auto options=p;
        if (!read(r,QVariant(options),o0)) return failure();
        return GA::differenceClouds(source,o0);
    }
    if(op=="fibers") {
        FibersOptions o0{};
        auto options=p;
        if (!read(r,QVariant(options),o0)) return failure();
        return GA::fibers(source,o0);
    }
    if(op=="lensFlare") {
        LensFlareOptions o0{};
        auto options=p;
        if (!read(r,QVariant(options),o0)) return failure();
        return GA::lensFlare(source,o0);
    }
    if(op=="lightingEffects") {
        LightingEffectsOptions o0{};
        auto options=p;
        if (!read(r,QVariant(options),o0)) return failure();
        return GA::lightingEffects(source,o0);
    }
    if(op=="relight") {
        RelightOptions o0{};
        auto options=p;
        if (!read(r,QVariant(options),o0)) return failure();
        return GA::relight(source,o0);
    }
    if(op=="flame") {
        FlameOptions o0{};
        auto options=p;
        if (!read(r,QVariant(options),o0)) return failure();
        return GA::flame(source,o0);
    }
    if(op=="tree") {
        TreeOptions o0{};
        auto options=p;
        if (!read(r,QVariant(options),o0)) return failure();
        return GA::tree(source,o0);
    }
    if(op=="pictureFrame") {
        PictureFrameOptions o0{};
        auto options=p;
        if (!read(r,QVariant(options),o0)) return failure();
        return GA::pictureFrame(source,o0);
    }
    if(op=="brightnessContrast") {
        BrightnessContrastOptions o0{};
        auto options=p;
        if (!read(r,QVariant(options),o0)) return failure();
        return GA::brightnessContrast(source,o0);
    }
    if(op=="levels") {
        LevelsOptions o0{};
        auto options=p;
        if (!read(r,QVariant(options),o0)) return failure();
        return GA::levels(source,o0);
    }
    if(op=="curves") {
        CurvesOptions o0{};
        auto options=p;
        if (!read(r,QVariant(options),o0)) return failure();
        return GA::curves(source,o0);
    }
    if(op=="exposure") {
        ExposureOptions o0{};
        auto options=p;
        if (!read(r,QVariant(options),o0)) return failure();
        return GA::exposure(source,o0);
    }
    if(op=="shadowsHighlights") {
        ShadowsHighlightsOptions o0{};
        auto options=p;
        if (!read(r,QVariant(options),o0)) return failure();
        return GA::shadowsHighlights(source,o0);
    }
    if(op=="invert") {
        if (!r.keys(p,{})) return failure();
        return GA::invert(source);
    }
    if(op=="posterize") {
        unsigned a0={};
        if (!member(r,p,"levelCount",a0)) return failure();
        if (!p.contains("levelCount")) { r.fail("Supply parameter: levelCount"); return failure(); }
        if (!r.keys(p,{"levelCount"})) return failure();
        return GA::posterize(source,a0);
    }
    if(op=="threshold") {
        double a0=.5;
        if (!member(r,p,"level",a0)) return failure();
        if (!r.keys(p,{"level"})) return failure();
        return GA::threshold(source,a0);
    }
    if(op=="equalize") {
        if (!r.keys(p,{})) return failure();
        return GA::equalize(source);
    }
    if(op=="autoLevels") {
        AutoToneOptions o0{};
        auto options=p;
        if (!read(r,QVariant(options),o0)) return failure();
        return GA::autoLevels(source,o0);
    }
    if(op=="autoContrast") {
        AutoToneOptions o0{};
        auto options=p;
        if (!read(r,QVariant(options),o0)) return failure();
        return GA::autoContrast(source,o0);
    }
    if(op=="autoColor") {
        AutoToneOptions o0{};
        auto options=p;
        if (!read(r,QVariant(options),o0)) return failure();
        return GA::autoColor(source,o0);
    }
    if(op=="filterGaussianBlur") {
        double a0=1;
        if (!member(r,p,"sigma",a0)) return failure();
        if (!r.keys(p,{"sigma"})) return failure();
        return GA::filterGaussianBlur(source,a0);
    }
    if(op=="boxBlur") {
        double a0=1;
        if (!member(r,p,"radius",a0)) return failure();
        if (!r.keys(p,{"radius"})) return failure();
        return GA::boxBlur(source,a0);
    }
    if(op=="blur") {
        if (!r.keys(p,{})) return failure();
        return GA::blur(source);
    }
    if(op=="blurMore") {
        if (!r.keys(p,{})) return failure();
        return GA::blurMore(source);
    }
    if(op=="motionBlur") {
        MotionBlurOptions o0{};
        auto options=p;
        if (!read(r,QVariant(options),o0)) return failure();
        return GA::motionBlur(source,o0);
    }
    if(op=="radialBlur") {
        RadialBlurOptions o0{};
        auto options=p;
        if (!read(r,QVariant(options),o0)) return failure();
        return GA::radialBlur(source,o0);
    }
    if(op=="surfaceBlur") {
        SurfaceBlurOptions o0{};
        auto options=p;
        if (!read(r,QVariant(options),o0)) return failure();
        return GA::surfaceBlur(source,o0);
    }
    if(op=="unsharpMask") {
        UnsharpMaskOptions o0{};
        auto options=p;
        if (!read(r,QVariant(options),o0)) return failure();
        return GA::unsharpMask(source,o0);
    }
    if(op=="filterSharpen") {
        if (!r.keys(p,{})) return failure();
        return GA::filterSharpen(source);
    }
    if(op=="sharpenMore") {
        if (!r.keys(p,{})) return failure();
        return GA::sharpenMore(source);
    }
    if(op=="sharpenEdges") {
        if (!r.keys(p,{})) return failure();
        return GA::sharpenEdges(source);
    }
    if(op=="smartSharpen") {
        SmartSharpenOptions o0{};
        auto options=p;
        if (!read(r,QVariant(options),o0)) return failure();
        return GA::smartSharpen(source,o0);
    }
    if(op=="highPass") {
        double a0=10;
        if (!member(r,p,"radius",a0)) return failure();
        if (!r.keys(p,{"radius"})) return failure();
        return GA::highPass(source,a0);
    }
    if(op=="despeckle") {
        if (!r.keys(p,{})) return failure();
        return GA::despeckle(source);
    }
    if(op=="smartBlur") {
        SmartBlurOptions o0{};
        auto options=p;
        if (!read(r,QVariant(options),o0)) return failure();
        return GA::smartBlur(source,o0);
    }
    if(op=="shapeBlur") {
        ShapeBlurOptions o0{};
        auto options=p;
        if (!read(r,QVariant(options),o0)) return failure();
        return GA::shapeBlur(source,o0);
    }
    if(op=="lensBlur") {
        LensBlurOptions o0{};
        auto options=p;
        if (!read(r,QVariant(options),o0)) return failure();
        return GA::lensBlur(source,o0);
    }
    if(op=="tiltShift") {
        TiltShiftOptions o0{};
        auto options=p;
        if (!read(r,QVariant(options),o0)) return failure();
        return GA::tiltShift(source,o0);
    }
    if(op=="irisBlur") {
        std::span<const IrisBlurPin> a0={};
        if (!member(r,p,"pins",a0)) return failure();
        if (!r.keys(p,{"pins"})) return failure();
        return GA::irisBlur(source,a0);
    }
    if(op=="fieldBlur") {
        std::span<const FieldBlurPin> a0={};
        if (!member(r,p,"pins",a0)) return failure();
        if (!r.keys(p,{"pins"})) return failure();
        return GA::fieldBlur(source,a0);
    }
    if(op=="spinBlur") {
        std::span<const SpinBlurPin> a0={};
        if (!member(r,p,"pins",a0)) return failure();
        if (!r.keys(p,{"pins"})) return failure();
        return GA::spinBlur(source,a0);
    }
    if(op=="pathBlur") {
        std::span<const BlurPath> a0={};
        if (!member(r,p,"paths",a0)) return failure();
        if (!r.keys(p,{"paths"})) return failure();
        return GA::pathBlur(source,a0);
    }
    if(op=="whiteBalance") {
        WhiteBalanceOptions o0{};
        auto options=p;
        if (!read(r,QVariant(options),o0)) return failure();
        return GA::whiteBalance(source,o0);
    }
    if(op=="developLight") {
        DevelopLightOptions o0{};
        auto options=p;
        if (!read(r,QVariant(options),o0)) return failure();
        return GA::developLight(source,o0);
    }
    if(op=="developCurve") {
        DevelopCurveOptions o0{};
        auto options=p;
        if (!read(r,QVariant(options),o0)) return failure();
        return GA::developCurve(source,o0);
    }
    if(op=="average") {
        if (!r.keys(p,{})) return failure();
        return GA::average(source);
    }
    if(op=="filterAddNoise") {
        FilterNoiseOptions o0{};
        auto options=p;
        if (!read(r,QVariant(options),o0)) return failure();
        return GA::filterAddNoise(source,o0);
    }
    if(op=="median") {
        double a0=1;
        if (!member(r,p,"radius",a0)) return failure();
        if (!r.keys(p,{"radius"})) return failure();
        return GA::median(source,a0);
    }
    if(op=="dustAndScratches") {
        DustAndScratchesOptions o0{};
        auto options=p;
        if (!read(r,QVariant(options),o0)) return failure();
        return GA::dustAndScratches(source,o0);
    }
    if(op=="minimum") {
        MorphologyOptions o0{};
        auto options=p;
        if (!read(r,QVariant(options),o0)) return failure();
        return GA::minimum(source,o0);
    }
    if(op=="maximum") {
        MorphologyOptions o0{};
        auto options=p;
        if (!read(r,QVariant(options),o0)) return failure();
        return GA::maximum(source,o0);
    }
    if(op=="reduceNoise") {
        ReduceNoiseOptions o0{};
        auto options=p;
        if (!read(r,QVariant(options),o0)) return failure();
        return GA::reduceNoise(source,o0);
    }
    if(op=="customFilter") {
        CustomFilterOptions o0{};
        auto options=p;
        if (!read(r,QVariant(options),o0)) return failure();
        return GA::customFilter(source,o0);
    }
    if(op=="hsbHsl") {
        FilterColorModel a0={};
        if (!member(r,p,"input",a0)) return failure();
        if (!p.contains("input")) { r.fail("Supply parameter: input"); return failure(); }
        FilterColorModel a1={};
        if (!member(r,p,"output",a1)) return failure();
        if (!p.contains("output")) { r.fail("Supply parameter: output"); return failure(); }
        if (!r.keys(p,{"input","output"})) return failure();
        return GA::hsbHsl(source,a0,a1);
    }
    if(op=="deInterlace") {
        bool a0=false;
        if (!member(r,p,"eliminateEven",a0)) return failure();
        bool a1=true;
        if (!member(r,p,"interpolate",a1)) return failure();
        if (!r.keys(p,{"eliminateEven","interpolate"})) return failure();
        return GA::deInterlace(source,a0,a1);
    }
    if(op=="ntscColors") {
        if (!r.keys(p,{})) return failure();
        return GA::ntscColors(source);
    }
    if(op=="guidedBlur") {
        GuidedBlurOptions o0{};
        auto options=p;
        if (!read(r,QVariant(options),o0)) return failure();
        return GA::guidedBlur(source,o0);
    }
    if(op=="crossGuidedBlur") {
        ImageView a0={};
        if (!member(r,p,"guide",a0)) return failure();
        if (!p.contains("guide")) { r.fail("Supply parameter: guide"); return failure(); }
        GuidedBlurOptions o1{};
        auto options=p; options.remove("guide");
        if (!read(r,QVariant(options),o1)) return failure();
        return GA::crossGuidedBlur(source,a0,o1);
    }
    if(op=="liquify") {
        LiquifyOptions o0{};
        auto options=p;
        if (!read(r,QVariant(options),o0)) return failure();
        return GA::liquify(source,o0);
    }
    if(op=="adaptiveWideAngle") {
        AdaptiveWideAngleOptions o0{};
        auto options=p;
        if (!read(r,QVariant(options),o0)) return failure();
        return GA::adaptiveWideAngle(source,o0);
    }
    if(op=="puppetWarp") {
        PuppetWarpOptions o0{};
        auto options=p;
        if (!read(r,QVariant(options),o0)) return failure();
        return GA::puppetWarp(source,o0);
    }
    if(op=="perspectiveWarp") {
        PerspectiveWarpOptions o0{};
        auto options=p;
        if (!read(r,QVariant(options),o0)) return failure();
        return GA::perspectiveWarp(source,o0);
    }
    if(op=="vanishingPoint") {
        VanishingPointOptions o0{};
        auto options=p;
        if (!read(r,QVariant(options),o0)) return failure();
        return GA::vanishingPoint(source,o0);
    }
    if(op=="pointColorRangePreview") {
        PointColorOptions o0{};
        auto options=p;
        if (!read(r,QVariant(options),o0)) return failure();
        return GA::pointColorRangePreview(source,o0);
    }
    if(op=="maskColorOverlay") {
        MaskView a0={};
        if (!maskMember(r,p,"mask",a0)) return failure();
        MaskOverlayOptions o1{};
        auto options=p; options.remove("mask");
        if (!read(r,QVariant(options),o1)) return failure();
        return GA::maskColorOverlay(source,a0,o1);
    }
    if(op=="maskColorOnBwOverlay") {
        MaskView a0={};
        if (!maskMember(r,p,"mask",a0)) return failure();
        MaskOverlayOptions o1{};
        auto options=p; options.remove("mask");
        if (!read(r,QVariant(options),o1)) return failure();
        return GA::maskColorOnBwOverlay(source,a0,o1);
    }
    if(op=="maskImageOnBlack") {
        MaskView a0={};
        if (!maskMember(r,p,"mask",a0)) return failure();
        if (!r.keys(p,{"mask"})) return failure();
        return GA::maskImageOnBlack(source,a0);
    }
    if(op=="maskImageOnWhite") {
        MaskView a0={};
        if (!maskMember(r,p,"mask",a0)) return failure();
        if (!r.keys(p,{"mask"})) return failure();
        return GA::maskImageOnWhite(source,a0);
    }
    if(op=="maskWhiteOnBlack") {
        MaskView a0={};
        if (!maskMember(r,p,"mask",a0)) return failure();
        if (!r.keys(p,{"mask"})) return failure();
        return GA::maskWhiteOnBlack(source,a0);
    }
    if(op=="destinationGamutWarning") {
        GamutWarningOptions o0{};
        auto options=p;
        if (!read(r,QVariant(options),o0)) return failure();
        return GA::destinationGamutWarning(source,o0);
    }
    if(op=="displayGamutWarning") {
        GamutWarningOptions o0{};
        auto options=p;
        if (!read(r,QVariant(options),o0)) return failure();
        return GA::displayGamutWarning(source,o0);
    }
    if(op=="applyLightcraftLook") {
        LightcraftLook a0={};
        if (!member(r,p,"look",a0)) return failure();
        if (!p.contains("look")) { r.fail("Supply parameter: look"); return failure(); }
        double a1=1;
        if (!member(r,p,"amount",a1)) return failure();
        if (!r.keys(p,{"look","amount"})) return failure();
        return GA::applyLightcraftLook(source,a0,a1);
    }
    if(op=="applyLightcraftPreset") {
        LightcraftPreset a0={};
        if (!member(r,p,"preset",a0)) return failure();
        if (!p.contains("preset")) { r.fail("Supply parameter: preset"); return failure(); }
        double a1=1;
        if (!member(r,p,"amount",a1)) return failure();
        if (!r.keys(p,{"preset","amount"})) return failure();
        return GA::applyLightcraftPreset(source,a0,a1);
    }
    if(op=="lightcraftLookColor") {
        double a0=1;
        if (!member(r,p,"amount",a0)) return failure();
        if (!r.keys(p,{"amount"})) return failure();
        return GA::lightcraftLookColor(source,a0);
    }
    if(op=="lightcraftLookNeutral") {
        double a0=1;
        if (!member(r,p,"amount",a0)) return failure();
        if (!r.keys(p,{"amount"})) return failure();
        return GA::lightcraftLookNeutral(source,a0);
    }
    if(op=="lightcraftLookVivid") {
        double a0=1;
        if (!member(r,p,"amount",a0)) return failure();
        if (!r.keys(p,{"amount"})) return failure();
        return GA::lightcraftLookVivid(source,a0);
    }
    if(op=="lightcraftLookLandscape") {
        double a0=1;
        if (!member(r,p,"amount",a0)) return failure();
        if (!r.keys(p,{"amount"})) return failure();
        return GA::lightcraftLookLandscape(source,a0);
    }
    if(op=="lightcraftLookPortrait") {
        double a0=1;
        if (!member(r,p,"amount",a0)) return failure();
        if (!r.keys(p,{"amount"})) return failure();
        return GA::lightcraftLookPortrait(source,a0);
    }
    if(op=="lightcraftLookMono") {
        double a0=1;
        if (!member(r,p,"amount",a0)) return failure();
        if (!r.keys(p,{"amount"})) return failure();
        return GA::lightcraftLookMono(source,a0);
    }
    if(op=="lightcraftLookFilmWarmPrint") {
        double a0=1;
        if (!member(r,p,"amount",a0)) return failure();
        if (!r.keys(p,{"amount"})) return failure();
        return GA::lightcraftLookFilmWarmPrint(source,a0);
    }
    if(op=="lightcraftLookFilmCoolFade") {
        double a0=1;
        if (!member(r,p,"amount",a0)) return failure();
        if (!r.keys(p,{"amount"})) return failure();
        return GA::lightcraftLookFilmCoolFade(source,a0);
    }
    if(op=="lightcraftLookFilmGoldenHour") {
        double a0=1;
        if (!member(r,p,"amount",a0)) return failure();
        if (!r.keys(p,{"amount"})) return failure();
        return GA::lightcraftLookFilmGoldenHour(source,a0);
    }
    if(op=="lightcraftLookFilmFadedSlide") {
        double a0=1;
        if (!member(r,p,"amount",a0)) return failure();
        if (!r.keys(p,{"amount"})) return failure();
        return GA::lightcraftLookFilmFadedSlide(source,a0);
    }
    if(op=="lightcraftLookCineTealAmber") {
        double a0=1;
        if (!member(r,p,"amount",a0)) return failure();
        if (!r.keys(p,{"amount"})) return failure();
        return GA::lightcraftLookCineTealAmber(source,a0);
    }
    if(op=="lightcraftLookCineNightBlue") {
        double a0=1;
        if (!member(r,p,"amount",a0)) return failure();
        if (!r.keys(p,{"amount"})) return failure();
        return GA::lightcraftLookCineNightBlue(source,a0);
    }
    if(op=="lightcraftLookCineDesertHeat") {
        double a0=1;
        if (!member(r,p,"amount",a0)) return failure();
        if (!r.keys(p,{"amount"})) return failure();
        return GA::lightcraftLookCineDesertHeat(source,a0);
    }
    if(op=="lightcraftLookCineNeonDusk") {
        double a0=1;
        if (!member(r,p,"amount",a0)) return failure();
        if (!r.keys(p,{"amount"})) return failure();
        return GA::lightcraftLookCineNeonDusk(source,a0);
    }
    if(op=="lightcraftLookMutedMatteSoft") {
        double a0=1;
        if (!member(r,p,"amount",a0)) return failure();
        if (!r.keys(p,{"amount"})) return failure();
        return GA::lightcraftLookMutedMatteSoft(source,a0);
    }
    if(op=="lightcraftLookMutedBleached") {
        double a0=1;
        if (!member(r,p,"amount",a0)) return failure();
        if (!r.keys(p,{"amount"})) return failure();
        return GA::lightcraftLookMutedBleached(source,a0);
    }
    if(op=="lightcraftLookMutedPastelHaze") {
        double a0=1;
        if (!member(r,p,"amount",a0)) return failure();
        if (!r.keys(p,{"amount"})) return failure();
        return GA::lightcraftLookMutedPastelHaze(source,a0);
    }
    if(op=="lightcraftLookMutedQuietGreen") {
        double a0=1;
        if (!member(r,p,"amount",a0)) return failure();
        if (!r.keys(p,{"amount"})) return failure();
        return GA::lightcraftLookMutedQuietGreen(source,a0);
    }
    if(op=="lightcraftLookBwMonoRich") {
        double a0=1;
        if (!member(r,p,"amount",a0)) return failure();
        if (!r.keys(p,{"amount"})) return failure();
        return GA::lightcraftLookBwMonoRich(source,a0);
    }
    if(op=="lightcraftLookBwRedFilter") {
        double a0=1;
        if (!member(r,p,"amount",a0)) return failure();
        if (!r.keys(p,{"amount"})) return failure();
        return GA::lightcraftLookBwRedFilter(source,a0);
    }
    if(op=="lightcraftLookBwSoft") {
        double a0=1;
        if (!member(r,p,"amount",a0)) return failure();
        if (!r.keys(p,{"amount"})) return failure();
        return GA::lightcraftLookBwSoft(source,a0);
    }
    if(op=="lightcraftLookBwSepia") {
        double a0=1;
        if (!member(r,p,"amount",a0)) return failure();
        if (!r.keys(p,{"amount"})) return failure();
        return GA::lightcraftLookBwSepia(source,a0);
    }
    if(op=="lightcraftPresetWarmGlow") {
        double a0=1;
        if (!member(r,p,"amount",a0)) return failure();
        if (!r.keys(p,{"amount"})) return failure();
        return GA::lightcraftPresetWarmGlow(source,a0);
    }
    if(op=="lightcraftPresetCoolMorning") {
        double a0=1;
        if (!member(r,p,"amount",a0)) return failure();
        if (!r.keys(p,{"amount"})) return failure();
        return GA::lightcraftPresetCoolMorning(source,a0);
    }
    if(op=="lightcraftPresetTealOrange") {
        double a0=1;
        if (!member(r,p,"amount",a0)) return failure();
        if (!r.keys(p,{"amount"})) return failure();
        return GA::lightcraftPresetTealOrange(source,a0);
    }
    if(op=="lightcraftPresetVividPop") {
        double a0=1;
        if (!member(r,p,"amount",a0)) return failure();
        if (!r.keys(p,{"amount"})) return failure();
        return GA::lightcraftPresetVividPop(source,a0);
    }
    if(op=="lightcraftPresetSoftPastel") {
        double a0=1;
        if (!member(r,p,"amount",a0)) return failure();
        if (!r.keys(p,{"amount"})) return failure();
        return GA::lightcraftPresetSoftPastel(source,a0);
    }
    if(op=="lightcraftPresetFadedMatte") {
        double a0=1;
        if (!member(r,p,"amount",a0)) return failure();
        if (!r.keys(p,{"amount"})) return failure();
        return GA::lightcraftPresetFadedMatte(source,a0);
    }
    if(op=="lightcraftPresetWarmFilm") {
        double a0=1;
        if (!member(r,p,"amount",a0)) return failure();
        if (!r.keys(p,{"amount"})) return failure();
        return GA::lightcraftPresetWarmFilm(source,a0);
    }
    if(op=="lightcraftPresetMutedFilm") {
        double a0=1;
        if (!member(r,p,"amount",a0)) return failure();
        if (!r.keys(p,{"amount"})) return failure();
        return GA::lightcraftPresetMutedFilm(source,a0);
    }
    if(op=="lightcraftPresetBwHighContrast") {
        double a0=1;
        if (!member(r,p,"amount",a0)) return failure();
        if (!r.keys(p,{"amount"})) return failure();
        return GA::lightcraftPresetBwHighContrast(source,a0);
    }
    if(op=="lightcraftPresetBwSoft") {
        double a0=1;
        if (!member(r,p,"amount",a0)) return failure();
        if (!r.keys(p,{"amount"})) return failure();
        return GA::lightcraftPresetBwSoft(source,a0);
    }
    if(op=="lightcraftPresetBwSelenium") {
        double a0=1;
        if (!member(r,p,"amount",a0)) return failure();
        if (!r.keys(p,{"amount"})) return failure();
        return GA::lightcraftPresetBwSelenium(source,a0);
    }
    if(op=="lightcraftPresetCrispLandscape") {
        double a0=1;
        if (!member(r,p,"amount",a0)) return failure();
        if (!r.keys(p,{"amount"})) return failure();
        return GA::lightcraftPresetCrispLandscape(source,a0);
    }
    if(op=="lightcraftPresetGoldenHour") {
        double a0=1;
        if (!member(r,p,"amount",a0)) return failure();
        if (!r.keys(p,{"amount"})) return failure();
        return GA::lightcraftPresetGoldenHour(source,a0);
    }
    if(op=="lightcraftPresetBlueHour") {
        double a0=1;
        if (!member(r,p,"amount",a0)) return failure();
        if (!r.keys(p,{"amount"})) return failure();
        return GA::lightcraftPresetBlueHour(source,a0);
    }
    if(op=="lightcraftPresetSoftSkin") {
        double a0=1;
        if (!member(r,p,"amount",a0)) return failure();
        if (!r.keys(p,{"amount"})) return failure();
        return GA::lightcraftPresetSoftSkin(source,a0);
    }
    if(op=="lightcraftPresetBrightAiry") {
        double a0=1;
        if (!member(r,p,"amount",a0)) return failure();
        if (!r.keys(p,{"amount"})) return failure();
        return GA::lightcraftPresetBrightAiry(source,a0);
    }
    if(op=="lightcraftPresetMoody") {
        double a0=1;
        if (!member(r,p,"amount",a0)) return failure();
        if (!r.keys(p,{"amount"})) return failure();
        return GA::lightcraftPresetMoody(source,a0);
    }
    if(op=="lightcraftPresetCinematic") {
        double a0=1;
        if (!member(r,p,"amount",a0)) return failure();
        if (!r.keys(p,{"amount"})) return failure();
        return GA::lightcraftPresetCinematic(source,a0);
    }
    if(op=="lightcraftPresetMoodyPortrait") {
        double a0=1;
        if (!member(r,p,"amount",a0)) return failure();
        if (!r.keys(p,{"amount"})) return failure();
        return GA::lightcraftPresetMoodyPortrait(source,a0);
    }
    if(op=="lightcraftPresetGoldenGlow") {
        double a0=1;
        if (!member(r,p,"amount",a0)) return failure();
        if (!r.keys(p,{"amount"})) return failure();
        return GA::lightcraftPresetGoldenGlow(source,a0);
    }
    if(op=="lightcraftPresetCleanStudio") {
        double a0=1;
        if (!member(r,p,"amount",a0)) return failure();
        if (!r.keys(p,{"amount"})) return failure();
        return GA::lightcraftPresetCleanStudio(source,a0);
    }
    if(op=="lightcraftPresetCrispVista") {
        double a0=1;
        if (!member(r,p,"amount",a0)) return failure();
        if (!r.keys(p,{"amount"})) return failure();
        return GA::lightcraftPresetCrispVista(source,a0);
    }
    if(op=="lightcraftPresetDeepSky") {
        double a0=1;
        if (!member(r,p,"amount",a0)) return failure();
        if (!r.keys(p,{"amount"})) return failure();
        return GA::lightcraftPresetDeepSky(source,a0);
    }
    if(op=="lightcraftPresetLushGreens") {
        double a0=1;
        if (!member(r,p,"amount",a0)) return failure();
        if (!r.keys(p,{"amount"})) return failure();
        return GA::lightcraftPresetLushGreens(source,a0);
    }
    if(op=="lightcraftPresetDesertWarmth") {
        double a0=1;
        if (!member(r,p,"amount",a0)) return failure();
        if (!r.keys(p,{"amount"})) return failure();
        return GA::lightcraftPresetDesertWarmth(source,a0);
    }
    if(op=="lightcraftPresetMistyMorning") {
        double a0=1;
        if (!member(r,p,"amount",a0)) return failure();
        if (!r.keys(p,{"amount"})) return failure();
        return GA::lightcraftPresetMistyMorning(source,a0);
    }
    if(op=="lightcraftPresetGrittyStreet") {
        double a0=1;
        if (!member(r,p,"amount",a0)) return failure();
        if (!r.keys(p,{"amount"})) return failure();
        return GA::lightcraftPresetGrittyStreet(source,a0);
    }
    if(op=="lightcraftPresetNeonNight") {
        double a0=1;
        if (!member(r,p,"amount",a0)) return failure();
        if (!r.keys(p,{"amount"})) return failure();
        return GA::lightcraftPresetNeonNight(source,a0);
    }
    if(op=="lightcraftPresetConcreteCool") {
        double a0=1;
        if (!member(r,p,"amount",a0)) return failure();
        if (!r.keys(p,{"amount"})) return failure();
        return GA::lightcraftPresetConcreteCool(source,a0);
    }
    if(op=="lightcraftPresetFadedUrban") {
        double a0=1;
        if (!member(r,p,"amount",a0)) return failure();
        if (!r.keys(p,{"amount"})) return failure();
        return GA::lightcraftPresetFadedUrban(source,a0);
    }
    if(op=="lightcraftPresetFreshBright") {
        double a0=1;
        if (!member(r,p,"amount",a0)) return failure();
        if (!r.keys(p,{"amount"})) return failure();
        return GA::lightcraftPresetFreshBright(source,a0);
    }
    if(op=="lightcraftPresetWarmTable") {
        double a0=1;
        if (!member(r,p,"amount",a0)) return failure();
        if (!r.keys(p,{"amount"})) return failure();
        return GA::lightcraftPresetWarmTable(source,a0);
    }
    if(op=="lightcraftPresetDarkMoodyFood") {
        double a0=1;
        if (!member(r,p,"amount",a0)) return failure();
        if (!r.keys(p,{"amount"})) return failure();
        return GA::lightcraftPresetDarkMoodyFood(source,a0);
    }
    if(op=="lightcraftPresetAutumnGold") {
        double a0=1;
        if (!member(r,p,"amount",a0)) return failure();
        if (!r.keys(p,{"amount"})) return failure();
        return GA::lightcraftPresetAutumnGold(source,a0);
    }
    if(op=="lightcraftPresetWinterBlue") {
        double a0=1;
        if (!member(r,p,"amount",a0)) return failure();
        if (!r.keys(p,{"amount"})) return failure();
        return GA::lightcraftPresetWinterBlue(source,a0);
    }
    if(op=="lightcraftPresetSpringFresh") {
        double a0=1;
        if (!member(r,p,"amount",a0)) return failure();
        if (!r.keys(p,{"amount"})) return failure();
        return GA::lightcraftPresetSpringFresh(source,a0);
    }
    if(op=="lightcraftPresetSummerHaze") {
        double a0=1;
        if (!member(r,p,"amount",a0)) return failure();
        if (!r.keys(p,{"amount"})) return failure();
        return GA::lightcraftPresetSummerHaze(source,a0);
    }
    if(op=="lightcraftPresetInstant70s") {
        double a0=1;
        if (!member(r,p,"amount",a0)) return failure();
        if (!r.keys(p,{"amount"})) return failure();
        return GA::lightcraftPresetInstant70s(source,a0);
    }
    if(op=="lightcraftPresetCrossProcess") {
        double a0=1;
        if (!member(r,p,"amount",a0)) return failure();
        if (!r.keys(p,{"amount"})) return failure();
        return GA::lightcraftPresetCrossProcess(source,a0);
    }
    if(op=="lightcraftPresetBleachBypass") {
        double a0=1;
        if (!member(r,p,"amount",a0)) return failure();
        if (!r.keys(p,{"amount"})) return failure();
        return GA::lightcraftPresetBleachBypass(source,a0);
    }
    if(op=="lightcraftPresetBwSepia") {
        double a0=1;
        if (!member(r,p,"amount",a0)) return failure();
        if (!r.keys(p,{"amount"})) return failure();
        return GA::lightcraftPresetBwSepia(source,a0);
    }
    if(op=="resizeBox") {
        std::size_t a0={};
        if (!member(r,p,"width",a0)) return failure();
        if (!p.contains("width")) { r.fail("Supply parameter: width"); return failure(); }
        std::size_t a1={};
        if (!member(r,p,"height",a1)) return failure();
        if (!p.contains("height")) { r.fail("Supply parameter: height"); return failure(); }
        if (!r.keys(p,{"width","height"})) return failure();
        return GA::resizeBox(source,a0,a1);
    }
    if(op=="resizeBilinear") {
        std::size_t a0={};
        if (!member(r,p,"width",a0)) return failure();
        if (!p.contains("width")) { r.fail("Supply parameter: width"); return failure(); }
        std::size_t a1={};
        if (!member(r,p,"height",a1)) return failure();
        if (!p.contains("height")) { r.fail("Supply parameter: height"); return failure(); }
        if (!r.keys(p,{"width","height"})) return failure();
        return GA::resizeBilinear(source,a0,a1);
    }
    if(op=="resizeMitchell") {
        std::size_t a0={};
        if (!member(r,p,"width",a0)) return failure();
        if (!p.contains("width")) { r.fail("Supply parameter: width"); return failure(); }
        std::size_t a1={};
        if (!member(r,p,"height",a1)) return failure();
        if (!p.contains("height")) { r.fail("Supply parameter: height"); return failure(); }
        if (!r.keys(p,{"width","height"})) return failure();
        return GA::resizeMitchell(source,a0,a1);
    }
    if(op=="resizeLanczos3") {
        std::size_t a0={};
        if (!member(r,p,"width",a0)) return failure();
        if (!p.contains("width")) { r.fail("Supply parameter: width"); return failure(); }
        std::size_t a1={};
        if (!member(r,p,"height",a1)) return failure();
        if (!p.contains("height")) { r.fail("Supply parameter: height"); return failure(); }
        if (!r.keys(p,{"width","height"})) return failure();
        return GA::resizeLanczos3(source,a0,a1);
    }
    if(op=="develop") {
        DevelopOptions o0{};
        auto options=p;
        if (!read(r,QVariant(options),o0)) return failure();
        return GA::develop(source,o0);
    }
    if(op=="lensCorrection") {
        LensCorrectionOptions o0{};
        auto options=p;
        if (!read(r,QVariant(options),o0)) return failure();
        return GA::lensCorrection(source,o0);
    }
    if(op=="correctOptics") {
        OpticsOptions o0{};
        auto options=p;
        if (!read(r,QVariant(options),o0)) return failure();
        return GA::correctOptics(source,o0);
    }
    if(op=="colorHalftone") {
        ColorHalftoneOptions o0{};
        auto options=p;
        if (!read(r,QVariant(options),o0)) return failure();
        return GA::colorHalftone(source,o0);
    }
    if(op=="crystallize") {
        CrystallizeOptions o0{};
        auto options=p;
        if (!read(r,QVariant(options),o0)) return failure();
        return GA::crystallize(source,o0);
    }
    if(op=="facet") {
        if (!r.keys(p,{})) return failure();
        return GA::facet(source);
    }
    if(op=="fragment") {
        if (!r.keys(p,{})) return failure();
        return GA::fragment(source);
    }
    if(op=="mezzotint") {
        MezzotintOptions o0{};
        auto options=p;
        if (!read(r,QVariant(options),o0)) return failure();
        return GA::mezzotint(source,o0);
    }
    if(op=="pointillize") {
        PointillizeOptions o0{};
        auto options=p;
        if (!read(r,QVariant(options),o0)) return failure();
        return GA::pointillize(source,o0);
    }
    if(op=="mosaic") {
        double a0=10;
        if (!member(r,p,"cellSize",a0)) return failure();
        if (!r.keys(p,{"cellSize"})) return failure();
        return GA::mosaic(source,a0);
    }
    if(op=="emboss") {
        EmbossOptions o0{};
        auto options=p;
        if (!read(r,QVariant(options),o0)) return failure();
        return GA::emboss(source,o0);
    }
    if(op=="findEdges") {
        if (!r.keys(p,{})) return failure();
        return GA::findEdges(source);
    }
    if(op=="solarize") {
        if (!r.keys(p,{})) return failure();
        return GA::solarize(source);
    }
    if(op=="diffuse") {
        DiffuseOptions o0{};
        auto options=p;
        if (!read(r,QVariant(options),o0)) return failure();
        return GA::diffuse(source,o0);
    }
    if(op=="extrude") {
        ExtrudeOptions o0{};
        auto options=p;
        if (!read(r,QVariant(options),o0)) return failure();
        return GA::extrude(source,o0);
    }
    if(op=="oilPaint") {
        OilPaintOptions o0{};
        auto options=p;
        if (!read(r,QVariant(options),o0)) return failure();
        return GA::oilPaint(source,o0);
    }
    if(op=="tiles") {
        TilesOptions o0{};
        auto options=p;
        if (!read(r,QVariant(options),o0)) return failure();
        return GA::tiles(source,o0);
    }
    if(op=="traceContour") {
        TraceContourOptions o0{};
        auto options=p;
        if (!read(r,QVariant(options),o0)) return failure();
        return GA::traceContour(source,o0);
    }
    if(op=="wind") {
        WindOptions o0{};
        auto options=p;
        if (!read(r,QVariant(options),o0)) return failure();
        return GA::wind(source,o0);
    }
    r.fail("Unsupported image operation: "+op); return failure();
}
const QMap<QString,QStringList> &parameterCatalog() { static const QMap<QString,QStringList> result {
    {"blend",{"source","mode","opacity","space"}},
    {"applyLocalAdjustments",{"adjustments"}},
    {"gradientMap",{"stops","reverse","dither","amount"}},
    {"colorLookup",{"lut","interpolation","amount","dither"}},
    {"cameraRawFilter",{"temperature","tint","exposure","contrast","highlights","shadows","whites","blacks","texture","clarity","dehaze","vibrance","saturation","curveHighlights","curveLights","curveDarks","curveShadows","curveSplits","pointCurve","pointCurveRed","pointCurveGreen","pointCurveBlue","hslHue","hslSaturation","hslLuminance","gradeShadows","gradeMidtones","gradeHighlights","gradeGlobal","gradeBlending","gradeBalance","sharpenAmount","sharpenRadius","sharpenDetail","sharpenMasking","noiseLuminance","noiseLuminanceDetail","noiseColor","noiseColorDetail","grainAmount","grainSize","grainRoughness","vignetteAmount","vignetteMidpoint","vignetteRoundness","vignetteFeather","vignetteHighlights","vignetteStyle","seed","pixelScale"}},
    {"offset",{"horizontal","vertical","edge"}},
    {"twirl",{"angle"}},
    {"pinch",{"amount"}},
    {"spherize",{"amount","mode"}},
    {"wave",{"generators","wavelengthMin","wavelengthMax","amplitudeMin","amplitudeMax","type","edge","seed"}},
    {"ripple",{"amount","size"}},
    {"polarCoordinates",{"mode"}},
    {"displace",{"map","horizontal","vertical","stretch","edge"}},
    {"shear",{"points","edge"}},
    {"zigZag",{"amount","ridges","style"}},
    {"developEffects",{"texture","clarity","dehaze"}},
    {"postCropVignette",{"amount","midpoint","roundness","feather","highlights","style"}},
    {"filmGrain",{"amount","size","roughness","seed"}},
    {"developDetail",{"sharpenAmount","sharpenMasking","luminanceNoise","luminanceDetail","colorNoise","colorDetail","colorSmoothness"}},
    {"hueSaturation",{"hue","saturation","lightness","range","falloff","colorize"}},
    {"vibrance",{"vibrance","saturation"}},
    {"colorBalance",{"shadows","midtones","highlights","preserveLuminosity"}},
    {"blackAndWhite",{"weights","tint","tintHue","tintSaturation"}},
    {"photoFilter",{"color","density","preserveLuminosity"}},
    {"channelMixer",{"red","green","blue","monochrome","gray"}},
    {"selectiveColor",{"ranges","method"}},
    {"desaturate",{}},
    {"replaceColor",{"target","fuzziness","softness","hue","saturation","lightness"}},
    {"matchColor",{"reference","luminance","colorIntensity","fade","neutralize"}},
    {"coloredPencil",{"pencilWidth","strokePressure","paperBrightness","background","seed"}},
    {"cutout",{"numberOfLevels","edgeSimplicity","edgeFidelity"}},
    {"dryBrush",{"brushSize","brushDetail","texture","seed"}},
    {"galleryFilmGrain",{"grain","highlightArea","intensity","seed"}},
    {"fresco",{"brushSize","brushDetail","texture","seed"}},
    {"neonGlow",{"glowSize","glowBrightness","glowColor","foreground","background"}},
    {"paintDaubs",{"brushSize","sharpness","brushType","seed"}},
    {"paletteKnife",{"strokeSize","strokeDetail","softness"}},
    {"plasticWrap",{"highlightStrength","detail","smoothness"}},
    {"posterEdges",{"edgeThickness","edgeIntensity","posterization"}},
    {"roughPastels",{"strokeLength","strokeDetail","texture","scaling","relief","light","invert","seed"}},
    {"smudgeStick",{"strokeLength","highlightArea","intensity"}},
    {"sponge",{"brushSize","definition","smoothness","seed"}},
    {"underpainting",{"brushSize","textureCoverage","texture","scaling","relief","light","invert","seed"}},
    {"watercolor",{"brushDetail","shadowIntensity","texture","seed"}},
    {"accentedEdges",{"edgeWidth","edgeBrightness","smoothness"}},
    {"angledStrokes",{"directionBalance","strokeLength","sharpness","seed"}},
    {"crosshatch",{"strokeLength","sharpness","strength","seed"}},
    {"darkStrokes",{"balance","blackIntensity","whiteIntensity"}},
    {"inkOutlines",{"strokeLength","darkIntensity","lightIntensity"}},
    {"spatter",{"sprayRadius","smoothness","seed"}},
    {"sprayedStrokes",{"strokeLength","sprayRadius","strokeDirection","seed"}},
    {"sumiE",{"strokeWidth","strokePressure","contrast"}},
    {"diffuseGlow",{"graininess","glowAmount","clearAmount","background","seed"}},
    {"glass",{"distortion","smoothness","texture","scaling","invert","seed"}},
    {"oceanRipple",{"rippleSize","rippleMagnitude","seed"}},
    {"basRelief",{"detail","smoothness","light","foreground","background"}},
    {"chalkCharcoal",{"charcoalArea","chalkArea","strokePressure","foreground","background","seed"}},
    {"charcoal",{"charcoalThickness","detail","lightDarkBalance","foreground","background","seed"}},
    {"chrome",{"detail","smoothness"}},
    {"conteCrayon",{"foregroundLevel","backgroundLevel","texture","scaling","relief","light","invert","foreground","background","seed"}},
    {"graphicPen",{"strokeLength","lightDarkBalance","strokeDirection","foreground","background","seed"}},
    {"halftonePattern",{"size","contrast","patternType","foreground","background"}},
    {"notePaper",{"imageBalance","graininess","relief","foreground","background","seed"}},
    {"photocopy",{"detail","darkness","foreground","background"}},
    {"plaster",{"imageBalance","smoothness","light","foreground","background"}},
    {"reticulation",{"density","foregroundLevel","backgroundLevel","foreground","background","seed"}},
    {"stamp",{"lightDarkBalance","smoothness","foreground","background"}},
    {"tornEdges",{"imageBalance","smoothness","contrast","foreground","background","seed"}},
    {"waterPaper",{"fiberLength","brightness","contrast","seed"}},
    {"glowingEdges",{"edgeWidth","edgeBrightness","smoothness"}},
    {"craquelure",{"crackSpacing","crackDepth","crackBrightness","seed"}},
    {"grain",{"intensity","contrast","grainType","foreground","background","seed"}},
    {"mosaicTiles",{"tileSize","groutWidth","lightenGrout","seed"}},
    {"patchwork",{"squareSize","relief","seed"}},
    {"stainedGlass",{"cellSize","borderThickness","lightIntensity","foreground","seed"}},
    {"texturizer",{"texture","scaling","relief","light","invert","seed"}},
    {"filterGallery",{"effects"}},
    {"cloneSpot",{"points","size","feather","opacity","sourceOffset"}},
    {"healSpot",{"points","size","feather","opacity","sourceOffset"}},
    {"removeSpot",{"points","size","feather","opacity","sourceOffset"}},
    {"redEye",{"center","radiusX","radiusY","pupilSize","darken","catchlight"}},
    {"petEye",{"center","radiusX","radiusY","pupilSize","darken","catchlight"}},
    {"visualizeSpots",{"sensitivity"}},
    {"gaussianBlur",{"sigma"}},
    {"sharpen",{"sigma","amount","threshold"}},
    {"addNoise",{"amount","seed","distribution","monochromatic"}},
    {"adjust",{"exposure","brightness","contrast","saturation","gamma","temperature","tint","opacity"}},
    {"developColor",{"vibrance","saturation","mixer","points","blackAndWhite","blackAndWhiteMix","grading"}},
    {"calibrate",{"shadowsTint","primaryHue","primarySaturation"}},
    {"hdrToning",{"method","exposure","gamma"}},
    {"clouds",{"foreground","background","seed"}},
    {"differenceClouds",{"foreground","background","seed"}},
    {"fibers",{"variance","strength","seed","foreground","background"}},
    {"lensFlare",{"brightness","centerX","centerY","lens"}},
    {"lightingEffects",{"lights","gloss","metallic","exposure","ambience","texture","height","whiteIsHigh"}},
    {"relight",{"angle","elevation","intensity","ambient","warmth","softness"}},
    {"flame",{"type","length","width","angle","interval","randomizeLength","adjustIntervalForLoops","color","turbulence","jaggedness","opacity","flameLines","bottomAlignment","style","shape","randomizeShapes","seed","quality","paths"}},
    {"tree",{"baseTreeType","lightDirection","leavesAmount","leavesSize","branchesHeight","branchesThickness","defaultLeaves","customBranchColor","flatShading","leavesColor","branchesColor","seed","x","y","size"}},
    {"pictureFrame",{"style","margin","size","arrangement","thickness","fade","lines","vineColor","flowerColor","leafColor","seed"}},
    {"brightnessContrast",{"brightness","contrast"}},
    {"levels",{"master","red","green","blue"}},
    {"curves",{"master","red","green","blue"}},
    {"exposure",{"stops","offset","gamma"}},
    {"shadowsHighlights",{"shadows","highlights","shadowWidth","highlightWidth"}},
    {"invert",{}},
    {"posterize",{"levelCount"}},
    {"threshold",{"level"}},
    {"equalize",{}},
    {"autoLevels",{"blackClip","whiteClip"}},
    {"autoContrast",{"blackClip","whiteClip"}},
    {"autoColor",{"blackClip","whiteClip"}},
    {"filterGaussianBlur",{"sigma"}},
    {"boxBlur",{"radius"}},
    {"blur",{}},
    {"blurMore",{}},
    {"motionBlur",{"angle","distance"}},
    {"radialBlur",{"amount","method","centerX","centerY"}},
    {"surfaceBlur",{"radius","threshold"}},
    {"unsharpMask",{"amount","radius","threshold"}},
    {"filterSharpen",{}},
    {"sharpenMore",{}},
    {"sharpenEdges",{}},
    {"smartSharpen",{"amount","radius","reduceNoise"}},
    {"highPass",{"radius"}},
    {"despeckle",{}},
    {"smartBlur",{"radius","threshold","quality","mode"}},
    {"shapeBlur",{"radius","shape"}},
    {"lensBlur",{"radius","blades","curvature","rotation","depth","focalDistance","invertDepth","brightness","threshold","noise","distribution","monochromatic","seed","depthMap"}},
    {"tiltShift",{"blur","centerX","centerY","angle","focus","transition"}},
    {"irisBlur",{"pins"}},
    {"fieldBlur",{"pins"}},
    {"spinBlur",{"pins"}},
    {"pathBlur",{"paths"}},
    {"whiteBalance",{"temperature","tint","asShotTemperature","asShotTint"}},
    {"developLight",{"exposure","contrast","highlights","shadows","whites","blacks"}},
    {"developCurve",{"shadows","darks","lights","highlights","splitShadows","splitMid","splitHighlights","master","red","green","blue","refineSaturation"}},
    {"average",{}},
    {"filterAddNoise",{"amount","distribution","monochromatic","seed"}},
    {"median",{"radius"}},
    {"dustAndScratches",{"radius","threshold"}},
    {"minimum",{"radius","shape","affectAlpha"}},
    {"maximum",{"radius","shape","affectAlpha"}},
    {"reduceNoise",{"strength","preserveDetails","reduceColorNoise","sharpenDetails","removeJpegArtifact"}},
    {"customFilter",{"kernel","scale","offset"}},
    {"hsbHsl",{"input","output"}},
    {"deInterlace",{"eliminateEven","interpolate"}},
    {"ntscColors",{}},
    {"guidedBlur",{"radius","epsilon"}},
    {"crossGuidedBlur",{"guide","radius","epsilon"}},
    {"liquify",{"cell","strokes"}},
    {"adaptiveWideAngle",{"model","focalLength","cropFactor","scale","constraints","meshCells","solverIterations","interpolation"}},
    {"puppetWarp",{"pins","mode","density","expansion","iterations","solverIterations","interpolation"}},
    {"perspectiveWarp",{"planes","straighten","cell","interpolation"}},
    {"vanishingPoint",{"planes","tearOffs","focalLength","pastes","clones"}},
    {"pointColorRangePreview",{"lightness","chroma","hue","hueShift","saturationShift","luminanceShift","variance","range","hueRange","saturationRange","luminanceRange"}},
    {"maskColorOverlay",{"mask","color","opacity"}},
    {"maskColorOnBwOverlay",{"mask","color","opacity"}},
    {"maskImageOnBlack",{"mask"}},
    {"maskImageOnWhite",{"mask"}},
    {"maskWhiteOnBlack",{"mask"}},
    {"destinationGamutWarning",{"proofSpace","linearRec2020"}},
    {"displayGamutWarning",{"proofSpace","linearRec2020"}},
    {"applyLightcraftLook",{"look","amount"}},
    {"applyLightcraftPreset",{"preset","amount"}},
    {"lightcraftLookColor",{"amount"}},
    {"lightcraftLookNeutral",{"amount"}},
    {"lightcraftLookVivid",{"amount"}},
    {"lightcraftLookLandscape",{"amount"}},
    {"lightcraftLookPortrait",{"amount"}},
    {"lightcraftLookMono",{"amount"}},
    {"lightcraftLookFilmWarmPrint",{"amount"}},
    {"lightcraftLookFilmCoolFade",{"amount"}},
    {"lightcraftLookFilmGoldenHour",{"amount"}},
    {"lightcraftLookFilmFadedSlide",{"amount"}},
    {"lightcraftLookCineTealAmber",{"amount"}},
    {"lightcraftLookCineNightBlue",{"amount"}},
    {"lightcraftLookCineDesertHeat",{"amount"}},
    {"lightcraftLookCineNeonDusk",{"amount"}},
    {"lightcraftLookMutedMatteSoft",{"amount"}},
    {"lightcraftLookMutedBleached",{"amount"}},
    {"lightcraftLookMutedPastelHaze",{"amount"}},
    {"lightcraftLookMutedQuietGreen",{"amount"}},
    {"lightcraftLookBwMonoRich",{"amount"}},
    {"lightcraftLookBwRedFilter",{"amount"}},
    {"lightcraftLookBwSoft",{"amount"}},
    {"lightcraftLookBwSepia",{"amount"}},
    {"lightcraftPresetWarmGlow",{"amount"}},
    {"lightcraftPresetCoolMorning",{"amount"}},
    {"lightcraftPresetTealOrange",{"amount"}},
    {"lightcraftPresetVividPop",{"amount"}},
    {"lightcraftPresetSoftPastel",{"amount"}},
    {"lightcraftPresetFadedMatte",{"amount"}},
    {"lightcraftPresetWarmFilm",{"amount"}},
    {"lightcraftPresetMutedFilm",{"amount"}},
    {"lightcraftPresetBwHighContrast",{"amount"}},
    {"lightcraftPresetBwSoft",{"amount"}},
    {"lightcraftPresetBwSelenium",{"amount"}},
    {"lightcraftPresetCrispLandscape",{"amount"}},
    {"lightcraftPresetGoldenHour",{"amount"}},
    {"lightcraftPresetBlueHour",{"amount"}},
    {"lightcraftPresetSoftSkin",{"amount"}},
    {"lightcraftPresetBrightAiry",{"amount"}},
    {"lightcraftPresetMoody",{"amount"}},
    {"lightcraftPresetCinematic",{"amount"}},
    {"lightcraftPresetMoodyPortrait",{"amount"}},
    {"lightcraftPresetGoldenGlow",{"amount"}},
    {"lightcraftPresetCleanStudio",{"amount"}},
    {"lightcraftPresetCrispVista",{"amount"}},
    {"lightcraftPresetDeepSky",{"amount"}},
    {"lightcraftPresetLushGreens",{"amount"}},
    {"lightcraftPresetDesertWarmth",{"amount"}},
    {"lightcraftPresetMistyMorning",{"amount"}},
    {"lightcraftPresetGrittyStreet",{"amount"}},
    {"lightcraftPresetNeonNight",{"amount"}},
    {"lightcraftPresetConcreteCool",{"amount"}},
    {"lightcraftPresetFadedUrban",{"amount"}},
    {"lightcraftPresetFreshBright",{"amount"}},
    {"lightcraftPresetWarmTable",{"amount"}},
    {"lightcraftPresetDarkMoodyFood",{"amount"}},
    {"lightcraftPresetAutumnGold",{"amount"}},
    {"lightcraftPresetWinterBlue",{"amount"}},
    {"lightcraftPresetSpringFresh",{"amount"}},
    {"lightcraftPresetSummerHaze",{"amount"}},
    {"lightcraftPresetInstant70s",{"amount"}},
    {"lightcraftPresetCrossProcess",{"amount"}},
    {"lightcraftPresetBleachBypass",{"amount"}},
    {"lightcraftPresetBwSepia",{"amount"}},
    {"resizeBox",{"width","height"}},
    {"resizeBilinear",{"width","height"}},
    {"resizeMitchell",{"width","height"}},
    {"resizeLanczos3",{"width","height"}},
    {"develop",{"optics","whiteBalance","detail","effects","calibration","light","color","vignette","curve","grain"}},
    {"lensCorrection",{"profile","correctDistortion","correctVignette","correctCa","distortion","redCyan","blueYellow","vignetteAmount","vignetteMidpoint","vertical","horizontal","angle","scale","edge","edgeColor"}},
    {"correctOptics",{"distortion","vignetting","vignettingMidpoint","redChromaticAberration","blueChromaticAberration","autoChromaticAberration","purpleDefringe","greenDefringe","purpleHueLow","purpleHueHigh","greenHueLow","greenHueHigh","embeddedWarp","embeddedVignette","profileDistortion","profileVignetting"}},
    {"colorHalftone",{"maximumRadius","angles"}},
    {"crystallize",{"cellSize","seed"}},
    {"facet",{}},
    {"fragment",{}},
    {"mezzotint",{"pattern","seed"}},
    {"pointillize",{"cellSize","seed","background"}},
    {"mosaic",{"cellSize"}},
    {"emboss",{"angle","height","amount"}},
    {"findEdges",{}},
    {"solarize",{}},
    {"diffuse",{"mode","seed"}},
    {"extrude",{"shape","size","depth","levelBased","solidFrontFaces","maskIncompleteBlocks","seed"}},
    {"oilPaint",{"stylization","cleanliness","scale","bristleDetail","lighting","angle","shine"}},
    {"tiles",{"count","maximumOffset","fill","foreground","background","seed"}},
    {"traceContour",{"level","upper"}},
    {"wind",{"method","fromRight","seed"}},
    {"linearMask",{"start","end"}},
    {"radialMask",{"center","radiusX","radiusY","angle","feather","invert"}},
    {"luminanceRangeMask",{"low","high","lowFeather","highFeather","exposure"}},
    {"colorRangeMask",{"samples","range","exposure"}},
    {"brushMask",{"points","radius","feather","flow","density","erase","autoMask"}},
    {"invertMask",{"mask"}},
    {"refineMask",{"mask","amount"}},
    {"combineMasks",{"base","operand","operation"}},
    {"autoDevelopLight",{}},
    {"autoWhiteBalance",{"temperature","tint","asShotTemperature","asShotTint"}},
    {"sampledWhiteBalance",{"sample","temperature","tint","asShotTemperature","asShotTint"}},
    {"whiteBalancePreset",{"preset"}},
    {"autoBlackAndWhiteMix",{}},
    {"samplePointColor",{"sample"}},
    {"autoLensScale",{"profile","correctDistortion","correctVignette","correctCa","distortion","redCyan","blueYellow","vignetteAmount","vignetteMidpoint","vertical","horizontal","angle","scale","edge","edgeColor"}},
    {"estimateChromaticAberration",{}},
}; return result; }
} // namespace

QString canonicalOperation(const QString &operation) {
    const auto trimmed=operation.trimmed();
    if(aliases().contains(trimmed)) return aliases().value(trimmed);
    return trimmed;
}
QStringList operations() { auto result=parameterCatalog().keys(); for(auto it=aliases().cbegin();it!=aliases().cend();++it) if(parameterCatalog().contains(it.value())) result.append(it.key()); result.sort(); result.removeDuplicates(); return result; }
QStringList parameterNames(const QString &operation) { return parameterCatalog().value(canonicalOperation(operation)); }
bool supports(const QString &operation) { return parameterCatalog().contains(canonicalOperation(operation)); }
QStringList helpers() {
    return {"autoBlackAndWhiteMix", "autoDevelopLight", "autoLensScale", "autoWhiteBalance", "estimateChromaticAberration", "samplePointColor", "sampledWhiteBalance", "whiteBalancePreset"};
}
bool isHelper(const QString &operation) { return helpers().contains(canonicalOperation(operation)); }
bool helperNeedsImage(const QString &operation) {
    const auto op = canonicalOperation(operation);
    return isHelper(op) && op != "samplePointColor" && op != "sampledWhiteBalance" && op != "whiteBalancePreset";
}
PreparedHelper prepareHelper(const QString &operation, const QVariantMap &parameters) {
    const auto op = canonicalOperation(operation); auto p = normalizedParameters(op, parameters); Reader r;
    const auto bad = [&]() -> PreparedHelper { return {{}, r.error.isEmpty() ? QStringLiteral("Supply valid parameters for this settings calculation.") : r.error}; };
    if (op == "autoDevelopLight" || op == "autoBlackAndWhiteMix" || op == "estimateChromaticAberration") {
        if (!r.keys(p, {})) return bad();
        if (op == "autoDevelopLight") return {graphicHelper::AutoLight{}, {}};
        if (op == "autoBlackAndWhiteMix") return {graphicHelper::AutoMix{}, {}};
        return {graphicHelper::ChromaticAlignment{}, {}};
    }
    if (op == "autoWhiteBalance" || op == "sampledWhiteBalance") {
        auto options = p; options.remove("sample"); WhiteBalanceOptions source;
        if (op == "autoWhiteBalance") { if (!read(r, QVariant(p), source)) return bad(); return {graphicHelper::AutoWhite{source}, {}}; }
        Rgba sample;
        if (!read(r, QVariant(options), source) || !p.contains("sample") || !member(r, p, "sample", sample)) return bad();
        return {graphicHelper::SampleWhite{sample, source}, {}};
    }
    if (op == "whiteBalancePreset") {
        WhiteBalancePreset preset = WhiteBalancePreset::Daylight;
        if (!r.keys(p, {"preset"}) || !p.contains("preset") || !member(r, p, "preset", preset)) return bad();
        return {graphicHelper::WhitePreset{preset}, {}};
    }
    if (op == "samplePointColor") {
        Rgba sample;
        if (!r.keys(p, {"sample"}) || !p.contains("sample") || !member(r, p, "sample", sample)) return bad();
        return {graphicHelper::PointColor{sample}, {}};
    }
    if (op == "autoLensScale") {
        LensCorrectionOptions options; if (!read(r, QVariant(p), options)) return bad();
        return {graphicHelper::LensScale{options}, {}};
    }
    return {{}, "Unsupported settings calculation: " + operation};
}
QVariantMap helperParameters(const graphicHelper::Result &result) {
    QVariantMap parameters;
    for (const auto &[name, value] : result.parameters) parameters[QString::fromStdString(name)] = std::visit([](const auto &input) -> QVariant {
        using T = std::decay_t<decltype(input)>;
        if constexpr (std::is_same_v<T, std::array<double, 8>>) { QVariantList values; for (double value : input) values.append(value); return values; }
        else return QVariant::fromValue(input);
    }, value);
    return parameters;
}
bool usesAdjustmentCollection(const QVariantMap &parameters) {
    return normalizedParameters("applyLocalAdjustments", parameters).contains("adjustments");
}
Result process(const QImage &source,const QString &operation,const QVariantMap &parameters) {
    return process(source, operation, parameters, {});
}
Result process(const QImage &source,const QString &operation,const QVariantMap &parameters,const MaskInputs &masks) {
    const auto op=canonicalOperation(operation);
    if(!supports(op)) return {{},"Unsupported image operation: "+operation};
    try {
        auto image=importImage(source); if(!image) return {{},sdkError(image.error())};
        Reader reader; reader.width=image->width; reader.height=image->height; reader.masks=&masks;
        auto p=normalizedParameters(op,parameters); nativeMaskKeys(p,masks); QVariantMap returned;
        GA::Result<Image> result=std::unexpected(Error{ErrorCode::InvalidParameter,"Unsupported image operation"});
        auto wbSettings=[](const WhiteBalanceOptions &o) { return QVariantMap{{"temperature",o.temperature},{"tint",o.tint},{"asShotTemperature",o.asShotTemperature},{"asShotTint",o.asShotTint}}; };
        if(op=="autoDevelopLight") {
            if(!reader.keys(p,{})) return {{},reader.error};
            auto settings=GA::autoDevelopLight(image->view()); if(!settings) return {{},sdkError(settings.error())};
            returned={{"exposure",settings->exposure},{"contrast",settings->contrast},{"highlights",settings->highlights},{"shadows",settings->shadows},{"whites",settings->whites},{"blacks",settings->blacks}};
            result=GA::developLight(image->view(),*settings);
        } else if(op=="autoWhiteBalance" || op=="sampledWhiteBalance") {
            Rgba sample; auto source=p; source.remove("sample"); WhiteBalanceOptions options;
            if(!read(reader,QVariant(source),options)) return {{},reader.error};
            if(op=="sampledWhiteBalance" && (!p.contains("sample") || !member(reader,p,"sample",sample))) return {{},reader.error.isEmpty() ? "Supply a visible neutral sample" : reader.error};
            auto settings=op=="autoWhiteBalance" ? GA::autoWhiteBalance(image->view(),options) : GA::sampledWhiteBalance(sample,options);
            if(!settings) return {{},sdkError(settings.error())}; returned=wbSettings(*settings); result=GA::whiteBalance(image->view(),*settings);
        } else if(op=="whiteBalancePreset") {
            WhiteBalancePreset preset=WhiteBalancePreset::Daylight;
            if(!reader.keys(p,{"preset"}) || !p.contains("preset") || !member(reader,p,"preset",preset)) return {{},reader.error.isEmpty() ? "Supply a white balance preset" : reader.error};
            auto settings=GA::whiteBalancePreset(preset); if(!settings) return {{},sdkError(settings.error())}; returned=wbSettings(*settings); result=GA::whiteBalance(image->view(),*settings);
        } else if(op=="autoBlackAndWhiteMix") {
            if(!reader.keys(p,{})) return {{},reader.error};
            auto settings=GA::autoBlackAndWhiteMix(image->view()); if(!settings) return {{},sdkError(settings.error())};
            DevelopColorOptions options; options.blackAndWhite=true; options.blackAndWhiteMix=*settings;
            QVariantList values; for(double value:*settings) values.append(value); returned={{"blackAndWhite",true},{"blackAndWhiteMix",values}};
            result=GA::developColor(image->view(),options);
        } else if(op=="samplePointColor") {
            Rgba sample;
            if(!reader.keys(p,{"sample"}) || !p.contains("sample") || !member(reader,p,"sample",sample)) return {{},reader.error.isEmpty() ? "Supply a visible color sample" : reader.error};
            auto point=GA::samplePointColor(sample); if(!point) return {{},sdkError(point.error())};
            returned={{"lightness",point->lightness},{"chroma",point->chroma},{"hue",point->hue},{"hueShift",point->hueShift},{"saturationShift",point->saturationShift},{"luminanceShift",point->luminanceShift},{"variance",point->variance},{"range",point->range},{"hueRange",point->hueRange},{"saturationRange",point->saturationRange},{"luminanceRange",point->luminanceRange}};
            result=*image;
        } else if(op=="autoLensScale") {
            LensCorrectionOptions options; if(!read(reader,QVariant(p),options)) return {{},reader.error};
            auto scale=GA::autoLensScale(image->view(),options); if(!scale) return {{},sdkError(scale.error())}; options.scale=*scale; returned={{"scale",*scale}}; result=GA::lensCorrection(image->view(),options);
        } else if(op=="estimateChromaticAberration") {
            if(!reader.keys(p,{})) return {{},reader.error};
            auto settings=GA::estimateChromaticAberration(image->view()); if(!settings) return {{},sdkError(settings.error())};
            OpticsOptions options; options.redChromaticAberration=(*settings)[0]; options.blueChromaticAberration=(*settings)[1];
            returned={{"redChromaticAberration",options.redChromaticAberration},{"blueChromaticAberration",options.blueChromaticAberration},{"autoChromaticAberration",false}};
            result=GA::correctOptics(image->view(),options);
        } else result=dispatch(reader,image->view(),op,p);
        if(!reader.error.isEmpty()) return {{},reader.error};
        if(!result) return {{},sdkError(result.error())};
        auto words=toArgb32(result->view()); if(!words) return {{},sdkError(words.error())};
        QImage output(int(result->width),int(result->height),QImage::Format_ARGB32);
        if(output.isNull()) return {{},"Could not allocate the output image"};
        for(int y=0;y<output.height();++y) std::copy_n(words->begin()+std::size_t(y)*output.width(),output.width(),reinterpret_cast<std::uint32_t *>(output.scanLine(y)));
        output.setColorSpace(QColorSpace::SRgb);
        return {std::move(output),{},returned};
    } catch(const std::bad_alloc &) { return {{},"Image processing allocation failed"}; }
}
bool maskNeedsPixels(const QString &operation) {
    return QStringList{"brushMask", "luminanceRangeMask", "colorRangeMask", "refineMask"}.contains(canonicalOperation(operation));
}
namespace {
MaskResult calculateMask(QSize extent, const QImage *source, const QString &operation, const QVariantMap &parameters, const MaskInputs &masks) {
    try {
        if (extent.isEmpty()) return {0,0,{},"Supply a nonempty source image"};
        const auto op=canonicalOperation(operation);
        std::optional<Image> image;
        if (maskNeedsPixels(op)) {
            if (!source) return {0,0,{},"This mask operation requires source image pixels"};
            auto imported=importImage(*source); if(!imported) return {0,0,{},sdkError(imported.error())};
            image=std::move(*imported);
        }
        Reader r; r.width=std::size_t(extent.width()); r.height=std::size_t(extent.height()); r.masks=&masks;
        auto p=normalizedParameters(op,parameters); nativeMaskKeys(p,masks);
        GA::Result<Mask> mask=std::unexpected(Error{ErrorCode::InvalidParameter,"Unsupported mask operation"});
        if(op=="linearMask") { LinearMaskOptions o; if(!read(r,QVariant(p),o)) return {0,0,{},r.error}; mask=linearMask(r.width,r.height,o); }
        else if(op=="radialMask") { RadialMaskOptions o; if(!read(r,QVariant(p),o)) return {0,0,{},r.error}; mask=radialMask(r.width,r.height,o); }
        else if(op=="luminanceRangeMask") { LuminanceRangeOptions o; if(!read(r,QVariant(p),o)) return {0,0,{},r.error}; mask=luminanceRangeMask(image->view(),o); }
        else if(op=="colorRangeMask") { ColorRangeOptions o; if(!read(r,QVariant(p),o)) return {0,0,{},r.error}; mask=colorRangeMask(image->view(),o); }
        else if(op=="brushMask") { std::vector<BrushStroke> strokes;
            if(p.contains("strokes")) { if(!r.keys(p,{"strokes"}) || !member(r,p,"strokes",strokes)) return {0,0,{},r.error}; }
            else { BrushStroke stroke; if(!read(r,QVariant(p),stroke)) return {0,0,{},r.error}; strokes.push_back(std::move(stroke)); }
            mask=brushMask(image->view(),strokes); }
        else if(op=="invertMask" || op=="refineMask") {
            MaskView view; double amount=0;
            if(!r.keys(p,op=="refineMask" ? QStringList{"mask","amount"} : QStringList{"mask"}) || !maskMember(r,p,"mask",view) || !p.contains("mask") || !member(r,p,"amount",amount)) return {0,0,{},r.error.isEmpty() ? "Supply a mask" : r.error};
            mask=op=="invertMask" ? invertMask(view) : refineMask(image->view(),view,amount);
        } else if(op=="combineMasks") {
            MaskView base,operand; MaskCombine mode=MaskCombine::Add;
            if(!r.keys(p,{"base","operand","operation"}) || !maskMember(r,p,"base",base) || !maskMember(r,p,"operand",operand) || !member(r,p,"operation",mode) || !p.contains("base") || !p.contains("operand")) return {0,0,{},r.error.isEmpty() ? "Supply both masks" : r.error};
            mask=combineMasks(base,operand,mode);
        }
        if(!mask) return {0,0,{},sdkError(mask.error())};
        return {int(mask->width),int(mask->height),std::move(mask->coverage),{}};
    } catch(const std::bad_alloc &) { return {0,0,{},"Mask processing allocation failed"}; }
}
} // namespace
MaskResult makeMask(const QImage &source,const QString &operation,const QVariantMap &parameters) {
    return makeMask(source, operation, parameters, {});
}
MaskResult makeMask(const QImage &source,const QString &operation,const QVariantMap &parameters,const MaskInputs &masks) {
    return calculateMask(source.size(), source.isNull() ? nullptr : &source, operation, parameters, masks);
}
MaskResult makeMask(QSize extent,const QString &operation,const QVariantMap &parameters,const MaskInputs &masks) {
    return calculateMask(extent, nullptr, operation, parameters, masks);
}
} // namespace EditorGraphicAdjust
