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
#include <vector>

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
constexpr std::uint8_t kOpSec = 0x38;
constexpr std::uint8_t kOpSbcAbs = 0xED;
constexpr std::uint8_t kOpBeq = 0xF0;
constexpr std::uint8_t kOpBne = 0xD0;
constexpr std::uint8_t kOpJmpAbs = 0x4C;
constexpr std::uint8_t kOpLdyImm = 0xA0;
constexpr std::uint8_t kOpLdyAbs = 0xAC;

enum class ExprTarget {
    Accumulator,
    RegisterY,
};

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

void patchBranchRel8(
    codegen::CodeBuffer& buffer,
    std::size_t branchOpcodeOffset,
    std::size_t branchTargetOffset,
    DiagnosticBag& diagnostics,
    const SourceLocation& errLoc
) {
    const std::size_t nextPc = branchOpcodeOffset + 2;
    const int delta = static_cast<int>(branchTargetOffset) - static_cast<int>(nextPc);
    if (delta < -128 || delta > 127) {
        diagnostics.addError(
            "codegen: branch displacement out of range (-128..127) for this expression",
            errLoc,
            "Simplify the expression or add a linker step for long branches."
        );
        return;
    }
    buffer.patchU8(branchOpcodeOffset + 1, static_cast<std::uint8_t>(delta & 0xFF));
}

class Generator6502 {
public:
    Generator6502(
        const SourceMap& map,
        const std::vector<Token>& tokens,
        DiagnosticBag& diagnostics,
        bool verbose,
        codegen::MemoryLayout& layout,
        const codegen::CodeGenTarget& target,
        codegen::CodeBuffer& buffer
    )
        : map_(map),
          tokens_(tokens),
          diagnostics_(diagnostics),
          verbose_(verbose),
          layout_(layout),
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
            visitWhileStatement(static_cast<AstWhileStatement&>(st));
            return;
        case AstNodeKind::IfStatement:
            visitIfStatement(static_cast<AstIfStatement&>(st));
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
        codegenTrace(verbose_, map_, tokens_, st.span(), "VarDecl (no code emitted; RAM already reserved)");
    }

    void visitPrint(AstPrintStatement& st) {
        AstExpr* expr = st.expr();
        if (expr->nodeKind() == AstNodeKind::LiteralString) {
            emitPrintStringLiteral(static_cast<AstLiteralString&>(*expr), st.span());
            return;
        }

        codegenTrace(verbose_, map_, tokens_, st.span(), "PrintStatement (int: expr → Y, syscall #1)");
        if (!emitIntExpr(*expr, ExprTarget::RegisterY)) {
            return;
        }
        emitSysCallIntPrint();
    }

    void emitSysCallIntPrint() {
        buffer_.emitU8(kOpLdxImm);
        buffer_.emitU8(0x01);
        if (target_.sysEncoding == codegen::SysCallEncoding::EmulatorNopEA) {
            buffer_.emitU8(kOpNop);
        } else {
            buffer_.emitU8(kOpSys);
        }
    }

    void emitSysCallStringPrint() {
        buffer_.emitU8(kOpLdxImm);
        buffer_.emitU8(0x02);
        if (target_.sysEncoding == codegen::SysCallEncoding::EmulatorNopEA) {
            buffer_.emitU8(kOpNop);
        } else {
            buffer_.emitU8(kOpSys);
        }
    }

    /// String literal: emit `LDA #byte` / `STA abs` for each character + trailing `$00`, then syscall `#02`
    /// with **Y = low 8 bits of base** (assumes high byte is `$00`; fails if `base >= 256`).
    void emitPrintStringLiteral(AstLiteralString& lit, AstSpan printSpan) {
        const std::string& lex = lit.lexeme();
        const std::size_t n = lex.size() + 1;
        if (n > 0xFFFF) {
            codegenError(printSpan, "codegen: string literal is too long", "Shorten the string literal.");
            return;
        }
        const std::uint16_t base = layout_.allocateAnonymous(n);
        if (base >= 256) {
            codegenError(
                printSpan,
                "codegen: string literal would reside at address >= $0100; this emitter only supports `LDY #imm` low-byte pointers for syscall #2",
                "Use a smaller program (fewer variables/temporaries) so the string pool stays in zero page ($00–$FF), or extend the generator with a 16-bit pointer convention."
            );
            return;
        }
        codegenTrace(
            verbose_,
            map_,
            tokens_,
            lit.span(),
            "PrintStatement (string: emit RAM init at $" + hexAddr(base) + " then syscall #2)"
        );
        for (std::size_t i = 0; i < lex.size(); ++i) {
            buffer_.emitU8(kOpLdaImm);
            buffer_.emitU8(static_cast<std::uint8_t>(static_cast<unsigned char>(lex[i])));
            buffer_.emitU8(kOpStaAbs);
            buffer_.emitAddr16LE(static_cast<std::uint16_t>(base + static_cast<std::uint16_t>(i)));
        }
        buffer_.emitU8(kOpLdaImm);
        buffer_.emitU8(0x00);
        buffer_.emitU8(kOpStaAbs);
        buffer_.emitAddr16LE(static_cast<std::uint16_t>(base + static_cast<std::uint16_t>(lex.size())));

        buffer_.emitU8(kOpLdyImm);
        buffer_.emitU8(static_cast<std::uint8_t>(base & 0xFF));
        emitSysCallStringPrint();
    }

    void visitWhileStatement(AstWhileStatement& w) {
        const SourceLocation errLoc = locationAtSpan(map_, tokens_, w.span());
        const std::size_t lTop = buffer_.size();
        codegenTrace(
            verbose_,
            map_,
            tokens_,
            w.span(),
            "While: L_top at PC " + std::to_string(lTop) + " (emit condition → A as 0/1)"
        );
        if (!emitBooleanExprAsIntInA(*w.condition())) {
            return;
        }
        const std::size_t bneOpcode = buffer_.size();
        buffer_.emitU8(kOpBne);
        buffer_.emitU8(0x00);
        buffer_.emitU8(kOpJmpAbs);
        const std::size_t jmpExitAddrOperand = buffer_.size();
        buffer_.emitAddr16LE(0x0000);
        const std::size_t lBody = buffer_.size();
        patchBranchRel8(buffer_, bneOpcode, lBody, diagnostics_, errLoc);
        codegenTrace(verbose_, map_, tokens_, w.span(), "While: L_body at PC " + std::to_string(lBody));
        visitBlock(*w.body());
        codegenTrace(verbose_, map_, tokens_, w.span(), "While: JMP abs back to L_top");
        buffer_.emitU8(kOpJmpAbs);
        const std::size_t jmpTopAddrOperand = buffer_.size();
        buffer_.emitAddr16LE(0x0000);
        buffer_.patchAddr16LE(jmpTopAddrOperand, static_cast<std::uint16_t>(lTop));
        const std::size_t lExit = buffer_.size();
        buffer_.patchAddr16LE(jmpExitAddrOperand, static_cast<std::uint16_t>(lExit));
        codegenTrace(verbose_, map_, tokens_, w.span(), "While: L_exit at PC " + std::to_string(lExit));
    }

    void visitIfStatement(AstIfStatement& i) {
        const SourceLocation errLoc = locationAtSpan(map_, tokens_, i.span());
        codegenTrace(verbose_, map_, tokens_, i.span(), "If: emit condition → A as 0/1; BEQ will skip body when false");
        if (!emitBooleanExprAsIntInA(*i.condition())) {
            return;
        }
        const std::size_t beqOpcode = buffer_.size();
        buffer_.emitU8(kOpBeq);
        buffer_.emitU8(0x00);
        const std::size_t lBody = buffer_.size();
        codegenTrace(verbose_, map_, tokens_, i.span(), "If: L_body at PC " + std::to_string(lBody));
        visitBlock(*i.body());
        const std::size_t lSkip = buffer_.size();
        patchBranchRel8(buffer_, beqOpcode, lSkip, diagnostics_, errLoc);
        codegenTrace(verbose_, map_, tokens_, i.span(), "If: L_skip at PC " + std::to_string(lSkip));
    }

    void visitAssign(AstAssignStatement& st) {
        codegenTrace(verbose_, map_, tokens_, st.span(), "AssignStatement");
        if (!st.hasLhsResolvedDeclScope()) {
            codegenError(st.span(), "codegen: internal error (assignment LHS missing resolved scope)", "Re-run scope analysis.");
            return;
        }
        if (!emitIntExpr(*st.expr(), ExprTarget::Accumulator)) {
            return;
        }
        const codegen::VarKey key{st.name(), st.lhsResolvedDeclScopeId()};
        const std::uint16_t addr = layout_.addressOf(key);
        buffer_.emitU8(kOpStaAbs);
        buffer_.emitAddr16LE(addr);
    }

    /// Substep 1: route value to **A** (assignments, arithmetic operands) or **Y** (int print syscall).
    bool emitIntExpr(AstExpr& e, ExprTarget dest) {
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
                codegenError(e.span(), "codegen: integer literal out of range for 8-bit immediate", "Use values between 0 and 255, or extend the generator.");
                return false;
            }
            const auto imm = static_cast<std::uint8_t>(value);
            if (dest == ExprTarget::Accumulator) {
                codegenTrace(verbose_, map_, tokens_, e.span(), "emit LiteralInt → A (LDA #imm)");
                buffer_.emitU8(kOpLdaImm);
                buffer_.emitU8(imm);
            } else {
                codegenTrace(verbose_, map_, tokens_, e.span(), "emit LiteralInt → Y (LDY #imm)");
                buffer_.emitU8(kOpLdyImm);
                buffer_.emitU8(imm);
            }
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
            if (dest == ExprTarget::Accumulator) {
                codegenTrace(verbose_, map_, tokens_, e.span(), "emit IdentifierExpr → A (LDA abs)");
                buffer_.emitU8(kOpLdaAbs);
                buffer_.emitAddr16LE(addr);
            } else {
                codegenTrace(verbose_, map_, tokens_, e.span(), "emit IdentifierExpr → Y (LDY abs)");
                buffer_.emitU8(kOpLdyAbs);
                buffer_.emitAddr16LE(addr);
            }
            return true;
        }
        case AstNodeKind::AddExpr: {
            auto& a = static_cast<AstAddExpr&>(e);
            // Substep 2: fresh scratch per `+` so nested adds do not clobber the same cell.
            const std::uint16_t scratch = layout_.allocateAnonymous(1);
            codegenTrace(verbose_, map_, tokens_, e.span(), "emit AddExpr: evaluate left → A, STA temp $" + hexAddr(scratch));
            if (!emitIntExpr(*a.left(), ExprTarget::Accumulator)) {
                return false;
            }
            buffer_.emitU8(kOpStaAbs);
            buffer_.emitAddr16LE(scratch);
            codegenTrace(verbose_, map_, tokens_, e.span(), "emit AddExpr: evaluate right → A");
            if (!emitIntExpr(*a.right(), ExprTarget::Accumulator)) {
                return false;
            }
            codegenTrace(verbose_, map_, tokens_, e.span(), "emit AddExpr: CLC then ADC temp (8-bit add)");
            buffer_.emitU8(kOpClc);
            buffer_.emitU8(kOpAdcAbs);
            buffer_.emitAddr16LE(scratch);
            if (dest == ExprTarget::RegisterY) {
                codegenTrace(verbose_, map_, tokens_, e.span(), "emit AddExpr: TAY (result needed in Y)");
                buffer_.emitU8(kOpTay);
            }
            return true;
        }
        case AstNodeKind::BooleanExprWrapper: {
            auto& w = static_cast<AstBooleanExprWrapper&>(e);
            codegenTrace(verbose_, map_, tokens_, e.span(), "emit BooleanExprWrapper: lower to 0/1 in A");
            if (!emitBooleanExprAsIntInA(*w.inner())) {
                return false;
            }
            if (dest == ExprTarget::RegisterY) {
                codegenTrace(verbose_, map_, tokens_, e.span(), "emit BooleanExprWrapper: TAY");
                buffer_.emitU8(kOpTay);
            }
            return true;
        }
        case AstNodeKind::LiteralString:
            codegenError(
                e.span(),
                "codegen: string literal here is not supported (only `print(\"...\")` is lowered for strings)",
                "Move the string literal into a `print(...)` argument, or extend assignment/string variables."
            );
            return false;
        case AstNodeKind::LiteralBool: {
            const bool v = static_cast<const AstLiteralBool&>(e).value();
            codegenTrace(verbose_, map_, tokens_, e.span(), std::string("emit LiteralBool → ") + (dest == ExprTarget::Accumulator ? "A" : "Y") + " (" + (v ? "true" : "false") + ")");
            buffer_.emitU8(kOpLdaImm);
            buffer_.emitU8(static_cast<std::uint8_t>(v ? 1 : 0));
            if (dest == ExprTarget::RegisterY) {
                buffer_.emitU8(kOpTay);
            }
            return true;
        }
        default:
            break;
        }
        codegenError(e.span(), "codegen: unsupported expression form for 6502 lowering", "Extend `emitIntExpr` for this node kind.");
        return false;
    }

    bool emitBooleanExprAsIntInA(AstBooleanExpr& b) {
        switch (b.nodeKind()) {
        case AstNodeKind::BooleanLiteralExpr: {
            const bool v = static_cast<const AstBooleanLiteralExpr&>(b).value();
            codegenTrace(verbose_, map_, tokens_, b.span(), std::string("emit BooleanLiteralExpr → A (") + (v ? "true" : "false") + ")");
            buffer_.emitU8(kOpLdaImm);
            buffer_.emitU8(static_cast<std::uint8_t>(v ? 1 : 0));
            return true;
        }
        case AstNodeKind::BinaryBoolExpr:
            return emitBinaryIntCompare(static_cast<AstBinaryBoolExpr&>(b));
        default:
            codegenError(b.span(), "codegen: unsupported boolean expression", "Only literals and `==` / `!=` on ints are supported.");
            return false;
        }
    }

    /// Substep 4: int `==` / `!=` → 0/1 in A using SEC/SBC + relative branch + JMP abs patch.
    bool emitBinaryIntCompare(AstBinaryBoolExpr& b) {
        const AstBinaryBoolExpr::Op op = b.op();
        codegenTrace(
            verbose_,
            map_,
            tokens_,
            b.span(),
            std::string("emit BinaryBoolExpr: int compare (") + (op == AstBinaryBoolExpr::Op::Equal ? "==" : "!=") + ")"
        );

        const std::uint16_t leftSlot = layout_.allocateAnonymous(1);
        if (!emitIntExpr(*b.left(), ExprTarget::Accumulator)) {
            return false;
        }
        buffer_.emitU8(kOpStaAbs);
        buffer_.emitAddr16LE(leftSlot);
        codegenTrace(verbose_, map_, tokens_, b.span(), "emit compare: right → A, SEC, SBC leftTemp");

        if (!emitIntExpr(*b.right(), ExprTarget::Accumulator)) {
            return false;
        }
        buffer_.emitU8(kOpSec);
        buffer_.emitU8(kOpSbcAbs);
        buffer_.emitAddr16LE(leftSlot);

        const SourceLocation errLoc = locationAtSpan(map_, tokens_, b.span());

        if (op == AstBinaryBoolExpr::Op::Equal) {
            const std::size_t beqOpcode = buffer_.size();
            buffer_.emitU8(kOpBeq);
            buffer_.emitU8(0x00);
            codegenTrace(verbose_, map_, tokens_, b.span(), "emit compare `==`: BEQ → true branch");
            buffer_.emitU8(kOpLdaImm);
            buffer_.emitU8(0x00);
            buffer_.emitU8(kOpJmpAbs);
            const std::size_t jmpAddrOperand = buffer_.size();
            buffer_.emitAddr16LE(0x0000);
            const std::size_t trueLabel = buffer_.size();
            buffer_.emitU8(kOpLdaImm);
            buffer_.emitU8(0x01);
            const std::size_t skipLabel = buffer_.size();
            patchBranchRel8(buffer_, beqOpcode, trueLabel, diagnostics_, errLoc);
            buffer_.patchAddr16LE(jmpAddrOperand, static_cast<std::uint16_t>(skipLabel));
            return true;
        }

        const std::size_t bneOpcode = buffer_.size();
        buffer_.emitU8(kOpBne);
        buffer_.emitU8(0x00);
        codegenTrace(verbose_, map_, tokens_, b.span(), "emit compare `!=`: BNE → true branch");
        buffer_.emitU8(kOpLdaImm);
        buffer_.emitU8(0x00);
        buffer_.emitU8(kOpJmpAbs);
        const std::size_t jmpAddrOperandNe = buffer_.size();
        buffer_.emitAddr16LE(0x0000);
        const std::size_t trueLabelNe = buffer_.size();
        buffer_.emitU8(kOpLdaImm);
        buffer_.emitU8(0x01);
        const std::size_t skipLabelNe = buffer_.size();
        patchBranchRel8(buffer_, bneOpcode, trueLabelNe, diagnostics_, errLoc);
        buffer_.patchAddr16LE(jmpAddrOperandNe, static_cast<std::uint16_t>(skipLabelNe));
        return true;
    }

    static std::string hexAddr(std::uint16_t a) {
        std::ostringstream oss;
        oss << std::hex << a;
        return oss.str();
    }

    const SourceMap& map_;
    const std::vector<Token>& tokens_;
    DiagnosticBag& diagnostics_;
    bool verbose_;
    codegen::MemoryLayout& layout_;
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

    codegen::CodeBuffer buffer;
    Generator6502 gen(map, tokens, diagnostics, verbose, layout, target, buffer);
    gen.visitProgram(program);
    buffer.emitU8(kOpBrk);

    outObject = buffer.bytes();
    return diagnostics.errorCount() == errorsBefore;
}

} // namespace pavic
