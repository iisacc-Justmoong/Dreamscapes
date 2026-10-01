#include "EditorCanvas.h"
#include "App/Views/Home/CanvasPresets.h"

bool EditorCanvas::createCanvas(const QVariantMap &specification) {
    CanvasPresets validator;
    const auto checked = validator.specification(specification.value("width").toDouble(),
        specification.value("height").toDouble(), specification.value("unit").toString(),
        specification.value("ppi", 300).toDouble(), specification.value("background", "White").toString());
    if (!checked.value("valid").toBool()) return false;
    using namespace iiSharedCanvas;
    Document canvas;
    canvas.extent = {checked.value("pixelWidth").toInt(), checked.value("pixelHeight").toInt()};
    const auto background = checked.value("background").toString();
    if (background != "Transparent") {
        const double width = canvas.extent.width, height = canvas.extent.height;
        VectorPath path;
        path.commands = {MoveTo{{0, 0}}, LineTo{{width, 0}}, LineTo{{width, height}},
                         LineTo{{0, height}}, ClosePath{}};
        path.fill = SolidPaint{background == "Black" ? 0xff000000U : 0xffffffffU};
        canvas.assets.emplace_back(VectorAsset{"canvas.background", canvas.extent, {path}});
        canvas.layers.emplace_back(VectorLayer{
            {"canvas.background.layer", "Background", true, 1.0, {}, RasterBlendMode::SourceOver},
            StaticSource{"canvas.background"}});
    }
    // The sparse SDK renderer renders visible tiles, not a giant initial bitmap.
    unbind();
    m_canvas = std::move(canvas);
    if (!bind(m_canvas)) return false;
    m_specification = specification;
    for (auto it = checked.cbegin(); it != checked.cend(); ++it) m_specification.insert(it.key(), it.value());
    emit specificationChanged();
    fitToView();
    return true;
}
