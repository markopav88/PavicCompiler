#pragma once

#include <memory>
#include <vector>

namespace pavic {

class AstProgram;
class CstProgram;
class SourceMap;
struct Token;

/// Walks the CST and builds an equivalent AST (Step 1 of semantic analysis).
/// Emits `[semantic] …` lines when `verbose` is true (matches lexer/parser `-q` policy).
std::unique_ptr<AstProgram> lowerCstToAst(
    const SourceMap& map,
    const std::vector<Token>& tokens,
    const CstProgram& cst,
    bool verbose
);

} // namespace pavic
