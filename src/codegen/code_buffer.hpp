#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace pavic {
namespace codegen {

/// Step 1 (substep 3): grow-only stream of object bytes (6502 opcodes + little-endian absolute addresses).
class CodeBuffer {
public:
    void emitU8(std::uint8_t b);
    /// Write 16-bit address low byte first (e.g. `$0010` → `10 00`).
    void emitAddr16LE(std::uint16_t addr);

    std::size_t size() const { return bytes_.size(); }
    const std::vector<std::uint8_t>& bytes() const { return bytes_; }
    std::vector<std::uint8_t>& bytes() { return bytes_; }

    /// Patch a byte at `offset` (for later label back-patching).
    void patchU8(std::size_t offset, std::uint8_t b);

private:
    std::vector<std::uint8_t> bytes_;
};

} // namespace codegen
} // namespace pavic
