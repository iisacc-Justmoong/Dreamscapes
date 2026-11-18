#pragma once

#include <iiGUI>
#include <string>
#include <string_view>

inline ii::core::Result<ii::gui::Widget> failureReportBranch(const ii::gui::Widget &modal,std::string_view child) {
    // LVUI owns generated conditional IDs; their ordinal is not a view contract.
    for (int index=0;index<128;++index) {
        auto branch=modal.popup_content("@lvui/conditional/"+std::to_string(index));
        if (branch && branch->content_item(child)) return *branch;
    }
    return ii::core::fail(ii::core::ErrorCode::not_found,"Report editor branch was not found");
}
