#include "multi_backend.hpp"

#include "ast.hpp"
#include "diagnostic.hpp"
#include "source.hpp"
#include "token.hpp"

#include <cctype>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace pavic {

namespace {

enum class ValueType {
    Unknown,
    Int,
    Bool,
    String,
};

SourceLocation locationAtSpan(const SourceMap& map, const std::vector<Token>& tokens, AstSpan span) {
    std::size_t byteOffset = 0;
    if (!tokens.empty() && span.begin < tokens.size()) {
        byteOffset = tokens[span.begin].offset;
    }
    return map.locationAt(byteOffset);
}

std::string stripStringQuotes(const std::string& lexeme) {
    if (lexeme.size() >= 2 && lexeme.front() == '"' && lexeme.back() == '"') {
        return lexeme.substr(1, lexeme.size() - 2);
    }
    return lexeme;
}

std::string escapeJavaString(const std::string& in) {
    std::string out;
    out.reserve(in.size() + 8);
    for (unsigned char ch : in) {
        switch (ch) {
        case '\\':
            out += "\\\\";
            break;
        case '"':
            out += "\\\"";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\t':
            out += "\\t";
            break;
        default:
            out.push_back(static_cast<char>(ch));
            break;
        }
    }
    return out;
}

std::string escapeTsString(const std::string& in) {
    return escapeJavaString(in);
}

std::string escapeLlvmCString(const std::string& in) {
    std::ostringstream out;
    for (unsigned char ch : in) {
        if (ch == '\\' || ch == '"') {
            out << '\\' << static_cast<char>(ch);
        } else if (std::isprint(ch) && ch != '\n' && ch != '\r' && ch != '\t') {
            out << static_cast<char>(ch);
        } else {
            const char* hex = "0123456789ABCDEF";
            out << '\\' << hex[(ch >> 4) & 0xF] << hex[ch & 0xF];
        }
    }
    return out.str();
}

struct ScopeTypes {
    std::vector<std::unordered_map<char, ValueType>> stack;

    void push() { stack.emplace_back(); }
    void pop() {
        if (!stack.empty()) {
            stack.pop_back();
        }
    }
    void declare(char name, ValueType type) {
        if (stack.empty()) {
            push();
        }
        stack.back()[name] = type;
    }
    ValueType lookup(char name) const {
        for (auto it = stack.rbegin(); it != stack.rend(); ++it) {
            auto found = it->find(name);
            if (found != it->end()) {
                return found->second;
            }
        }
        return ValueType::Unknown;
    }
};

ValueType inferBooleanExprType(const AstBooleanExpr& b, const ScopeTypes& scopes);

ValueType inferExprType(const AstExpr& e, const ScopeTypes& scopes) {
    switch (e.nodeKind()) {
    case AstNodeKind::LiteralInt:
        return ValueType::Int;
    case AstNodeKind::LiteralString:
        return ValueType::String;
    case AstNodeKind::LiteralBool:
        return ValueType::Bool;
    case AstNodeKind::IdentifierExpr:
        return scopes.lookup(static_cast<const AstIdentifierExpr&>(e).name());
    case AstNodeKind::AddExpr:
        return ValueType::Int;
    case AstNodeKind::BooleanExprWrapper:
        return inferBooleanExprType(*static_cast<const AstBooleanExprWrapper&>(e).inner(), scopes);
    default:
        return ValueType::Unknown;
    }
}

ValueType inferBooleanExprType(const AstBooleanExpr& b, const ScopeTypes&) {
    if (b.nodeKind() == AstNodeKind::BinaryBoolExpr || b.nodeKind() == AstNodeKind::BooleanLiteralExpr) {
        return ValueType::Bool;
    }
    return ValueType::Unknown;
}

std::string indent(int depth) {
    return std::string(static_cast<std::size_t>(depth * 2), ' ');
}

class JavaEmitter {
public:
    JavaEmitter(const SourceMap& map, const std::vector<Token>& tokens, DiagnosticBag& diagnostics)
        : map_(map), tokens_(tokens), diagnostics_(diagnostics) {}

    bool emit(const std::vector<AstProgram*>& programs, std::string& outText) {
        out_.str("");
        out_.clear();
        out_ << "public final class GeneratedProgram {\n";
        for (std::size_t i = 0; i < programs.size(); ++i) {
            if (programs[i] == nullptr) {
                continue;
            }
            out_ << "  private static void runProgram" << (i + 1) << "() {\n";
            emitBlock(*programs[i]->block(), 2);
            out_ << "  }\n\n";
        }
        out_ << "  public static void main(String[] args) {\n";
        for (std::size_t i = 0; i < programs.size(); ++i) {
            if (programs[i] != nullptr) {
                out_ << "    runProgram" << (i + 1) << "();\n";
            }
        }
        out_ << "  }\n";
        out_ << "}\n";
        outText = out_.str();
        return !diagnostics_.hasErrors();
    }

private:
    void pushScope() {
        scopes_.push();
        aliasScopes_.emplace_back();
    }
    void popScope() {
        scopes_.pop();
        if (!aliasScopes_.empty()) {
            aliasScopes_.pop_back();
        }
    }
    std::string declareAlias(char name) {
        const std::string alias = std::string(1, name) + "_" + std::to_string(nextAliasId_++);
        if (aliasScopes_.empty()) {
            aliasScopes_.emplace_back();
        }
        aliasScopes_.back()[name] = alias;
        return alias;
    }
    std::string lookupAlias(char name) const {
        for (auto it = aliasScopes_.rbegin(); it != aliasScopes_.rend(); ++it) {
            auto found = it->find(name);
            if (found != it->end()) {
                return found->second;
            }
        }
        return std::string(1, name);
    }

    void emitBlock(const AstBlock& block, int depth) {
        if (block.statements() == nullptr) {
            return;
        }
        pushScope();
        for (const auto& st : block.statements()->statements()) {
            if (st) {
                emitStatement(*st, depth);
            }
        }
        popScope();
    }

    void emitStatement(const AstStatement& st, int depth) {
        switch (st.nodeKind()) {
        case AstNodeKind::VarDeclStatement: {
            const auto& v = static_cast<const AstVarDeclStatement&>(st);
            ValueType t = ValueType::Unknown;
            const std::string alias = declareAlias(v.name());
            if (v.typeName() == "int") {
                t = ValueType::Int;
                out_ << indent(depth) << "int " << alias << " = 0;\n";
            } else if (v.typeName() == "boolean") {
                t = ValueType::Bool;
                out_ << indent(depth) << "boolean " << alias << " = false;\n";
            } else if (v.typeName() == "string") {
                t = ValueType::String;
                out_ << indent(depth) << "String " << alias << " = \"\";\n";
            }
            scopes_.declare(v.name(), t);
            return;
        }
        case AstNodeKind::AssignStatement: {
            const auto& a = static_cast<const AstAssignStatement&>(st);
            out_ << indent(depth) << lookupAlias(a.name()) << " = " << emitExpr(*a.expr()) << ";\n";
            return;
        }
        case AstNodeKind::PrintStatement: {
            const auto& p = static_cast<const AstPrintStatement&>(st);
            out_ << indent(depth) << "System.out.print(" << emitExpr(*p.expr()) << ");\n";
            return;
        }
        case AstNodeKind::IfStatement: {
            const auto& i = static_cast<const AstIfStatement&>(st);
            out_ << indent(depth) << "if (" << emitBooleanExpr(*i.condition()) << ") {\n";
            emitBlock(*i.body(), depth + 1);
            out_ << indent(depth) << "}\n";
            return;
        }
        case AstNodeKind::WhileStatement: {
            const auto& w = static_cast<const AstWhileStatement&>(st);
            out_ << indent(depth) << "while (" << emitBooleanExpr(*w.condition()) << ") {\n";
            emitBlock(*w.body(), depth + 1);
            out_ << indent(depth) << "}\n";
            return;
        }
        case AstNodeKind::BlockStatement: {
            const auto& b = static_cast<const AstBlockStatement&>(st);
            out_ << indent(depth) << "{\n";
            emitBlock(*b.block(), depth + 1);
            out_ << indent(depth) << "}\n";
            return;
        }
        default:
            diagnostics_.addError("java backend: unsupported statement", locationAtSpan(map_, tokens_, st.span()));
            return;
        }
    }

    std::string emitExpr(const AstExpr& e) {
        switch (e.nodeKind()) {
        case AstNodeKind::LiteralInt:
            return static_cast<const AstLiteralInt&>(e).lexeme();
        case AstNodeKind::LiteralString:
            return std::string("\"") + escapeJavaString(stripStringQuotes(static_cast<const AstLiteralString&>(e).lexeme())) + "\"";
        case AstNodeKind::LiteralBool:
            return static_cast<const AstLiteralBool&>(e).value() ? "true" : "false";
        case AstNodeKind::IdentifierExpr:
            return lookupAlias(static_cast<const AstIdentifierExpr&>(e).name());
        case AstNodeKind::AddExpr: {
            const auto& a = static_cast<const AstAddExpr&>(e);
            return "(" + emitExpr(*a.left()) + " + " + emitExpr(*a.right()) + ")";
        }
        case AstNodeKind::BooleanExprWrapper:
            return emitBooleanExpr(*static_cast<const AstBooleanExprWrapper&>(e).inner());
        default:
            diagnostics_.addError("java backend: unsupported expression", locationAtSpan(map_, tokens_, e.span()));
            return "0";
        }
    }

    std::string emitBooleanExpr(const AstBooleanExpr& b) {
        switch (b.nodeKind()) {
        case AstNodeKind::BooleanLiteralExpr:
            return static_cast<const AstBooleanLiteralExpr&>(b).value() ? "true" : "false";
        case AstNodeKind::BinaryBoolExpr: {
            const auto& cmp = static_cast<const AstBinaryBoolExpr&>(b);
            const ValueType lt = inferExprType(*cmp.left(), scopes_);
            const ValueType rt = inferExprType(*cmp.right(), scopes_);
            const bool strCmp = (lt == ValueType::String && rt == ValueType::String);
            const std::string op = (cmp.op() == AstBinaryBoolExpr::Op::Equal) ? "==" : "!=";
            if (strCmp) {
                const std::string call =
                    "java.util.Objects.equals(" + emitExpr(*cmp.left()) + ", " + emitExpr(*cmp.right()) + ")";
                return (cmp.op() == AstBinaryBoolExpr::Op::Equal) ? call : "(!" + call + ")";
            }
            return "(" + emitExpr(*cmp.left()) + " " + op + " " + emitExpr(*cmp.right()) + ")";
        }
        default:
            diagnostics_.addError("java backend: unsupported boolean expression", locationAtSpan(map_, tokens_, b.span()));
            return "false";
        }
    }

    const SourceMap& map_;
    const std::vector<Token>& tokens_;
    DiagnosticBag& diagnostics_;
    ScopeTypes scopes_;
    std::vector<std::unordered_map<char, std::string>> aliasScopes_;
    std::size_t nextAliasId_ = 0;
    std::ostringstream out_;
};

class TsEmitter {
public:
    TsEmitter(const SourceMap& map, const std::vector<Token>& tokens, DiagnosticBag& diagnostics)
        : map_(map), tokens_(tokens), diagnostics_(diagnostics) {}

    bool emit(const std::vector<AstProgram*>& programs, std::string& outText) {
        out_.str("");
        out_.clear();
        out_ << "/* Generated TypeScript from Markos AST */\n";
        out_ << "declare const process: any;\n";
        for (std::size_t i = 0; i < programs.size(); ++i) {
            if (programs[i] == nullptr) {
                continue;
            }
            out_ << "function runProgram" << (i + 1) << "(): void {\n";
            emitBlock(*programs[i]->block(), 1);
            out_ << "}\n\n";
        }
        out_ << "function mkPrint(v: number | boolean | string): void {\n";
        out_ << "  process.stdout.write(String(v));\n";
        out_ << "}\n\n";
        for (std::size_t i = 0; i < programs.size(); ++i) {
            if (programs[i] != nullptr) {
                out_ << "runProgram" << (i + 1) << "();\n";
            }
        }
        outText = out_.str();
        return !diagnostics_.hasErrors();
    }

private:
    void pushScope() {
        scopes_.push();
        aliasScopes_.emplace_back();
    }
    void popScope() {
        scopes_.pop();
        if (!aliasScopes_.empty()) {
            aliasScopes_.pop_back();
        }
    }
    std::string declareAlias(char name) {
        const std::string alias = std::string(1, name) + "_" + std::to_string(nextAliasId_++);
        if (aliasScopes_.empty()) {
            aliasScopes_.emplace_back();
        }
        aliasScopes_.back()[name] = alias;
        return alias;
    }
    std::string lookupAlias(char name) const {
        for (auto it = aliasScopes_.rbegin(); it != aliasScopes_.rend(); ++it) {
            auto found = it->find(name);
            if (found != it->end()) {
                return found->second;
            }
        }
        return std::string(1, name);
    }

    void emitBlock(const AstBlock& block, int depth) {
        if (block.statements() == nullptr) {
            return;
        }
        pushScope();
        for (const auto& st : block.statements()->statements()) {
            if (st) {
                emitStatement(*st, depth);
            }
        }
        popScope();
    }

    void emitStatement(const AstStatement& st, int depth) {
        switch (st.nodeKind()) {
        case AstNodeKind::VarDeclStatement: {
            const auto& v = static_cast<const AstVarDeclStatement&>(st);
            ValueType t = ValueType::Unknown;
            const std::string alias = declareAlias(v.name());
            if (v.typeName() == "int") {
                t = ValueType::Int;
                out_ << indent(depth) << "let " << alias << ": number = 0;\n";
            } else if (v.typeName() == "boolean") {
                t = ValueType::Bool;
                out_ << indent(depth) << "let " << alias << ": boolean = false;\n";
            } else if (v.typeName() == "string") {
                t = ValueType::String;
                out_ << indent(depth) << "let " << alias << ": string = \"\";\n";
            }
            scopes_.declare(v.name(), t);
            return;
        }
        case AstNodeKind::AssignStatement: {
            const auto& a = static_cast<const AstAssignStatement&>(st);
            out_ << indent(depth) << lookupAlias(a.name()) << " = " << emitExpr(*a.expr()) << ";\n";
            return;
        }
        case AstNodeKind::PrintStatement: {
            const auto& p = static_cast<const AstPrintStatement&>(st);
            out_ << indent(depth) << "mkPrint(" << emitExpr(*p.expr()) << ");\n";
            return;
        }
        case AstNodeKind::IfStatement: {
            const auto& i = static_cast<const AstIfStatement&>(st);
            out_ << indent(depth) << "if (" << emitBooleanExpr(*i.condition()) << ") {\n";
            emitBlock(*i.body(), depth + 1);
            out_ << indent(depth) << "}\n";
            return;
        }
        case AstNodeKind::WhileStatement: {
            const auto& w = static_cast<const AstWhileStatement&>(st);
            out_ << indent(depth) << "while (" << emitBooleanExpr(*w.condition()) << ") {\n";
            emitBlock(*w.body(), depth + 1);
            out_ << indent(depth) << "}\n";
            return;
        }
        case AstNodeKind::BlockStatement: {
            const auto& b = static_cast<const AstBlockStatement&>(st);
            out_ << indent(depth) << "{\n";
            emitBlock(*b.block(), depth + 1);
            out_ << indent(depth) << "}\n";
            return;
        }
        default:
            diagnostics_.addError("ts backend: unsupported statement", locationAtSpan(map_, tokens_, st.span()));
            return;
        }
    }

    std::string emitExpr(const AstExpr& e) {
        switch (e.nodeKind()) {
        case AstNodeKind::LiteralInt:
            return static_cast<const AstLiteralInt&>(e).lexeme();
        case AstNodeKind::LiteralString:
            return std::string("\"") + escapeTsString(stripStringQuotes(static_cast<const AstLiteralString&>(e).lexeme())) + "\"";
        case AstNodeKind::LiteralBool:
            return static_cast<const AstLiteralBool&>(e).value() ? "true" : "false";
        case AstNodeKind::IdentifierExpr:
            return lookupAlias(static_cast<const AstIdentifierExpr&>(e).name());
        case AstNodeKind::AddExpr: {
            const auto& a = static_cast<const AstAddExpr&>(e);
            return "(" + emitExpr(*a.left()) + " + " + emitExpr(*a.right()) + ")";
        }
        case AstNodeKind::BooleanExprWrapper:
            return emitBooleanExpr(*static_cast<const AstBooleanExprWrapper&>(e).inner());
        default:
            diagnostics_.addError("ts backend: unsupported expression", locationAtSpan(map_, tokens_, e.span()));
            return "0";
        }
    }

    std::string emitBooleanExpr(const AstBooleanExpr& b) {
        switch (b.nodeKind()) {
        case AstNodeKind::BooleanLiteralExpr:
            return static_cast<const AstBooleanLiteralExpr&>(b).value() ? "true" : "false";
        case AstNodeKind::BinaryBoolExpr: {
            const auto& cmp = static_cast<const AstBinaryBoolExpr&>(b);
            const std::string op = (cmp.op() == AstBinaryBoolExpr::Op::Equal) ? "===" : "!==";
            return "(" + emitExpr(*cmp.left()) + " " + op + " " + emitExpr(*cmp.right()) + ")";
        }
        default:
            diagnostics_.addError("ts backend: unsupported boolean expression", locationAtSpan(map_, tokens_, b.span()));
            return "false";
        }
    }

    const SourceMap& map_;
    const std::vector<Token>& tokens_;
    DiagnosticBag& diagnostics_;
    ScopeTypes scopes_;
    std::vector<std::unordered_map<char, std::string>> aliasScopes_;
    std::size_t nextAliasId_ = 0;
    std::ostringstream out_;
};

class LlvmEmitter {
public:
    LlvmEmitter(const SourceMap& map, const std::vector<Token>& tokens, DiagnosticBag& diagnostics)
        : map_(map), tokens_(tokens), diagnostics_(diagnostics) {}

    bool emit(const std::vector<AstProgram*>& programs, std::string& outText) {
        strings_.clear();
        out_.str("");
        out_.clear();
        out_ << "; Generated LLVM IR from Markos AST\n";
        out_ << "declare i32 @printf(ptr, ...)\n";
        out_ << "declare i32 @strcmp(ptr, ptr)\n";
        out_ << "@.fmt_int = private unnamed_addr constant [3 x i8] c\"%d\\00\"\n";
        out_ << "@.fmt_str = private unnamed_addr constant [3 x i8] c\"%s\\00\"\n";
        out_ << "\n";

        out_ << "define i32 @main() {\n";
        emitLine("entry:");
        scopes_.clear();
        scopes_.emplace_back();
        for (const AstProgram* p : programs) {
            if (p != nullptr && p->block() != nullptr) {
                emitBlock(*p->block());
            }
        }
        emitLine("ret i32 0");
        out_ << "}\n\n";

        for (const auto& s : strings_) {
            out_ << s << "\n";
        }
        outText = out_.str();
        return !diagnostics_.hasErrors();
    }

private:
    struct VarInfo {
        ValueType type = ValueType::Unknown;
        std::string ptrName;
    };
    struct LlvmValue {
        ValueType type = ValueType::Unknown;
        std::string reg;
    };

    void emitLine(const std::string& line) { out_ << "  " << line << "\n"; }

    std::string freshReg() { return "%t" + std::to_string(nextReg_++); }
    std::string freshLabel(const std::string& base) { return base + std::to_string(nextLabel_++); }

    void pushScope() { scopes_.emplace_back(); }
    void popScope() {
        if (!scopes_.empty()) {
            scopes_.pop_back();
        }
    }
    void declareVar(char name, ValueType t, const std::string& ptr) {
        if (scopes_.empty()) {
            pushScope();
        }
        scopes_.back()[name] = VarInfo{t, ptr};
    }
    const VarInfo* lookupVar(char name) const {
        for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it) {
            auto found = it->find(name);
            if (found != it->end()) {
                return &found->second;
            }
        }
        return nullptr;
    }

    std::string getOrCreateStringGlobal(const std::string& payload) {
        auto found = stringIds_.find(payload);
        if (found != stringIds_.end()) {
            return found->second;
        }
        const std::string sym = "@.str" + std::to_string(stringIds_.size());
        const std::string esc = escapeLlvmCString(payload);
        const std::size_t n = payload.size() + 1;
        std::ostringstream def;
        def << sym << " = private unnamed_addr constant [" << n << " x i8] c\"" << esc << "\\00\"";
        strings_.push_back(def.str());
        stringIds_[payload] = sym;
        return sym;
    }

    std::string gepStringPtr(const std::string& sym, std::size_t len) {
        const std::string reg = freshReg();
        emitLine(reg + " = getelementptr inbounds [" + std::to_string(len + 1) + " x i8], ptr " + sym + ", i64 0, i64 0");
        return reg;
    }

    LlvmValue emitExpr(const AstExpr& e) {
        switch (e.nodeKind()) {
        case AstNodeKind::LiteralInt:
            return LlvmValue{ValueType::Int, static_cast<const AstLiteralInt&>(e).lexeme()};
        case AstNodeKind::LiteralBool:
            return LlvmValue{ValueType::Bool, static_cast<const AstLiteralBool&>(e).value() ? "1" : "0"};
        case AstNodeKind::LiteralString: {
            const std::string payload = stripStringQuotes(static_cast<const AstLiteralString&>(e).lexeme());
            const std::string sym = getOrCreateStringGlobal(payload);
            return LlvmValue{ValueType::String, gepStringPtr(sym, payload.size())};
        }
        case AstNodeKind::IdentifierExpr: {
            const char name = static_cast<const AstIdentifierExpr&>(e).name();
            const VarInfo* info = lookupVar(name);
            if (info == nullptr) {
                diagnostics_.addError("llvm backend: unresolved identifier in expression", locationAtSpan(map_, tokens_, e.span()));
                return LlvmValue{};
            }
            const std::string r = freshReg();
            if (info->type == ValueType::Int) {
                emitLine(r + " = load i32, ptr " + info->ptrName);
            } else if (info->type == ValueType::Bool) {
                emitLine(r + " = load i1, ptr " + info->ptrName);
            } else if (info->type == ValueType::String) {
                emitLine(r + " = load ptr, ptr " + info->ptrName);
            }
            return LlvmValue{info->type, r};
        }
        case AstNodeKind::AddExpr: {
            const auto& a = static_cast<const AstAddExpr&>(e);
            LlvmValue l = emitExpr(*a.left());
            LlvmValue r = emitExpr(*a.right());
            const std::string dst = freshReg();
            emitLine(dst + " = add nsw i32 " + l.reg + ", " + r.reg);
            return LlvmValue{ValueType::Int, dst};
        }
        case AstNodeKind::BooleanExprWrapper:
            return emitBooleanExpr(*static_cast<const AstBooleanExprWrapper&>(e).inner());
        default:
            diagnostics_.addError("llvm backend: unsupported expression", locationAtSpan(map_, tokens_, e.span()));
            return LlvmValue{};
        }
    }

    LlvmValue emitBooleanExpr(const AstBooleanExpr& b) {
        if (b.nodeKind() == AstNodeKind::BooleanLiteralExpr) {
            return LlvmValue{ValueType::Bool, static_cast<const AstBooleanLiteralExpr&>(b).value() ? "1" : "0"};
        }
        if (b.nodeKind() != AstNodeKind::BinaryBoolExpr) {
            diagnostics_.addError("llvm backend: unsupported boolean expression", locationAtSpan(map_, tokens_, b.span()));
            return LlvmValue{};
        }
        const auto& cmp = static_cast<const AstBinaryBoolExpr&>(b);
        LlvmValue l = emitExpr(*cmp.left());
        LlvmValue r = emitExpr(*cmp.right());
        const std::string outReg = freshReg();
        if (l.type == ValueType::String && r.type == ValueType::String) {
            const std::string c = freshReg();
            emitLine(c + " = call i32 @strcmp(ptr " + l.reg + ", ptr " + r.reg + ")");
            const std::string pred = (cmp.op() == AstBinaryBoolExpr::Op::Equal) ? "eq" : "ne";
            emitLine(outReg + " = icmp " + pred + " i32 " + c + ", 0");
            return LlvmValue{ValueType::Bool, outReg};
        }
        std::string pred = (cmp.op() == AstBinaryBoolExpr::Op::Equal) ? "eq" : "ne";
        if (l.type == ValueType::Int && r.type == ValueType::Int) {
            emitLine(outReg + " = icmp " + pred + " i32 " + l.reg + ", " + r.reg);
            return LlvmValue{ValueType::Bool, outReg};
        }
        emitLine(outReg + " = icmp " + pred + " i1 " + l.reg + ", " + r.reg);
        return LlvmValue{ValueType::Bool, outReg};
    }

    void emitStatement(const AstStatement& st) {
        switch (st.nodeKind()) {
        case AstNodeKind::VarDeclStatement: {
            const auto& v = static_cast<const AstVarDeclStatement&>(st);
            const std::string ptr = "%" + std::string(1, v.name()) + "." + std::to_string(nextReg_++);
            if (v.typeName() == "int") {
                emitLine(ptr + " = alloca i32");
                emitLine("store i32 0, ptr " + ptr);
                declareVar(v.name(), ValueType::Int, ptr);
            } else if (v.typeName() == "boolean") {
                emitLine(ptr + " = alloca i1");
                emitLine("store i1 0, ptr " + ptr);
                declareVar(v.name(), ValueType::Bool, ptr);
            } else if (v.typeName() == "string") {
                emitLine(ptr + " = alloca ptr");
                const std::string emptySym = getOrCreateStringGlobal("");
                const std::string emptyPtr = gepStringPtr(emptySym, 0);
                emitLine("store ptr " + emptyPtr + ", ptr " + ptr);
                declareVar(v.name(), ValueType::String, ptr);
            }
            return;
        }
        case AstNodeKind::AssignStatement: {
            const auto& a = static_cast<const AstAssignStatement&>(st);
            const VarInfo* info = lookupVar(a.name());
            if (info == nullptr) {
                diagnostics_.addError("llvm backend: unresolved assignment target", locationAtSpan(map_, tokens_, st.span()));
                return;
            }
            LlvmValue rhs = emitExpr(*a.expr());
            if (info->type == ValueType::Int) {
                emitLine("store i32 " + rhs.reg + ", ptr " + info->ptrName);
            } else if (info->type == ValueType::Bool) {
                emitLine("store i1 " + rhs.reg + ", ptr " + info->ptrName);
            } else if (info->type == ValueType::String) {
                emitLine("store ptr " + rhs.reg + ", ptr " + info->ptrName);
            }
            return;
        }
        case AstNodeKind::PrintStatement: {
            const auto& p = static_cast<const AstPrintStatement&>(st);
            LlvmValue v = emitExpr(*p.expr());
            if (v.type == ValueType::Int) {
                const std::string fmt = freshReg();
                emitLine(fmt + " = getelementptr inbounds [3 x i8], ptr @.fmt_int, i64 0, i64 0");
                emitLine("call i32 (ptr, ...) @printf(ptr " + fmt + ", i32 " + v.reg + ")");
            } else if (v.type == ValueType::Bool) {
                const std::string ext = freshReg();
                emitLine(ext + " = zext i1 " + v.reg + " to i32");
                const std::string fmt = freshReg();
                emitLine(fmt + " = getelementptr inbounds [3 x i8], ptr @.fmt_int, i64 0, i64 0");
                emitLine("call i32 (ptr, ...) @printf(ptr " + fmt + ", i32 " + ext + ")");
            } else if (v.type == ValueType::String) {
                const std::string fmt = freshReg();
                emitLine(fmt + " = getelementptr inbounds [3 x i8], ptr @.fmt_str, i64 0, i64 0");
                emitLine("call i32 (ptr, ...) @printf(ptr " + fmt + ", ptr " + v.reg + ")");
            }
            return;
        }
        case AstNodeKind::IfStatement: {
            const auto& i = static_cast<const AstIfStatement&>(st);
            LlvmValue cond = emitBooleanExpr(*i.condition());
            const std::string thenLabel = freshLabel("if_then_");
            const std::string endLabel = freshLabel("if_end_");
            emitLine("br i1 " + cond.reg + ", label %" + thenLabel + ", label %" + endLabel);
            out_ << thenLabel << ":\n";
            emitBlock(*i.body());
            emitLine("br label %" + endLabel);
            out_ << endLabel << ":\n";
            return;
        }
        case AstNodeKind::WhileStatement: {
            const auto& w = static_cast<const AstWhileStatement&>(st);
            const std::string condLabel = freshLabel("while_cond_");
            const std::string bodyLabel = freshLabel("while_body_");
            const std::string endLabel = freshLabel("while_end_");
            emitLine("br label %" + condLabel);
            out_ << condLabel << ":\n";
            LlvmValue cond = emitBooleanExpr(*w.condition());
            emitLine("br i1 " + cond.reg + ", label %" + bodyLabel + ", label %" + endLabel);
            out_ << bodyLabel << ":\n";
            emitBlock(*w.body());
            emitLine("br label %" + condLabel);
            out_ << endLabel << ":\n";
            return;
        }
        case AstNodeKind::BlockStatement: {
            const auto& b = static_cast<const AstBlockStatement&>(st);
            emitBlock(*b.block());
            return;
        }
        default:
            diagnostics_.addError("llvm backend: unsupported statement", locationAtSpan(map_, tokens_, st.span()));
            return;
        }
    }

    void emitBlock(const AstBlock& block) {
        if (block.statements() == nullptr) {
            return;
        }
        pushScope();
        for (const auto& st : block.statements()->statements()) {
            if (st) {
                emitStatement(*st);
            }
        }
        popScope();
    }

    const SourceMap& map_;
    const std::vector<Token>& tokens_;
    DiagnosticBag& diagnostics_;
    std::ostringstream out_;
    std::vector<std::string> strings_;
    std::unordered_map<std::string, std::string> stringIds_;
    std::vector<std::unordered_map<char, VarInfo>> scopes_;
    std::size_t nextReg_ = 0;
    std::size_t nextLabel_ = 0;
};

} // namespace

bool parseBackendTarget(const std::string& value, BackendTarget& outTarget) {
    if (value == "6502" || value == "alan6502") {
        outTarget = BackendTarget::Target6502;
        return true;
    }
    if (value == "llvm" || value == "llvm-ir") {
        outTarget = BackendTarget::TargetLlvmIr;
        return true;
    }
    if (value == "java" || value == "java-src") {
        outTarget = BackendTarget::TargetJavaSource;
        return true;
    }
    if (value == "ts" || value == "typescript") {
        outTarget = BackendTarget::TargetTypeScriptSource;
        return true;
    }
    return false;
}

const char* backendTargetName(BackendTarget target) {
    switch (target) {
    case BackendTarget::Target6502:
        return "6502";
    case BackendTarget::TargetLlvmIr:
        return "llvm-ir";
    case BackendTarget::TargetJavaSource:
        return "java";
    case BackendTarget::TargetTypeScriptSource:
        return "typescript";
    }
    return "unknown";
}

bool emitLlvmIrModule(
    const std::vector<AstProgram*>& programs,
    const SourceMap& map,
    const std::vector<Token>& tokens,
    DiagnosticBag& diagnostics,
    std::string& outText
) {
    LlvmEmitter emitter(map, tokens, diagnostics);
    return emitter.emit(programs, outText);
}

bool emitJavaSourceModule(
    const std::vector<AstProgram*>& programs,
    const SourceMap& map,
    const std::vector<Token>& tokens,
    DiagnosticBag& diagnostics,
    std::string& outText
) {
    JavaEmitter emitter(map, tokens, diagnostics);
    return emitter.emit(programs, outText);
}

bool emitTypeScriptModule(
    const std::vector<AstProgram*>& programs,
    const SourceMap& map,
    const std::vector<Token>& tokens,
    DiagnosticBag& diagnostics,
    std::string& outText
) {
    TsEmitter emitter(map, tokens, diagnostics);
    return emitter.emit(programs, outText);
}

} // namespace pavic

