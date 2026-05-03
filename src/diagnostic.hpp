#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace pavic {

class SourceMap;

/// Byte-oriented edit applied as: delete the half-open range [startByte, endByte), then insert `newText` at that position.
/// For insertion only, use startByte == endByte (empty removal range). Indices are 0-based offsets into the UTF-8 source bytes.
struct SourceRewrite {
    std::size_t startByte = 0;
    std::size_t endByte = 0;
    std::string newText;
};

enum class DiagnosticKind {
    Error,
    Warning,
    /// Non-fatal hygiene / style suggestions; do not block parse or later phases.
    Hint,
};

struct SourceLocation {
    std::size_t line = 1;
    std::size_t column = 1;
};

struct Diagnostic {
    DiagnosticKind kind = DiagnosticKind::Error;
    std::string message;
    SourceLocation location{};
    std::string hint;
    /// Optional lexer/parser rewrite that preserves meaning when applied (see course “fix-it” milestone).
    std::optional<SourceRewrite> suggestedFix;
};

class DiagnosticBag {
public:
    void addError(
        std::string message,
        SourceLocation location,
        std::string hint = "",
        std::optional<SourceRewrite> suggestedFix = std::nullopt
    );
    void addWarning(
        std::string message,
        SourceLocation location,
        std::string hint = "",
        std::optional<SourceRewrite> suggestedFix = std::nullopt
    );
    void addHint(std::string message, SourceLocation location, std::string detail = "");

    bool hasErrors() const;
    std::size_t errorCount() const;
    std::size_t warningCount() const;
    std::size_t hintCount() const;
    bool empty() const;
    const std::vector<Diagnostic>& all() const;

    /// Clears all diagnostics (used by `--fix` between re-lex / re-parse attempts).
    void clear();

private:
    std::vector<Diagnostic> diagnostics_;
};

std::string formatDiagnostic(
    const std::string& filePath,
    const Diagnostic& diagnostic,
    const SourceMap* sourceMap = nullptr
);

/// Collects `suggestedFix` rewrites from errors and warnings (hints are skipped).
std::vector<SourceRewrite> collectSuggestedFixes(const DiagnosticBag& bag);

/// Applies non-overlapping rewrites to `text` in one pass (offsets refer to `text` before this call).
/// Pure insertions at the same byte offset are merged in diagnostic order.
/// Returns false if ranges are invalid or rewrites overlap.
bool applySourceRewritesToString(std::string& text, std::vector<SourceRewrite> fixes, std::string* errorMessage);

} // namespace pavic
