#include "CTestSupport.h"
#include <sstream>

int main() {
    int calls = 0;
    dreamscapes_ctest::Runner group("rows");
    group.run("rows:first", [&] { ++calls; });
    group.run("rows:second", [&] { ++calls; });
    group.run("other", [&] { ++calls; });
    if (calls != 2 || group.result() != 0) return 1;
    dreamscapes_ctest::Runner row("rows:second");
    row.run("rows:first", [&] { ++calls; });
    row.run("rows:second", [&] { ++calls; });
    if (calls != 3 || row.result() != 0) return 1;
    std::ostringstream errors;
    auto* prior = std::cerr.rdbuf(errors.rdbuf());
    dreamscapes_ctest::Runner missing("unknown");
    missing.run("case", [&] { ++calls; });
    const auto missingResult = missing.result();
    dreamscapes_ctest::Runner failed;
    failed.run("failed", [] { CTEST_CHECK(false); });
    const auto failedResult = failed.result();
    std::cerr.rdbuf(prior);
    return (calls == 3 && missingResult != 0 && failedResult != 0) ? 0 : 1;
}
