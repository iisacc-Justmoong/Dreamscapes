#pragma once
#include "ViewValue.h"
#include <iiSDKCore/Runtime/Query.h>

namespace dreamscapes::vm {
enum class EditorRoute {
    execute,
    source,
    preview,
    generate,
    capabilities,
    openDocument,
    saveDocumentAs,
    exportImage,
    paintColor
};
struct EditorInvocation {
    std::string tool, field;
    Value values, definition, generativeDraft;
};
struct EditorPlan {
    EditorRoute route = EditorRoute::execute;
    std::string tool, field;
    Value values;
};
struct GenerationPlan {
    std::string operation;
    Value values;
};
inline EditorPlan editorCommand(const EditorInvocation &input) {
    EditorPlan plan{EditorRoute::execute, input.tool, input.field, input.values};
    const auto &t = input.tool, &f = input.field;
    const auto route = [&](EditorRoute route, std::string tool = {}, std::string field = {}) {
        plan.route = route;
        if (!tool.empty())
            plan.tool = std::move(tool);
        if (!field.empty())
            plan.field = std::move(field);
        return plan;
    };
    if (t == "elements" && f == "image-fill")
        return route(EditorRoute::source);
    if (f == "capabilities")
        return route(EditorRoute::capabilities);
    for (const auto &field : array(get(input.definition, "fields")))
        if (text(get(field, "id")) == f && text(get(field, "type")) == "Visual")
            return route(EditorRoute::preview);
    if (t == "file" && f == "field-3")
        return route(EditorRoute::openDocument);
    if (t == "file" && f == "field-6")
        return route(EditorRoute::source);
    if (t == "file" && f == "field-12")
        return route(text(get(input.values, "field-1")) == "IISC" && boolean(get(input.values, "field-11"))
                         ? EditorRoute::saveDocumentAs
                         : EditorRoute::exportImage);
    if ((t == "camera-photo" && f == "field-7") || (t == "audio-track" && f == "field-3"))
        return route(EditorRoute::source);
    if (t == "layers" && (f == "field-11" || f == "field-15"))
        return route(EditorRoute::preview);
    if (t == "brush" && f == "field-20")
        return route(EditorRoute::paintColor);
    if (t == "brush" && f == "field-3")
        return route(EditorRoute::preview, {}, "field-2");
    if (t == "retouch" && f == "field-18")
        return route(EditorRoute::preview);
    if ((t == "generative" && (f == "field-3" || f == "field-7" || f == "field-10" || f == "field-14")) ||
        (t == "background" && f == "field-15" && text(get(input.values, "field-13")) == "Generate") ||
        (t == "fill" && f == "field-15") ||
        (t == "retouch" && (f == "field-7" || (f == "field-3" && text(get(input.values, "field-2")) == "Generate"))) ||
        (t == "eraser" && f == "field-7" && text(get(input.values, "field-6")) == "Generate"))
        return route(EditorRoute::generate);
    if (t == "background" && f == "field-15")
        return route(EditorRoute::source);
    if (t == "generative" && f == "field-22")
        return route(EditorRoute::source, "reference");
    if (t == "layers" && f == "field-3") {
        plan.values = input.generativeDraft;
        return route(EditorRoute::generate, "generative", "field-7");
    }
    return plan;
}
inline GenerationPlan editorGeneration(const EditorInvocation &input) {
    const auto &t = input.tool, &f = input.field;
    auto recipe = input.values;
    std::string operation = f == "field-14"                                      ? "Outpaint"
                            : f == "field-10" || t == "retouch" || t == "eraser" ? "Inpaint"
                            : f == "field-7"                                     ? "Image-to-image"
                                             : text(get(input.values, "selector"), "Text-to-image");
    if (t == "background") {
        operation = "Background";
        const auto prompt = text(get(recipe, "replacePrompt"));
        const auto model = text(get(recipe, "replaceModel"));
        recipe = merged(
            recipe,
            Value(Object{{"field-0", prompt.empty() ? get(recipe, "field-12") : Value(prompt)},
                         {"field-2", Value(model.empty() || model == "Current generation model" ? "Auto" : model)},
                         {"field-5", has(recipe, "replaceStrength") ? get(recipe, "replaceStrength") : Value(60)}}));
    }
    if (t == "fill") {
        operation = "Fill";
        recipe = Value(Object{{"field-0", get(input.values, "field-12")}, {"field-5", get(input.values, "field-13")}});
    }
    if (t == "retouch")
        recipe = Value(Object{{"field-0", get(input.values, "field-5")}, {"field-5", get(input.values, "field-6")}});
    if (t == "eraser")
        recipe = input.generativeDraft;
    return {std::move(operation), std::move(recipe)};
}
class EditorCommandRouter final {
  public:
    EditorCommandRouter()
        : routes_([](const EditorInvocation &input) { return editorCommand(input); }),
          generation_([](const EditorInvocation &input) { return editorGeneration(input); }) {}
    const ii::runtime::QueryPort<EditorInvocation, EditorPlan> &routes() const {
        return routes_;
    }
    const ii::runtime::QueryPort<EditorInvocation, GenerationPlan> &generation() const {
        return generation_;
    }

  private:
    ii::runtime::QueryPort<EditorInvocation, EditorPlan> routes_;
    ii::runtime::QueryPort<EditorInvocation, GenerationPlan> generation_;
};
} // namespace dreamscapes::vm
