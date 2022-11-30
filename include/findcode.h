#ifndef __FINDCODE_H__
#define __FINDCODE_H__

#include <cstdint>
#include <vector>
#include <span>

struct RomRegion {
    size_t rom_start;
    size_t rom_end;

    RomRegion(size_t new_rom_start, size_t new_rom_end) :
        rom_start(new_rom_start), rom_end(new_rom_end) {}
};

constexpr size_t instruction_size = 4;
constexpr size_t min_region_instructions = 4;
constexpr size_t microcode_check_threshold = 1024 * instruction_size;

using RegisterId = rabbitizer::Registers::Cpu::GprO32;
using InstrId = rabbitizer::InstrId::UniqueId;

// Byteswap a 32-bit value
#ifdef _MSC_VER
inline uint32_t byteswap(uint32_t val) {
    return _byteswap_ulong(val);
}
#else
constexpr uint32_t byteswap(uint32_t val) {
    return __builtin_bswap32(val);
}
#endif

// Nearest multiple of `divisor` greater than or equal to `val`
template <size_t divisor>
constexpr size_t nearest_multiple_up(size_t val) {
    return ((val + divisor - 1) / divisor) * divisor;
}

// Nearest multiple of `divisor` less than or equal to `val`
template <size_t divisor>
constexpr size_t nearest_multiple_down(size_t val) {
    return (val / divisor) * divisor;
}

// Reads a 32-bit value from a given uint8_t span at the given offset
inline uint32_t read32(std::span<uint8_t> bytes, size_t offset) {
    return *reinterpret_cast<uint32_t*>(bytes.data() + offset);
}

// // Check if a given CPU instruction is valid
bool is_valid(const rabbitizer::InstructionCpu& instr);

// Check if a given rom range is valid RSP microcode
bool check_range_rsp(size_t rom_start, size_t rom_end, std::span<uint8_t> rom_bytes);

#endif
