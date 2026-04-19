#pragma once

#include "diagnostic.hpp"

namespace pavic {

class AstProgram;
class SourceMap;
struct Token;

/// Step 4 (part 1): warnings/hints for unused names, uninitialized reads, assign-but-never-read.
/// Runs only when scope and type already succeeded for this program. Does not add errors.
void runUsageAndInitHints(
    const AstProgram& ast,
    const SourceMap& map,
    const std::vector<Token>& tokens,
    DiagnosticBag& diagnostics,
    bool verbose
);

} // namespace pavic
