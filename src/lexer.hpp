#pragma once

#include "diagnostic.hpp"
#include "source.hpp"
#include "token.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace pavic {

class Lexer {
public:
    static constexpr int kNumLexTraceStages = 7;

    Lexer(const SourceMap& map, DiagnosticBag& diagnostics, bool verbose);

    /// Tokenizes the entire source. On any lexer **error**, still completes best-effort scanning
    /// but callers must not run the parser (`diagnostics.hasErrors()`).
    void lexAll(std::vector<Token>& tokens);

private:
    const SourceMap& map_;
    DiagnosticBag& diagnostics_;
    bool verbose_;
    std::string_view text_;
    std::size_t pos_ = 0;
    std::size_t programCount_ = 1;
    bool currentProgramHasContent_ = false;

    void skipWhitespace();
    bool trySkipBlockComment();
    void lexString(std::vector<Token>& tokens);
    void lexNumber(std::vector<Token>& tokens);
    void lexWord(std::vector<Token>& tokens);

    void emitToken(std::vector<Token>& tokens, TokenKind kind, std::size_t start, std::size_t end);
    void traceToken(const Token& token) const;

    void traceStage(int stage, const std::string& message) const;
    void traceLexSummary(const std::vector<Token>& tokens) const;
    void noteProgramContent();
    void traceProgramBoundary(std::size_t endedProgramIndex, SourceLocation location) const;

    void warnTrailingEop();
};

} // namespace pavic
