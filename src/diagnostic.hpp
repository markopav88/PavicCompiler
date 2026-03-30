#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace pavic {

enum class DiagnosticKind {
    Error,
    Warning
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
};

class DiagnosticBag {
public:
    void addError(std::string message, SourceLocation location, std::string hint = "");
    void addWarning(std::string message, SourceLocation location, std::string hint = "");

    bool hasErrors() const;
    std::size_t errorCount() const;
    std::size_t warningCount() const;
    bool empty() const;
    const std::vector<Diagnostic>& all() const;

private:
    std::vector<Diagnostic> diagnostics_;
};

std::string formatDiagnostic(const std::string& filePath, const Diagnostic& diagnostic);

} // namespace pavic
