#pragma once

#include "diagnostic.hpp"

#include <map>
#include <ostream>
#include <string>
#include <vector>

namespace pavic {

/// One row for display and later type-checking: name, declared type string, nesting depth, position.
struct SymbolRecord {
    char name = '\0';
    std::string declType;
    /// 0 = outermost block of the program, 1 = first nested `{ }`, etc.
    std::size_t scopeDepth = 0;
    SourceLocation declaredAt{};
};

/// Stack of lexical scopes (`{` … `}`). Innermost map is the current block.
class ScopeStack {
public:
    void pushScope();
    void popScope();

    /// Declares `name` in the innermost scope. Returns false if `name` is already declared there.
    bool tryDeclare(char name, std::string declType, SourceLocation loc);

    /// Walks inner → outer; returns nullptr if not found.
    const SymbolRecord* lookup(char name) const;

    std::size_t scopeCount() const { return scopes_.size(); }

    /// Declarations in first-seen order (for printing the symbol table).
    const std::vector<SymbolRecord>& orderedDeclarations() const { return ordered_; }

private:
    std::vector<std::map<char, SymbolRecord>> scopes_;
    std::vector<SymbolRecord> ordered_;
};

void printSymbolTable(std::ostream& os, const std::vector<SymbolRecord>& symbols);

} // namespace pavic
