#pragma once

namespace pavic {

class AstProgram;

/// Rewrites AST in-place with conservative, semantics-preserving optimizations:
/// - constant folding
/// - constant propagation
/// - dead code elimination (constant-condition if/while)
/// - bounded loop unrolling for simple canonical counting loops
void optimizeAstProgram(AstProgram& ast, bool verbose);

} // namespace pavic
