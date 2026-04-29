#include "codegen/memory_layout.hpp"

namespace pavic {
namespace codegen {

MemoryLayout::MemoryLayout(std::uint16_t firstFreeByte) : nextAddr_(firstFreeByte) {}

std::uint16_t MemoryLayout::varSlot(VarKey key, std::size_t byteSize) {
    const auto it = varBase_.find(key);
    if (it != varBase_.end()) {
        return it->second;
    }
    const std::uint16_t base = nextAddr_;
    nextAddr_ = static_cast<std::uint16_t>(nextAddr_ + byteSize);
    varBase_.emplace(key, base);
    return base;
}

std::uint16_t MemoryLayout::addressOf(VarKey key) const {
    return varBase_.at(key);
}

std::uint16_t MemoryLayout::allocateStringLiteral(const std::vector<std::uint8_t>& bytes) {
    const std::uint16_t base = nextAddr_;
    // Reserve space for payload + trailing $00 (syscall $02). Actual byte emission happens in a later phase.
    nextAddr_ = static_cast<std::uint16_t>(nextAddr_ + bytes.size() + 1);
    return base;
}

std::uint16_t MemoryLayout::allocateAnonymous(std::size_t byteSize) {
    const std::uint16_t base = nextAddr_;
    nextAddr_ = static_cast<std::uint16_t>(nextAddr_ + byteSize);
    return base;
}

} // namespace codegen
} // namespace pavic
