#include "codegen/code_buffer.hpp"

namespace pavic {
namespace codegen {

void CodeBuffer::emitU8(std::uint8_t b) {
    bytes_.push_back(b);
}

void CodeBuffer::emitAddr16LE(std::uint16_t addr) {
    bytes_.push_back(static_cast<std::uint8_t>(addr & 0xFF));
    bytes_.push_back(static_cast<std::uint8_t>((addr >> 8) & 0xFF));
}

void CodeBuffer::patchU8(std::size_t offset, std::uint8_t b) {
    bytes_.at(offset) = b;
}

} // namespace codegen
} // namespace pavic
