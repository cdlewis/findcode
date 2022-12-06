#include "rabbitizer.hpp"
#include "fmt/format.h"

#include "findcode.h"

// Check if the provided cop0 register index is valid for the RSP
bool invalid_rsp_cop0_register(int reg) {
    return reg > 15;
}

// Check if a given RSP instruction is valid via several metrics
bool is_valid_rsp(const rabbitizer::InstructionRsp& instr) {
    InstrId id = instr.getUniqueId();
    // Check for instructions with invalid opcodes
    if (id == InstrId::rsp_INVALID) {
        return false;
    }
    
    // Check for instructions with invalid bits
    if (!instr.isValid()) {
        // Make sure this isn't a special jr with 
        return false;
    }

    // Check for mtc0 or mfc0 with invalid registers
    if ((id == InstrId::rsp_mtc0 || id == InstrId::rsp_mfc0) && invalid_rsp_cop0_register((int)instr.GetO32_rd())) {
        return false;
    }

    // Check for nonexistent RSP instructions
    if (id == InstrId::rsp_lwc1 || id == InstrId::rsp_swc1) {
        return false;
    }

    return true;
}

// Check if a given rom range is valid RSP microcode
bool check_range_rsp(size_t rom_start, size_t rom_end, std::span<uint8_t> rom_bytes) {
    // fmt::print("Test: 0x{:08X} - 0x{:08X}\n", rom_start, rom_end);
    for (size_t offset = rom_start; offset < rom_end; offset += instruction_size) {
        rabbitizer::InstructionRsp instr{read32(rom_bytes, offset), 0};
        if (!is_valid_rsp(instr)) {
            if (rom_start == 0x00B96390) {
                fmt::print(stderr, "  Invalid RSP: {}\n", instr.disassemble(0));
            }
            return false;
        }
    }
    return true;
}
