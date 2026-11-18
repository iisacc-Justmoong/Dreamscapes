#include "NativeGenerationRuntime.h"
#include <iiSDKCore>
#include <algorithm>
#include <cctype>

namespace dreamscapes::native {
ii::core::Result<std::vector<std::string>> findGenerationWorker(const GenerationWorkerPaths &paths) {
    using ii::core::ErrorCode;
    namespace fs=std::filesystem;
    try {
    const auto available=[&](const fs::path &path,bool executable) {
        if (!path.is_absolute() || path!=path.lexically_normal()) return false;
        std::error_code error;const auto status=fs::status(path,error);
        if (error || !fs::is_regular_file(status)) return false;
        constexpr auto bits=fs::perms::owner_exec|fs::perms::group_exec|fs::perms::others_exec;
        return !executable || paths.windows || (status.permissions()&bits)!=fs::perms::none;
    };
    auto worker=paths.explicitExecutable;
    if (worker.empty() && (paths.requireConfiguredExecutable || available(paths.configuredExecutable,true)))
        worker=paths.configuredExecutable;
    if (worker.empty()) {
        if (!paths.sdkRoot.is_absolute() || paths.sdkRoot!=paths.sdkRoot.lexically_normal())
            return ii::core::fail(ErrorCode::invalid_argument,"The iiLocalDiffusion SDK location is unavailable");
        if (paths.windows && available(paths.sdkRoot/"bin"/"iild-generate.exe",true))
            worker=paths.sdkRoot/"bin"/"iild-generate.exe";
        else worker=paths.sdkRoot/"bin"/"iild-generate";
    }
    if (!available(worker,true)) return ii::core::fail(ErrorCode::failed,"The configured iiLocalDiffusion worker is unavailable or not executable");
    auto encoded=ii::core::path_to_utf8(worker);
    if (!encoded) return ii::core::fail(ErrorCode::invalid_argument,"The worker path is not valid UTF-8");
    auto extension=worker.extension().string();
    std::ranges::transform(extension,extension.begin(),[](unsigned char c){return char(std::tolower(c));});
    if (!paths.windows || extension==".exe") return std::vector<std::string>{std::move(*encoded)};
    if (!available(paths.python,true))
        return ii::core::fail(ErrorCode::failed,"The Windows worker requires an absolute IILD_PYTHON_EXECUTABLE");
    auto python=ii::core::path_to_utf8(paths.python);
    if (!python) return ii::core::fail(ErrorCode::invalid_argument,"The worker interpreter path is not valid UTF-8");
    return std::vector<std::string>{std::move(*python),std::move(*encoded)};
    } catch (...) {
        return ii::core::fail(ErrorCode::failed,"Cannot inspect the iiLocalDiffusion worker",std::current_exception());
    }
}
ii::core::Result<void> discoverGenerationRuntime(ii::runtime::EventLoop &loop,
    std::function<bool(ii::runtime::Task)> submit,GenerationWorkerPaths paths,std::filesystem::path stagingRoot,
    std::function<void(ii::core::Result<GenerationRuntimeDiscovery>)> completion) {
    using ii::core::ErrorCode;
    if (!submit || !completion) return ii::core::fail(ErrorCode::invalid_argument,"Generation discovery requires an executor and completion");
    const auto sender=loop.sender();
    try {
        if (!submit([sender,paths=std::move(paths),stagingRoot=std::move(stagingRoot),completion=std::move(completion)]() mutable {
            auto discover=[&]() -> ii::core::Result<GenerationRuntimeDiscovery> {
                try {
                    auto command=findGenerationWorker(paths);
                    if (!command) return std::unexpected(command.error());
                    std::error_code error;
                    if (stagingRoot.empty()) stagingRoot=std::filesystem::temp_directory_path(error);
                    if (error || !stagingRoot.is_absolute())
                        return ii::core::fail(ErrorCode::failed,"The generation staging location is unavailable");
                    auto canonical=std::filesystem::canonical(stagingRoot,error);
                    if (error || !std::filesystem::is_directory(canonical,error) || error)
                        return ii::core::fail(ErrorCode::failed,"The generation staging location is unavailable");
                    return GenerationRuntimeDiscovery{std::move(*command),std::move(canonical)};
                } catch (...) {
                    return ii::core::fail(ErrorCode::failed,"Cannot discover the generation runtime",std::current_exception());
                }
            };
            auto result=discover();
            (void)sender.post([completion=std::move(completion),result=std::move(result)]() mutable {
                completion(std::move(result));
            });
        })) return ii::core::fail(ErrorCode::busy,"Cannot schedule generation runtime discovery");
        return {};
    } catch (...) {
        return ii::core::fail(ErrorCode::failed,"Cannot schedule generation runtime discovery",std::current_exception());
    }
}
}
