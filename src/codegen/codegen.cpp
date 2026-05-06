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
constexpr std::uint8_t kOpLdxImm = 0xA2;
constexpr std::uint8_t kOpLdxAbs = 0xAE;
constexpr std::uint8_t kOpCpxAbs = 0xEC;
constexpr std::uint8_t kOpIncAbs = 0xEE;
constexpr std::uint8_t kOpBne = 0xD0;
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
    (void)map;
    (void)tokens;
    (void)span;
    if (message == "Block" || message == "While" || message == "If" || message == "Print" || message == "Scope Up") {
        std::cout << "CODE GEN " << message << "\n";
    }
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
          buffer_(buffer),
          zeroAddr_(layout_.allocateAnonymous(1)) {}

    void visitProgram(AstProgram& program) {
        codegenTrace(verbose_, map_, tokens_, program.span(), "Block");
        // Reserve a stable byte that always stores 0; used for CPX-based predicates and unconditional BNE.
        buffer_.emitU8(kOpLdaImm);
        buffer_.emitU8(0x00);
        buffer_.emitU8(kOpStaAbs);
        emitDataAddr16(zeroAddr_);
        visitBlock(*program.block());
    }

    bool backpatchDataToAfterCode() {
        std::uint16_t dataBase = static_cast<std::uint16_t>(buffer_.size());
        if (dataBase < target_.minDataBase) {
            dataBase = target_.minDataBase;
        }
        if (dataBase < target_.ramBase) {
            diagnostics_.addError(
                "codegen: invalid data layout base for relocation",
                {1, 1},
                "Ensure target.ramBase <= target.minDataBase."
            );
            return false;
        }

        const std::uint16_t delta = static_cast<std::uint16_t>(dataBase - target_.ramBase);
        for (const auto& fx : dataAddrFixups_) {
            const std::uint32_t relocatedWide = static_cast<std::uint32_t>(fx.address) + delta;
            if (relocatedWide > 0xFFFFu) {
                diagnostics_.addError(
                    "codegen: data relocation overflow while backpatching absolute address",
                    {1, 1},
                    "Program/data image exceeds 16-bit address space."
                );
                return false;
            }
            const std::uint16_t relocated = static_cast<std::uint16_t>(relocatedWide);
            buffer_.patchAddr16LE(fx.operandOffset, relocated);
            if (verbose_) {
                std::cout << "CODE GEN backpatch T* @" << fx.operandOffset << " => $" << hexAddr(relocated) << "\n";
            }
        }
        for (const auto& fx : dataImmLowFixups_) {
            const std::uint32_t relocatedWide = static_cast<std::uint32_t>(fx.address) + delta;
            if (relocatedWide > 0xFFFFu) {
                diagnostics_.addError(
                    "codegen: data relocation overflow while backpatching immediate pointer",
                    {1, 1},
                    "Program/data image exceeds 16-bit address space."
                );
                return false;
            }
            const std::uint16_t relocated = static_cast<std::uint16_t>(relocatedWide);
            if (relocated > 0xFF) {
                diagnostics_.addError(
                    "codegen: relocated string pointer no longer fits in 8-bit Y immediate",
                    {1, 1},
                    "Program code/data exceeded zero page; use a 16-bit pointer convention for syscall #2."
                );
                return false;
            }
            buffer_.patchU8(fx.operandOffset, static_cast<std::uint8_t>(relocated & 0xFF));
            if (verbose_) {
                std::cout << "CODE GEN backpatch T*low @" << fx.operandOffset << " => $" << hexAddr(relocated & 0xFF) << "\n";
            }
        }
        return true;
    }

private:
    struct DataAddrFixup {
        std::size_t operandOffset = 0;
        std::uint16_t address = 0;
    };
    struct DataImmLowFixup {
        std::size_t operandOffset = 0;
        std::uint16_t address = 0;
    };

    void emitDataAddr16(std::uint16_t addr) {
        const std::size_t off = buffer_.size();
        buffer_.emitAddr16LE(addr);
        if (verbose_) {
            std::cout << "CODE GEN placeholder T" << nextTempPlaceholderId_++ << " XX at byte " << off << "\n";
        }
        dataAddrFixups_.push_back(DataAddrFixup{off, addr});
    }
    void emitDataImmLow(std::uint16_t addr) {
        const std::size_t off = buffer_.size();
        buffer_.emitU8(static_cast<std::uint8_t>(addr & 0xFF));
        if (verbose_) {
            std::cout << "CODE GEN placeholder T" << nextTempPlaceholderId_++ << " (low) at byte " << off << "\n";
        }
        dataImmLowFixups_.push_back(DataImmLowFixup{off, addr});
    }

    void codegenError(AstSpan span, std::string message, std::string hint) {
        diagnostics_.addError(std::move(message), locationAtSpan(map_, tokens_, span), std::move(hint));
    }

    void visitBlock(AstBlock& block) {
        ++blockDepth_;
        codegenTrace(verbose_, map_, tokens_, block.span(), "Block");
        if (block.statements()) {
            for (auto& st : block.statements()->statements()) {
                if (st) {
                    visitStatement(*st);
                }
            }
        }
        if (blockDepth_ > 1) {
            codegenTrace(verbose_, map_, tokens_, block.span(), "Scope Up");
        }
        --blockDepth_;
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
        codegenTrace(verbose_, map_, tokens_, st.span(), "Print");
        AstExpr* expr = st.expr();
        if (expr->nodeKind() == AstNodeKind::LiteralString) {
            emitPrintStringLiteral(static_cast<AstLiteralString&>(*expr), st.span());
            return;
        }
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
            emitDataAddr16(static_cast<std::uint16_t>(base + static_cast<std::uint16_t>(i)));
        }
        buffer_.emitU8(kOpLdaImm);
        buffer_.emitU8(0x00);
        buffer_.emitU8(kOpStaAbs);
        emitDataAddr16(static_cast<std::uint16_t>(base + static_cast<std::uint16_t>(lex.size())));

        buffer_.emitU8(kOpLdyImm);
        emitDataImmLow(base);
        emitSysCallStringPrint();
    }

    void emitForceZClear() {
        buffer_.emitU8(kOpLdxImm);
        buffer_.emitU8(0x01);
        buffer_.emitU8(kOpCpxAbs);
        emitDataAddr16(zeroAddr_);
    }

    void emitBranchAlwaysTo(std::size_t targetOffset, AstSpan span) {
        emitForceZClear(); // guarantees Z=0, so the next BNE is unconditional.
        const std::size_t bneOpcode = buffer_.size();
        buffer_.emitU8(kOpBne);
        buffer_.emitU8(0x00);
        const std::size_t jumpId = nextJumpPlaceholderId_++;
        if (verbose_) {
            std::cout << "CODE GEN placeholder J" << jumpId << " XX at byte " << (bneOpcode + 1) << "\n";
        }
        patchBranchRel8(buffer_, bneOpcode, targetOffset, diagnostics_, locationAtSpan(map_, tokens_, span));
        if (verbose_) {
            const std::size_t nextPc = bneOpcode + 2;
            const int delta = static_cast<int>(targetOffset) - static_cast<int>(nextPc);
            std::cout << "CODE GEN backpatch J" << jumpId << " => " << delta << "\n";
        }
    }

    void visitWhileStatement(AstWhileStatement& w) {
        const SourceLocation errLoc = locationAtSpan(map_, tokens_, w.span());
        const std::size_t lTop = buffer_.size();
        codegenTrace(verbose_, map_, tokens_, w.span(), "While");
        if (!emitBooleanExprAsPredicateZ(*w.condition())) {
            return;
        }
        // false (Z=0) => branch to exit.
        const std::size_t bneOpcode = buffer_.size();
        buffer_.emitU8(kOpBne);
        buffer_.emitU8(0x00);
        const std::size_t jumpId = nextJumpPlaceholderId_++;
        if (verbose_) {
            std::cout << "CODE GEN placeholder J" << jumpId << " XX at byte " << (bneOpcode + 1) << "\n";
        }
        visitBlock(*w.body());
        emitBranchAlwaysTo(lTop, w.span());
        const std::size_t lExit = buffer_.size();
        patchBranchRel8(buffer_, bneOpcode, lExit, diagnostics_, errLoc);
        if (verbose_) {
            const std::size_t nextPc = bneOpcode + 2;
            const int delta = static_cast<int>(lExit) - static_cast<int>(nextPc);
            std::cout << "CODE GEN backpatch J" << jumpId << " => " << delta << "\n";
        }
    }

    void visitIfStatement(AstIfStatement& i) {
        const SourceLocation errLoc = locationAtSpan(map_, tokens_, i.span());
        codegenTrace(verbose_, map_, tokens_, i.span(), "If");
        if (!emitBooleanExprAsPredicateZ(*i.condition())) {
            return;
        }
        const std::size_t bneOpcode = buffer_.size();
        buffer_.emitU8(kOpBne);
        buffer_.emitU8(0x00);
        const std::size_t jumpId = nextJumpPlaceholderId_++;
        if (verbose_) {
            std::cout << "CODE GEN placeholder J" << jumpId << " XX at byte " << (bneOpcode + 1) << "\n";
        }
        visitBlock(*i.body());
        const std::size_t lSkip = buffer_.size();
        patchBranchRel8(buffer_, bneOpcode, lSkip, diagnostics_, errLoc);
        if (verbose_) {
            const std::size_t nextPc = bneOpcode + 2;
            const int delta = static_cast<int>(lSkip) - static_cast<int>(nextPc);
            std::cout << "CODE GEN backpatch J" << jumpId << " => " << delta << "\n";
        }
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
        emitDataAddr16(addr);
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
                emitDataAddr16(addr);
            } else {
                codegenTrace(verbose_, map_, tokens_, e.span(), "emit IdentifierExpr → Y (LDY abs)");
                buffer_.emitU8(kOpLdyAbs);
                emitDataAddr16(addr);
            }
            return true;
        }
        case AstNodeKind::AddExpr: {
            auto& a = static_cast<AstAddExpr&>(e);
            // Strict-opcode addition using INC/CPX/BNE loop (no CLC/TAY/JMP).
            const std::uint16_t sumSlot = layout_.allocateAnonymous(1);
            const std::uint16_t rightSlot = layout_.allocateAnonymous(1);
            const std::uint16_t counterSlot = layout_.allocateAnonymous(1);
            codegenTrace(verbose_, map_, tokens_, e.span(), "emit AddExpr: left->sum, right->limit, INC loop");
            if (!emitIntExpr(*a.left(), ExprTarget::Accumulator)) {
                return false;
            }
            buffer_.emitU8(kOpStaAbs);
            emitDataAddr16(sumSlot);
            codegenTrace(verbose_, map_, tokens_, e.span(), "emit AddExpr: evaluate right → A");
            if (!emitIntExpr(*a.right(), ExprTarget::Accumulator)) {
                return false;
            }
            buffer_.emitU8(kOpStaAbs);
            emitDataAddr16(rightSlot);
            buffer_.emitU8(kOpLdaImm);
            buffer_.emitU8(0x00);
            buffer_.emitU8(kOpStaAbs);
            emitDataAddr16(counterSlot);
            const std::size_t loopTop = buffer_.size();
            buffer_.emitU8(kOpLdxAbs);
            emitDataAddr16(counterSlot);
            buffer_.emitU8(kOpCpxAbs);
            emitDataAddr16(rightSlot); // Z=1 when counter==right => done.
            const std::size_t bneBodyOpcode = buffer_.size();
            buffer_.emitU8(kOpBne);
            buffer_.emitU8(0x00); // to body
            // equal-path (Z=1): skip body and continue at doneLabel.
            emitForceZClear();
            const std::size_t bneDoneOpcode = buffer_.size();
            buffer_.emitU8(kOpBne);
            buffer_.emitU8(0x00);
            const std::size_t bodyLabel = buffer_.size();
            patchBranchRel8(buffer_, bneBodyOpcode, bodyLabel, diagnostics_, locationAtSpan(map_, tokens_, e.span()));
            buffer_.emitU8(kOpIncAbs);
            emitDataAddr16(sumSlot);
            buffer_.emitU8(kOpIncAbs);
            emitDataAddr16(counterSlot);
            emitBranchAlwaysTo(loopTop, e.span());
            const std::size_t doneLabel = buffer_.size();
            patchBranchRel8(buffer_, bneDoneOpcode, doneLabel, diagnostics_, locationAtSpan(map_, tokens_, e.span()));
            buffer_.emitU8(kOpLdaAbs);
            emitDataAddr16(sumSlot);
            if (dest == ExprTarget::RegisterY) {
                buffer_.emitU8(kOpStaAbs);
                emitDataAddr16(counterSlot);
                buffer_.emitU8(kOpLdyAbs);
                emitDataAddr16(counterSlot);
            }
            return true;
        }
        case AstNodeKind::BooleanExprWrapper: {
            auto& w = static_cast<AstBooleanExprWrapper&>(e);
            codegenTrace(verbose_, map_, tokens_, e.span(), "emit BooleanExprWrapper: predicate then materialize 0/1 in A");
            if (!emitBooleanExprAsPredicateZ(*w.inner())) {
                return false;
            }
            // Z=1 true, Z=0 false
            buffer_.emitU8(kOpLdaImm);
            buffer_.emitU8(0x01);
            const std::size_t bneFalse = buffer_.size();
            buffer_.emitU8(kOpBne); // false (Z=0) => branch to write 0
            buffer_.emitU8(0x00);
            const std::size_t truePathEnd = buffer_.size();
            emitForceZClear();
            const std::size_t bneSkipFalse = buffer_.size();
            buffer_.emitU8(kOpBne);
            buffer_.emitU8(0x00);
            const std::size_t falseLabel = buffer_.size();
            buffer_.emitU8(kOpLdaImm);
            buffer_.emitU8(0x00);
            const std::size_t doneLabel = buffer_.size();
            patchBranchRel8(buffer_, bneFalse, falseLabel, diagnostics_, locationAtSpan(map_, tokens_, e.span()));
            patchBranchRel8(buffer_, bneSkipFalse, doneLabel, diagnostics_, locationAtSpan(map_, tokens_, e.span()));
            (void)truePathEnd;
            if (dest == ExprTarget::RegisterY) {
                const std::uint16_t boolSlot = layout_.allocateAnonymous(1);
                buffer_.emitU8(kOpStaAbs);
                emitDataAddr16(boolSlot);
                buffer_.emitU8(kOpLdyAbs);
                emitDataAddr16(boolSlot);
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
                const std::uint16_t tmp = layout_.allocateAnonymous(1);
                buffer_.emitU8(kOpStaAbs);
                emitDataAddr16(tmp);
                buffer_.emitU8(kOpLdyAbs);
                emitDataAddr16(tmp);
            }
            return true;
        }
        default:
            break;
        }
        codegenError(e.span(), "codegen: unsupported expression form for 6502 lowering", "Extend `emitIntExpr` for this node kind.");
        return false;
    }

    bool emitBooleanExprAsPredicateZ(AstBooleanExpr& b) {
        switch (b.nodeKind()) {
        case AstNodeKind::BooleanLiteralExpr: {
            const bool v = static_cast<const AstBooleanLiteralExpr&>(b).value();
            codegenTrace(verbose_, map_, tokens_, b.span(), std::string("emit BooleanLiteralExpr predicate (") + (v ? "true" : "false") + ")");
            if (v) {
                buffer_.emitU8(kOpLdxImm);
                buffer_.emitU8(0x00);
                buffer_.emitU8(kOpCpxAbs);
                emitDataAddr16(zeroAddr_); // Z=1 true
            } else {
                emitForceZClear(); // Z=0 false
            }
            return true;
        }
        case AstNodeKind::BinaryBoolExpr:
            return emitBinaryIntCompare(static_cast<AstBinaryBoolExpr&>(b));
        default:
            codegenError(b.span(), "codegen: unsupported boolean expression", "Only literals and `==` / `!=` on ints are supported.");
            return false;
        }
    }

    /// int `==` / `!=` predicate using LDX/CPX/BNE, with true encoded as Z=1.
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
        const std::uint16_t rightSlot = layout_.allocateAnonymous(1);
        if (!emitIntExpr(*b.left(), ExprTarget::Accumulator)) {
            return false;
        }
        buffer_.emitU8(kOpStaAbs);
        emitDataAddr16(leftSlot);
        codegenTrace(verbose_, map_, tokens_, b.span(), "emit compare: right → slot, LDX left, CPX right");

        if (!emitIntExpr(*b.right(), ExprTarget::Accumulator)) {
            return false;
        }
        buffer_.emitU8(kOpStaAbs);
        emitDataAddr16(rightSlot);
        buffer_.emitU8(kOpLdxAbs);
        emitDataAddr16(leftSlot);
        buffer_.emitU8(kOpCpxAbs);
        emitDataAddr16(rightSlot); // Z=1 when equal

        if (op == AstBinaryBoolExpr::Op::Equal) {
            return true;
        }

        // Invert Z for `!=`: after this, Z=1 means not equal.
        const std::size_t bneNotEq = buffer_.size();
        buffer_.emitU8(kOpBne);
        buffer_.emitU8(0x00); // branch when original Z=0 (not equal)
        // equal path => force Z=0
        emitForceZClear();
        const std::size_t bneSkipNotEq = buffer_.size();
        buffer_.emitU8(kOpBne);
        buffer_.emitU8(0x00);
        const std::size_t notEqLabel = buffer_.size();
        // not-equal path => force Z=1
        buffer_.emitU8(kOpLdxImm);
        buffer_.emitU8(0x00);
        buffer_.emitU8(kOpCpxAbs);
        emitDataAddr16(zeroAddr_);
        const std::size_t doneLabel = buffer_.size();
        const SourceLocation errLoc = locationAtSpan(map_, tokens_, b.span());
        patchBranchRel8(buffer_, bneNotEq, notEqLabel, diagnostics_, errLoc);
        patchBranchRel8(buffer_, bneSkipNotEq, doneLabel, diagnostics_, errLoc);
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
    std::uint16_t zeroAddr_;
    std::vector<DataAddrFixup> dataAddrFixups_;
    std::vector<DataImmLowFixup> dataImmLowFixups_;
    std::size_t nextTempPlaceholderId_ = 0;
    std::size_t nextJumpPlaceholderId_ = 0;
    int blockDepth_ = 0;
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
    if (!gen.backpatchDataToAfterCode()) {
        return diagnostics.errorCount() == errorsBefore;
    }

    if (target.programImageBytes != 0 && buffer.size() < target.programImageBytes) {
        while (buffer.size() < target.programImageBytes) {
            buffer.emitU8(0x00);
        }
    }

    outObject = buffer.bytes();
    return diagnostics.errorCount() == errorsBefore;
}

} // namespace pavic
