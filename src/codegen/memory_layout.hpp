#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <vector>

namespace pavic {
namespace codegen {

/// Stable key for a declaration slot: single-letter name + declaring scope id (matches `SymbolRecord`).
struct VarKey {
    char name = '\0';
    std::size_t declScopeId = 0;

    friend bool operator<(VarKey a, VarKey b) {
        if (a.name != b.name) {
            return a.name < b.name;
        }
        return a.declScopeId < b.declScopeId;
    }
};

/// Step 1 (substep 2): assign each variable a concrete RAM range before you emit LDA/STA addresses.
/// Start simple: one byte per variable (class subset); widen later if you need 16-bit ints.
class MemoryLayout {
public:
    explicit MemoryLayout(std::uint16_t firstFreeByte = 0);

    /// Returns the low address of `byteSize` contiguous bytes; reuses address if already allocated.
    std::uint16_t varSlot(VarKey key, std::size_t byteSize);

    /// Address previously assigned by `varSlot` (undefined if `key` was never bound).
    std::uint16_t addressOf(VarKey key) const;

    /// Append raw bytes for a zero-terminated string literal; returns the address of the first byte.
    std::uint16_t allocateStringLiteral(const std::vector<std::uint8_t>& utf8OrAsciiBytes);

    /// Scratch/temporaries (e.g. one byte for `ADC` staging). Not keyed by source name.
    std::uint16_t allocateAnonymous(std::size_t byteSize);

    std::uint16_t nextFreeAddress() const { return nextAddr_; }

private:
    std::uint16_t nextAddr_;
    std::map<VarKey, std::uint16_t> varBase_;
};

} // namespace codegen
} // namespace pavic
