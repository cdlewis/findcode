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
    
    // Check for arithmetic that outputs to $zero
    if (instr.modifiesRd() && instr.GetO32_rd() == RegisterId::GPR_O32_zero) {
        return false;
    }
    if (instr.modifiesRt() && instr.GetO32_rt() == RegisterId::GPR_O32_zero) {
        return false;
    }

    // Check for mtc0 or mfc0 with invalid registers
    if ((id == InstrId::rsp_mtc0 || id == InstrId::rsp_mfc0) && invalid_rsp_cop0_register((int)instr.GetO32_rd())) {
        return false;
    }

    // Check for nonexistent RSP instructions
    if (id == InstrId::rsp_lwc1 || id == InstrId::rsp_swc1 || id == InstrId::cpu_ctc0 || id == InstrId::cpu_cfc0 || id == InstrId::rsp_cache) {
        return false;
    }

    return true;
}

// Check if a given rom range is valid RSP microcode
bool check_range_rsp(size_t rom_start, size_t rom_end, std::span<const uint8_t> rom_bytes) {
    uint32_t prev_word = 0xFFFFFFFF;
    int identical_count = 0;
    for (size_t offset = rom_start; offset < rom_end; offset += instruction_size) {
        uint32_t cur_word = read32(rom_bytes, offset);
        // Check if the previous instruction is identical to this one
        if (cur_word == prev_word) {
            // If it is, increase the consecutive identical instruction count
            identical_count++;
        } else {
            // Otherwise, reset the count and update the previous instruction for tracking
            prev_word = cur_word;
            identical_count = 0;
        }
        rabbitizer::InstructionRsp instr{cur_word, 0};
        // See `check_range_cpu` for an explanation of this logic.
        if (identical_count >= 3 && (instr.doesLoad() || instr.doesStore())) {
            return false;
        }
        if (!is_valid_rsp(instr)) {
            return false;
        }
    }
    return true;
}
