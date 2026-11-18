#include "NativeGenerationStaging.h"
#include <iiSDKCore>

namespace dreamscapes::native::detail {
GenerationStaging::GenerationStaging(const std::filesystem::path &root):directory(root),path(directory.path()) {}
GenerationStaging::~GenerationStaging() {if (preserve) directory.release();}
void GenerationStaging::workerFinished(generation::QueueCompletion &result) noexcept {
    try {
        std::error_code error;
        for (std::filesystem::directory_iterator it(path,error),end;!error && it!=end;it.increment(error))
            if (it->path().filename()!="cache") {preserve=true;break;}
        if (error) preserve=true;
        if (preserve && result.state!=generation::QueueState::Completed &&
            result.error.find("Recoverable generation files:")==std::string::npos)
            if (auto value=ii::core::path_to_utf8(path)) {
                if (!result.error.empty()) result.error+='\n';
                result.error+="Recoverable generation files: "+*value;
            }
    } catch (...) {preserve=true;}
}
}
