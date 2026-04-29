#include "codegen/codegen.hpp"

#include "ast.hpp"
#include "codegen/code_buffer.hpp"
#include "codegen/memory_layout.hpp"
#include "diagnostic.hpp"
#include "source.hpp"
#include "symbol_table.hpp"
#include "token.hpp"

#include <iostream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>

namespace pavic {

namespace {

constexpr std::uint8_t kOpBrk = 0x00;
constexpr std::uint8_t kOpNop = 0xEA;
constexpr std::uint8_t kOpSys = 0xFF;
constexpr std::uint8_t kOpLdaImm = 0xA9;
constexpr std::uint8_t kOpLdaAbs = 0xAD;
constexpr std::uint8_t kOpStaAbs = 0x8D;
constexpr std::uint8_t kOpAdcAbs = 0x6D;
constexpr std::uint8_t kOpLdxImm = 0xA2;
constexpr std::uint8_t kOpTay = 0xA8;
constexpr std::uint8_t kOpClc = 0x18;

SourceLocation locationAtSpan(const SourceMap& map, const std::vector<Token>& tokens, AstSpan span) {
    std::size_t byteOffset = 0;
    if (!tokens.empty() && span.begin < tokens.size()) {
        byteOffset = tokens[span.begin].offset;
    }
    return map.locationAt(byteOffset);
}

void codegenTrace(bool verbose, const SourceMap& map, const std::vector<Token>& tokens, AstSpan span, const std::string& message) {
    if (!verbose) {
        return;
    }
    const SourceLocation loc = locationAtSpan(map, tokens, span);
    std::cout << "[codegen] " << loc.line << ":" << loc.column << ": " << message << "\n";
}

class Generator6502 {
public:
    Generator6502(
        const SourceMap& map,
        const std::vector<Token>& tokens,
        DiagnosticBag& diagnostics,
        bool verbose,
        codegen::MemoryLayout& layout,
        std::uint16_t scratchAddr,
        const codegen::CodeGenTarget& target,
        codegen::CodeBuffer& buffer
    )
        : map_(map),
          tokens_(tokens),
          diagnostics_(diagnostics),
          verbose_(verbose),
          layout_(layout),
          scratchAddr_(scratchAddr),
          target_(target),
          buffer_(buffer) {}

    void visitProgram(AstProgram& program) {
        codegenTrace(verbose_, map_, tokens_, program.span(), "enter Program (codegen)");
        visitBlock(*program.block());
        codegenTrace(verbose_, map_, tokens_, program.span(), "leave Program (codegen)");
    }

private:
    void codegenError(AstSpan span, std::string message, std::string hint) {
        diagnostics_.addError(std::move(message), locationAtSpan(map_, tokens_, span), std::move(hint));
    }

    void visitBlock(AstBlock& block) {
        codegenTrace(verbose_, map_, tokens_, block.span(), "enter Block (codegen)");
        if (block.statements()) {
            for (auto& st : block.statements()->statements()) {
                if (st) {
                    visitStatement(*st);
                }
            }
        }
        codegenTrace(verbose_, map_, tokens_, block.span(), "leave Block (codegen)");
    }

    void visitStatement(AstStatement& st) {
        switch (st.nodeKind()) {
        case AstNodeKind::PrintStatement:
            visitPrint(static_cast<AstPrintStatement&>(st));
            return;
        case AstNodeKind::AssignStatement:
            visitAssign(static_cast<AstAssignStatement&>(st));
            return;
        case AstNodeKind::VarDeclStatement:
            visitVarDecl(static_cast<AstVarDeclStatement&>(st));
            return;
        case AstNodeKind::WhileStatement:
            codegenError(
                st.span(),
                "codegen: `while` is not implemented yet for 6502 output",
                "Use straight-line code for now, or extend the code generator with branches."
            );
            return;
        case AstNodeKind::IfStatement:
            codegenError(
                st.span(),
                "codegen: `if` is not implemented yet for 6502 output",
                "Use straight-line code for now, or extend the code generator with branches."
            );
            return;
        case AstNodeKind::BlockStatement:
            visitBlock(*static_cast<AstBlockStatement&>(st).block());
            return;
        default:
            break;
        }
        throw std::logic_error("codegen visitStatement: unexpected AST statement kind");
    }

    void visitVarDecl(const AstVarDeclStatement& st) {
        (void)st;
        // Storage was reserved from the symbol table in `bindLayout`; no init bytes yet.
        codegenTrace(verbose_, map_, tokens_, st.span(), "VarDecl (no code emitted; RAM already reserved)");
    }

    void visitPrint(AstPrintStatement& st) {
        codegenTrace(verbose_, map_, tokens_, st.span(), "PrintStatement (emit expr → Y, syscall int)");
        if (!emitIntExprToA(*st.expr())) {
            return;
        }
        buffer_.emitU8(kOpTay);
        buffer_.emitU8(kOpLdxImm);
        buffer_.emitU8(0x01);
        if (target_.sysEncoding == codegen::SysCallEncoding::EmulatorNopEA) {
            buffer_.emitU8(kOpNop);
        } else {
            buffer_.emitU8(kOpSys);
        }
    }

    void visitAssign(AstAssignStatement& st) {
        codegenTrace(verbose_, map_, tokens_, st.span(), "AssignStatement");
        if (!st.hasLhsResolvedDeclScope()) {
            codegenError(st.span(), "codegen: internal error (assignment LHS missing resolved scope)", "Re-run scope analysis.");
            return;
        }
        if (!emitIntExprToA(*st.expr())) {
            return;
        }
        const codegen::VarKey key{st.name(), st.lhsResolvedDeclScopeId()};
        const std::uint16_t addr = layout_.addressOf(key);
        buffer_.emitU8(kOpStaAbs);
        buffer_.emitAddr16LE(addr);
    }

    /// Emits code so the low 8 bits of the result end up in **A** (subset: int literals, int vars, `+`).
    bool emitIntExprToA(AstExpr& e) {
        switch (e.nodeKind()) {
        case AstNodeKind::LiteralInt: {
            const auto& lit = static_cast<const AstLiteralInt&>(e);
            int value = 0;
            try {
                value = std::stoi(lit.lexeme());
            } catch (...) {
                codegenError(e.span(), "codegen: invalid integer literal", "Use a decimal literal in range 0–255 for this milestone.");
                return false;
            }
            if (value < 0 || value > 255) {
                codegenError(e.span(), "codegen: integer literal out of range for 8-bit immediate LDA", "Use values between 0 and 255, or extend the generator.");
                return false;
            }
            buffer_.emitU8(kOpLdaImm);
            buffer_.emitU8(static_cast<std::uint8_t>(value));
            return true;
        }
        case AstNodeKind::IdentifierExpr: {
            auto& id = static_cast<AstIdentifierExpr&>(e);
            if (!id.hasResolvedDeclScope()) {
                codegenError(e.span(), "codegen: internal error (identifier missing resolved scope)", "Re-run scope analysis.");
                return false;
            }
            const codegen::VarKey key{id.name(), id.resolvedDeclScopeId()};
            const std::uint16_t addr = layout_.addressOf(key);
            buffer_.emitU8(kOpLdaAbs);
            buffer_.emitAddr16LE(addr);
            return true;
        }
        case AstNodeKind::AddExpr: {
            auto& a = static_cast<AstAddExpr&>(e);
            if (!emitIntExprToA(*a.left())) {
                return false;
            }
            buffer_.emitU8(kOpStaAbs);
            buffer_.emitAddr16LE(scratchAddr_);
            if (!emitIntExprToA(*a.right())) {
                return false;
            }
            buffer_.emitU8(kOpClc);
            buffer_.emitU8(kOpAdcAbs);
            buffer_.emitAddr16LE(scratchAddr_);
            return true;
        }
        case AstNodeKind::LiteralString:
            codegenError(
                e.span(),
                "codegen: string expressions are not implemented yet",
                "Only `int` expressions are lowered for this milestone."
            );
            return false;
        case AstNodeKind::LiteralBool:
            codegenError(e.span(), "codegen: boolean literals are not implemented yet", "Use `int` expressions only.");
            return false;
        case AstNodeKind::BooleanExprWrapper:
            codegenError(
                e.span(),
                "codegen: boolean / relational expressions are not implemented yet",
                "Use arithmetic on `int` values for this milestone."
            );
            return false;
        default:
            break;
        }
        codegenError(e.span(), "codegen: unsupported expression form for 6502 lowering", "Extend `emitIntExprToA` for this node kind.");
        return false;
    }

    const SourceMap& map_;
    const std::vector<Token>& tokens_;
    DiagnosticBag& diagnostics_;
    bool verbose_;
    codegen::MemoryLayout& layout_;
    std::uint16_t scratchAddr_;
    const codegen::CodeGenTarget& target_;
    codegen::CodeBuffer& buffer_;
};

bool bindLayoutFromSymbols(const std::vector<SymbolRecord>& symbols, codegen::MemoryLayout& layout, DiagnosticBag& diagnostics) {
    bool ok = true;
    for (const SymbolRecord& sym : symbols) {
        if (sym.declType == "int") {
            layout.varSlot(codegen::VarKey{sym.name, sym.scopeId}, 1);
        } else {
            diagnostics.addError(
                "codegen: variable type `" + sym.declType + "` is not supported for 6502 output yet",
                sym.declaredAt,
                "Only `int` storage is implemented; use `int` variables or extend the generator."
            );
            ok = false;
        }
    }
    return ok;
}

} // namespace

bool generate6502Program(
    AstProgram& program,
    const SourceMap& map,
    const std::vector<Token>& tokens,
    const std::vector<SymbolRecord>& symbols,
    const codegen::CodeGenTarget& target,
    DiagnosticBag& diagnostics,
    bool verbose,
    std::vector<std::uint8_t>& outObject
) {
    const std::size_t errorsBefore = diagnostics.errorCount();
    outObject.clear();

    codegen::MemoryLayout layout(target.ramBase);
    if (!bindLayoutFromSymbols(symbols, layout, diagnostics)) {
        return diagnostics.errorCount() == errorsBefore;
    }

    const std::uint16_t scratchAddr = layout.allocateAnonymous(1);
    codegen::CodeBuffer buffer;
    Generator6502 gen(map, tokens, diagnostics, verbose, layout, scratchAddr, target, buffer);
    gen.visitProgram(program);
    buffer.emitU8(kOpBrk);

    outObject = buffer.bytes();
    return diagnostics.errorCount() == errorsBefore;
}

} // namespace pavic
