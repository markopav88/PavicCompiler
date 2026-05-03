#include "diagnostic.hpp"

#include "source.hpp"

#include <algorithm>
#include <cstdio>
#include <sstream>
#include <string_view>

namespace pavic {

namespace {

std::string escapeForDiagnostic(std::string_view raw) {
    std::ostringstream oss;
    for (unsigned char ch : raw) {
        if (ch == '\n') {
            oss << "\\n";
        } else if (ch == '\r') {
            oss << "\\r";
        } else if (ch == '\t') {
            oss << "\\t";
        } else if (ch == '\\') {
            oss << "\\\\";
        } else if (ch == '`') {
            oss << "\\`";
        } else if (ch >= 32 && ch < 127) {
            oss << static_cast<char>(ch);
        } else {
            char buf[8];
            std::snprintf(buf, sizeof(buf), "\\x%02x", static_cast<unsigned>(ch));
            oss << buf;
        }
    }
    return oss.str();
}

void appendSuggestedFixText(std::ostringstream& output, const Diagnostic& diagnostic, const SourceMap& map) {
    if (!diagnostic.suggestedFix.has_value()) {
        return;
    }
    const SourceRewrite& fix = *diagnostic.suggestedFix;
    const std::string_view text = map.text();
    const std::size_t len = text.size();
    std::size_t a = fix.startByte;
    std::size_t b = fix.endByte;
    if (a > len) {
        a = len;
    }
    if (b > len) {
        b = len;
    }
    if (a > b) {
        output << "\n  suggested fix: (internal: invalid byte range; ignored)";
        return;
    }

    const std::string_view removed = text.substr(a, b - a);
    output << "\n  suggested fix: ";
    if (a == b) {
        output << "insert at byte offset " << a << ": `" << escapeForDiagnostic(fix.newText) << "`";
    } else {
        output << "replace bytes [" << a << ", " << b << ") (`" << escapeForDiagnostic(removed) << "`) with `"
               << escapeForDiagnostic(fix.newText) << "`";
    }
}

} // namespace

void DiagnosticBag::addError(
    std::string message,
    SourceLocation location,
    std::string hint,
    std::optional<SourceRewrite> suggestedFix
) {
    diagnostics_.push_back(Diagnostic{
        DiagnosticKind::Error,
        std::move(message),
        location,
        std::move(hint),
        std::move(suggestedFix),
    });
}

void DiagnosticBag::addWarning(
    std::string message,
    SourceLocation location,
    std::string hint,
    std::optional<SourceRewrite> suggestedFix
) {
    diagnostics_.push_back(Diagnostic{
        DiagnosticKind::Warning,
        std::move(message),
        location,
        std::move(hint),
        std::move(suggestedFix),
    });
}

void DiagnosticBag::addHint(std::string message, SourceLocation location, std::string detail) {
    diagnostics_.push_back(Diagnostic{
        DiagnosticKind::Hint,
        std::move(message),
        location,
        std::move(detail),
        std::nullopt,
    });
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

std::size_t DiagnosticBag::hintCount() const {
    std::size_t count = 0;
    for (const auto& diagnostic : diagnostics_) {
        if (diagnostic.kind == DiagnosticKind::Hint) {
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

void DiagnosticBag::clear() {
    diagnostics_.clear();
}

std::vector<SourceRewrite> collectSuggestedFixes(const DiagnosticBag& bag) {
    std::vector<SourceRewrite> out;
    for (const Diagnostic& d : bag.all()) {
        if (d.kind != DiagnosticKind::Error && d.kind != DiagnosticKind::Warning) {
            continue;
        }
        if (d.suggestedFix.has_value()) {
            out.push_back(*d.suggestedFix);
        }
    }
    return out;
}

bool applySourceRewritesToString(std::string& text, std::vector<SourceRewrite> fixes, std::string* errorMessage) {
    const std::string orig = text;
    const std::size_t n = orig.size();

    for (const SourceRewrite& f : fixes) {
        if (f.startByte > n || f.endByte > n || f.startByte > f.endByte) {
            if (errorMessage != nullptr) {
                *errorMessage = "source rewrite range is out of bounds for the current source buffer";
            }
            return false;
        }
    }

    if (fixes.empty()) {
        return true;
    }

    std::sort(
        fixes.begin(),
        fixes.end(),
        [](const SourceRewrite& a, const SourceRewrite& b) {
            if (a.startByte != b.startByte) {
                return a.startByte < b.startByte;
            }
            if (a.endByte != b.endByte) {
                return a.endByte < b.endByte;
            }
            return a.newText < b.newText;
        }
    );

    // Merge pure insertions at the same offset into one rewrite (preserve order).
    std::vector<SourceRewrite> merged;
    for (const SourceRewrite& f : fixes) {
        if (!merged.empty()) {
            SourceRewrite& back = merged.back();
            if (back.startByte == f.startByte && back.endByte == f.endByte && back.startByte == back.endByte && f.startByte == f.endByte) {
                back.newText += f.newText;
                continue;
            }
        }
        merged.push_back(f);
    }

    // Reject overlaps in original coordinates: a zero-length insert at `s` reserves byte position `s`
    // (no other rewrite may start before `s + 1`). Deletions use the usual half-open [start, end).
    std::size_t guard = 0;
    for (const SourceRewrite& f : merged) {
        if (f.startByte < guard) {
            if (errorMessage != nullptr) {
                *errorMessage = "overlapping source rewrites; apply fixes one at a time or resolve manually";
            }
            return false;
        }
        if (f.startByte == f.endByte) {
            guard = std::max(guard, f.startByte + 1);
        } else {
            guard = std::max(guard, f.endByte);
        }
    }

    std::string out;
    out.reserve(orig.size() + merged.size() * 4);
    std::size_t cur = 0;
    for (const SourceRewrite& f : merged) {
        if (f.startByte < cur) {
            if (errorMessage != nullptr) {
                *errorMessage = "inconsistent rewrite ordering";
            }
            return false;
        }
        out.append(orig, cur, f.startByte - cur);
        out += f.newText;
        cur = f.endByte;
    }
    out.append(orig, cur, std::string::npos);
    text = std::move(out);
    return true;
}

std::string formatDiagnostic(const std::string& filePath, const Diagnostic& diagnostic, const SourceMap* sourceMap) {
    std::ostringstream output;
    const char* kindText = "error";
    if (diagnostic.kind == DiagnosticKind::Warning) {
        kindText = "warning";
    } else if (diagnostic.kind == DiagnosticKind::Hint) {
        kindText = "hint";
    }

    output << filePath << ":" << diagnostic.location.line << ":" << diagnostic.location.column << ": "
           << kindText << ": " << diagnostic.message;

    if (!diagnostic.hint.empty()) {
        if (diagnostic.kind == DiagnosticKind::Hint) {
            output << "\n  detail: " << diagnostic.hint;
        } else {
            output << "\n  hint: " << diagnostic.hint;
        }
    }

    if (sourceMap != nullptr && diagnostic.suggestedFix.has_value()) {
        appendSuggestedFixText(output, diagnostic, *sourceMap);
    }

    return output.str();
}

} // namespace pavic
