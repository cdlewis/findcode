#ifndef __FINDCODE_H__
#define __FINDCODE_H__

#include <cstdint>
#include <vector>
#include <span>

#include "rabbitizer.hpp"

struct RomRegion {
    size_t rom_start;
    size_t rom_end;
    bool has_rsp;

    RomRegion(size_t new_rom_start, size_t new_rom_end) :
        rom_start(new_rom_start), rom_end(new_rom_end), has_rsp(false) {}
};

constexpr size_t instruction_size = 4;
constexpr size_t min_region_instructions = 4;
constexpr size_t microcode_check_threshold = 1024 * instruction_size;
constexpr bool show_true_ranges = false;

using RegisterId = rabbitizer::Registers::Cpu::GprO32;
using FprRegisterId = rabbitizer::Registers::Cpu::Cop1O32;
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
inline uint32_t read32(std::span<const uint8_t> bytes, size_t offset) {
    return *reinterpret_cast<const uint32_t*>(bytes.data() + offset);
}

// Find all the regions of code in the given rom
std::vector<RomRegion> find_code_regions(std::span<const uint8_t> rom_bytes);

// // Check if a given CPU instruction is valid
bool is_valid(const rabbitizer::InstructionCpu& instr);

// Check if a given RSP instruction is valid via several metrics
bool is_valid_rsp(const rabbitizer::InstructionRsp& instr);

// Check if a given rom range is valid RSP microcode
bool check_range_rsp(size_t rom_start, size_t rom_end, std::span<const uint8_t> rom_bytes);

// Count the number of instructions at the beginning of a region with uninitialized register references
size_t count_invalid_start_instructions(const RomRegion& region, std::span<const uint8_t> rom_bytes);

// Check if a given instruction outputs to $zero
bool has_zero_output(const rabbitizer::InstructionCpu& instr);

#endif
