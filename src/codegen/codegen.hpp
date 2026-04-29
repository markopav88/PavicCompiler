#pragma once

#include "codegen/target.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace pavic {

class AstProgram;
class SourceMap;
struct Token;
struct SymbolRecord;
class DiagnosticBag;

/// Walks `program` in the same block/statement order as scope/type passes and appends 6502a bytes to
/// `outObject`. Uses `symbols` to reserve RAM for declarations (`int` only for now).
/// Returns true iff no new errors were added to `diagnostics`.
bool generate6502Program(
    AstProgram& program,
    const SourceMap& map,
    const std::vector<Token>& tokens,
    const std::vector<SymbolRecord>& symbols,
    const codegen::CodeGenTarget& target,
    DiagnosticBag& diagnostics,
    bool verbose,
    std::vector<std::uint8_t>& outObject
);

} // namespace pavic
