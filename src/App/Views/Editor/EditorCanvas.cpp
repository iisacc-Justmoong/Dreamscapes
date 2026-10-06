#include "EditorCanvas.h"
#include "App/Views/Home/CanvasPresets.h"

bool EditorCanvas::createCanvas(const QVariantMap &specification) {
    CanvasPresets validator;
    const auto checked = validator.specification(specification.value("width").toDouble(),
        specification.value("height").toDouble(), specification.value("unit").toString(),
        specification.value("ppi", 300).toDouble(), specification.value("background", "White").toString());
    if (!checked.value("valid").toBool()) return fail(tr("Enter valid canvas dimensions, units and resolution."));
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
        canvas.layers.emplace_back(StaticVectorLayer{
            {"canvas.background.layer", "Background", true, 1.0, {}, RasterBlendMode::SourceOver},
            StaticSource{"canvas.background"}});
    }
    // The sparse SDK renderer renders visible tiles, not a giant initial bitmap.
    auto resolved = specification;
    for (auto it = checked.cbegin(); it != checked.cend(); ++it) resolved.insert(it.key(), it.value());
    return adopt(std::move(canvas), resolved);
}
