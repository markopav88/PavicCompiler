#pragma once

#include "diagnostic.hpp"

namespace pavic {

class AstProgram;
class SourceMap;
struct Token;

/// Type-checks one program AST after scope-check succeeded (Step 3).
/// Emits `[semantic] types: …` when `verbose` is true.
/// Returns true iff this pass added no new errors to `diagnostics`.
bool runTypeCheck(
    const AstProgram& ast,
    const SourceMap& map,
    const std::vector<Token>& tokens,
    DiagnosticBag& diagnostics,
    bool verbose
);

} // namespace pavic
