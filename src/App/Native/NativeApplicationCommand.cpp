#include "NativeApplicationCommand.h"
#include "NativeMigratedView.h"
#include <utility>

namespace dreamscapes::native {
std::optional<ApplicationCommandResult> applicationCommand(std::span<const std::string_view> args) {
    if (args.empty()) return {};
    // Finder may supply its process serial number to an app bundle.
    if (args.size()==1 && args[0].starts_with("-psn_")) return {};
    if (args.size()==1 && args[0]=="--help")
        return ApplicationCommandResult{0,"Dreamscapes\n  --list-views       List the compiled LVUI replacement views\n  --view KEY|PATH    Open a native view, for example image or an LVUI path\n  --iisaccson-stdin  Request the Society daemon's resident model (desktop)\n  --help             Show this help\n"};
    if (args.size()==2 && args[0]=="--view" && !args[1].empty())
        return ApplicationCommandResult{0,{},std::string(args[1])};
    if (args.size()==1 && args[0]=="--list-views") {
        std::string output;
        for (const auto &view : migratedViews()) {output.append(view.replacement); output.push_back('\n');}
        return ApplicationCommandResult{0,std::move(output)};
    }
    return ApplicationCommandResult{2,"Unknown command. Use --help.\n"};
}
}
