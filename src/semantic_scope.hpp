#pragma once

#include "symbol_table.hpp"

#include <vector>

namespace pavic {

class AstProgram;
class SourceMap;
struct Token;

/// Walks the AST: builds `ScopeStack`, records declarations, checks uses (Step 2 — no type rules yet).
/// Fills `outSymbols` with declaration order (for printing when the pass adds no new errors).
/// Returns true iff this pass did not add any new errors to `diagnostics`.
bool runScopeCheck(
    const AstProgram& ast,
    const SourceMap& map,
    const std::vector<Token>& tokens,
    DiagnosticBag& diagnostics,
    bool verbose,
    std::vector<SymbolRecord>& outSymbols
);

} // namespace pavic
