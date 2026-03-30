#pragma once

#include "diagnostic.hpp"

#include <cstddef>
#include <string_view>
#include <vector>

namespace pavic {

/// Maps byte offsets in UTF-8 / byte-oriented source to 1-based line and column.
/// Column counts bytes from the line start (appropriate for ASCII classroom grammars).
class SourceMap {
public:
    explicit SourceMap(std::string_view text);

    std::string_view text() const { return text_; }
    std::size_t byteLength() const { return text_.size(); }
    std::size_t lineCount() const { return lineStarts_.size(); }

    /// Byte offset may be in [0, byteLength()]; values past the end clamp to EOF.
    SourceLocation locationAt(std::size_t byteOffset) const;

private:
    std::string_view text_;
    std::vector<std::size_t> lineStarts_;
};

} // namespace pavic
