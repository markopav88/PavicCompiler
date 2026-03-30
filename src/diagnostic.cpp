#include "diagnostic.hpp"

#include <sstream>

namespace pavic {

void DiagnosticBag::addError(std::string message, SourceLocation location, std::string hint) {
    diagnostics_.push_back(Diagnostic{DiagnosticKind::Error, std::move(message), location, std::move(hint)});
}

void DiagnosticBag::addWarning(std::string message, SourceLocation location, std::string hint) {
    diagnostics_.push_back(Diagnostic{DiagnosticKind::Warning, std::move(message), location, std::move(hint)});
}

bool DiagnosticBag::hasErrors() const {
    for (const auto& diagnostic : diagnostics_) {
        if (diagnostic.kind == DiagnosticKind::Error) {
            return true;
        }
    }
    return false;
}

std::size_t DiagnosticBag::errorCount() const {
    std::size_t count = 0;
    for (const auto& diagnostic : diagnostics_) {
        if (diagnostic.kind == DiagnosticKind::Error) {
            ++count;
        }
    }
    return count;
}

std::size_t DiagnosticBag::warningCount() const {
    std::size_t count = 0;
    for (const auto& diagnostic : diagnostics_) {
        if (diagnostic.kind == DiagnosticKind::Warning) {
            ++count;
        }
    }
    return count;
}

bool DiagnosticBag::empty() const {
    return diagnostics_.empty();
}

const std::vector<Diagnostic>& DiagnosticBag::all() const {
    return diagnostics_;
}

std::string formatDiagnostic(const std::string& filePath, const Diagnostic& diagnostic) {
    std::ostringstream output;
    const char* kindText = diagnostic.kind == DiagnosticKind::Error ? "error" : "warning";

    output << filePath << ":" << diagnostic.location.line << ":" << diagnostic.location.column << ": "
           << kindText << ": " << diagnostic.message;

    if (!diagnostic.hint.empty()) {
        output << "\n  hint: " << diagnostic.hint;
    }

    return output.str();
}

} // namespace pavic
