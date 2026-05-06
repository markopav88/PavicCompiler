#pragma once

#include <cstdint>

namespace pavic {
namespace codegen {

/// How to emit the OS “system call” site. 
// 
// Class notes: SvegOS accepts `FF SYS`; the e-tradition
/// emulator may need `EA` (NOP) substituted in the object stream instead.
enum class SysCallEncoding {
    InstructionFF,
    EmulatorNopEA,
};

/// Global knobs for later phases (codegen driver passes this through).
struct CodeGenTarget {
    SysCallEncoding sysEncoding = SysCallEncoding::InstructionFF;
    /// First RAM byte available for your static layout (variables, temps, string pools).
    std::uint16_t ramBase = 0x0000;
    /// Keep static data at or above this address -temp table region starts away from opcode stream).
    std::uint16_t minDataBase = 0x0036;
    /// Emit fixed-size program image bytes (0 disables padding).
    std::uint16_t programImageBytes = 0x0100;
};

} // namespace codegen
} // namespace pavic
