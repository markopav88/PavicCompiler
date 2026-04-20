#include "symbol_table.hpp"

#include <iomanip>

namespace pavic {

void ScopeStack::pushScope() {
    scopes_.emplace_back();
    scopeIds_.push_back(nextScopeId_++);
}

void ScopeStack::popScope() {
    if (!scopes_.empty()) {
        scopes_.pop_back();
        scopeIds_.pop_back();
    }
}

bool ScopeStack::tryDeclare(char name, std::string declType, SourceLocation loc) {
    if (scopes_.empty()) {
        pushScope();
    }
    const std::size_t scopeDepth = scopes_.size() - 1;
    const std::size_t scopeId = scopeIds_.back();
    std::map<char, SymbolRecord>& inner = scopes_.back();
    if (inner.count(name) != 0) {
        return false;
    }
    SymbolRecord rec;
    rec.name = name;
    rec.declType = std::move(declType);
    rec.scopeId = scopeId;
    rec.scopeDepth = scopeDepth;
    rec.declaredAt = loc;
    inner[name] = rec;
    ordered_.push_back(rec);
    return true;
}

const SymbolRecord* ScopeStack::lookup(char name) const {
    for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it) {
        const auto found = it->find(name);
        if (found != it->end()) {
            return &found->second;
        }
    }
    return nullptr;
}

void printSymbolTable(std::ostream& os, const std::vector<SymbolRecord>& symbols) {
    if (symbols.empty()) {
        os << "(no declarations)\n";
        return;
    }
    os << std::left << std::setw(8) << "name" << std::setw(12) << "type" << std::setw(10) << "scope id" << std::setw(14) << "scope depth"
       << "declared at\n";
    for (const SymbolRecord& s : symbols) {
        os << std::setw(8) << std::string(1, s.name) << std::setw(12) << s.declType << std::setw(10) << s.scopeId << std::setw(14) << s.scopeDepth
           << s.declaredAt.line << ":" << s.declaredAt.column << "\n";
    }
}

} // namespace pavic
