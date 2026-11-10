#include "NativeFailureDetailsView.h"
#include "NativeFailureDetails.generated.h"

namespace dreamscapes::failure {
ii::core::Result<void> buildFailureDetails(ii::gui::WidgetBuilder &widgets, ii::gui::Widget parent,
                                           std::shared_ptr<FailureDetailsModel> model) {
    if (!model) return ii::core::fail(ii::core::ErrorCode::invalid_argument, "Failure details model is required");
    const auto factories = lvui::native::BackendFactories{}.with<NativeFailureDetailsBackend>([model] {
        return NativeFailureDetailsBackend(model);
    });
    return lvui_generated::NativeFailureDetails::build(widgets, parent, {}, nullptr, factories);
}
} // namespace dreamscapes::failure
