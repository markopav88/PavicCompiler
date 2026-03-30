#include "source.hpp"

#include <algorithm>

namespace pavic {

SourceMap::SourceMap(std::string_view text) : text_(text) {
    lineStarts_.push_back(0);
    for (std::size_t i = 0; i < text_.size(); ++i) {
        if (text_[i] == '\n') {
            lineStarts_.push_back(i + 1);
        }
    }
}

SourceLocation SourceMap::locationAt(std::size_t byteOffset) const {
    if (text_.empty()) {
        return {1, 1};
    }

    if (byteOffset > text_.size()) {
        byteOffset = text_.size();
    }

    auto afterLine = std::upper_bound(lineStarts_.begin(), lineStarts_.end(), byteOffset);
    const std::size_t lineIndex = static_cast<std::size_t>(afterLine - lineStarts_.begin());
    const std::size_t lineStart = lineStarts_[lineIndex - 1];
    const std::size_t column = byteOffset - lineStart + 1;

    return SourceLocation{lineIndex, column};
}

} // namespace pavic
