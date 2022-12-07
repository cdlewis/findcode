#include <array>

#include "rabbitizer.hpp"
#include "fmt/format.h"

#include "findcode.h"

struct RegisterState {
    bool initialized;
};

using GprRegisterStates = std::array<RegisterState, 32>;
using FprRegisterStates = std::array<RegisterState, 32>;

// Treat $v0 and $fv0 as an initialized register
// gcc will use these for the first uninitialized variable reference for ints and floats respectively,
// so enabling this option won't reject gcc functions that begin with a reference to an uninitialized local variable.
constexpr bool weak_uninitialized_check = true;

// Checks if an instruction has the given operand as an input
bool has_operand_input(const rabbitizer::InstructionCpu& instr, rabbitizer::OperandType operand) {
    InstrId id = instr.getUniqueId();

    // If the instruction has the given operand and doesn't modify it, then it's an input
    if (instr.hasOperandAlias(operand)) {
        switch (operand) {
            case rabbitizer::OperandType::cpu_rd:
                return !instr.modifiesRd();
            case rabbitizer::OperandType::cpu_rt:
                return !instr.modifiesRt();
            case rabbitizer::OperandType::cpu_rs:
                // rs is always an input
                return true;
            case rabbitizer::OperandType::cpu_fd:
                // fd is never an input
                return false;
            case rabbitizer::OperandType::cpu_ft:
                // ft is always an input except for lwc1 and ldc1
                return (id != InstrId::cpu_lwc1 && id != InstrId::cpu_ldc1);
            case rabbitizer::OperandType::cpu_fs:
                // fs is always an input, except for mtc1 and dmtc1
                return (id != InstrId::cpu_mtc1 && id != InstrId::cpu_dmtc1);
            default:
                return false;
        }
    }
    return false;
}

// Check if an instruction outputs to $zero
bool has_zero_output(const rabbitizer::InstructionCpu& instr) {
    RegisterId rd = instr.GetO32_rd();
    RegisterId rt = instr.GetO32_rt();

    if (instr.modifiesRd() && rd == RegisterId::GPR_O32_zero) {
        return true;
    }

    if (instr.modifiesRt() && rt == RegisterId::GPR_O32_zero) {
        return true;
    }

    return false;
}

// Checks if an instruction references an uninitialized register
bool references_uninitialized(const rabbitizer::InstructionCpu& instr, const GprRegisterStates& gpr_reg_states, const FprRegisterStates& fpr_reg_states) {
    bool ret = false;

    // Retrieve all of the possible operand registers
    int rs = (int)instr.GetO32_rs();
    int rd = (int)instr.GetO32_rd();
    int rt = (int)instr.GetO32_rt();

    int fs = (int)instr.GetO32_fs();
    int fd = (int)instr.GetO32_fd();
    int ft = (int)instr.GetO32_ft();

    // For each operand type, check if the instruction uses that operand as an input and whether the corresponding register is initialized
    if (has_operand_input(instr, rabbitizer::OperandType::cpu_rs) && !gpr_reg_states[rs].initialized) {
        ret = true;
    }

    if (has_operand_input(instr, rabbitizer::OperandType::cpu_rd) && !gpr_reg_states[rd].initialized) {
        ret = true;
    }

    if (has_operand_input(instr, rabbitizer::OperandType::cpu_rt) && !gpr_reg_states[rt].initialized) {
        ret = true;
    }

    if (has_operand_input(instr, rabbitizer::OperandType::cpu_fs) && !fpr_reg_states[fs].initialized) {
        ret = true;
    }

    if (has_operand_input(instr, rabbitizer::OperandType::cpu_fd) && !fpr_reg_states[fd].initialized) {
        ret = true;
    }

    if (has_operand_input(instr, rabbitizer::OperandType::cpu_ft) && !fpr_reg_states[ft].initialized) {
        ret = true;
    }

    return ret;
}

// Check if this instruction is (probably) invalid when at the beginning of a region of code
bool is_invalid_start_instruction(const rabbitizer::InstructionCpu& instr, const GprRegisterStates& gpr_reg_states, const FprRegisterStates& fpr_reg_states) {
    InstrId id = instr.getUniqueId();

    // Code probably won't start with a nop (some functions do, but it'll just be one nop that can be recovered later)
    if (id == InstrId::cpu_nop) {
        return true;
    }

    // Check if this is a valid instruction to begin with
    if (!is_valid(instr)) {
        return true;
    }
    
    // Code shouldn't output to $zero
    if (has_zero_output(instr)) {
        return true;
    }
    
    // Code shouldn't start with a reference to a register that isn't initialized
    if (references_uninitialized(instr, gpr_reg_states, fpr_reg_states)) {
        return true;
    }

    // Code shouldn't start with an unconditional branch
    if (id == InstrId::cpu_b || id == InstrId::cpu_j) {
        return true;
    }

    // Code shouldn't start with a linked jump, as it'd need to save the return address first
    if (id == InstrId::cpu_jal || id == InstrId::cpu_jalr) {
        return true;
    }

    // Code shouldn't jump to $zero
    if (id == InstrId::cpu_jr && instr.GetO32_rs() == RegisterId::GPR_O32_zero) {
        return true;
    }

    // Shifts with $zero as the input and a non-zero shift amount are likely not real code
    if (id == InstrId::cpu_sll || id == InstrId::cpu_srl || id == InstrId::cpu_sra ||
        id == InstrId::cpu_dsll || id == InstrId::cpu_dsll32 || id == InstrId::cpu_dsrl ||
        id == InstrId::cpu_dsrl32 || id == InstrId::cpu_dsra || id == InstrId::cpu_dsra32) {
        // fmt::print("test {} {} {}\n", (int)id, (int)instr.GetO32_rt(), instr.Get_sa());
        if (instr.GetO32_rt() == RegisterId::GPR_O32_zero && instr.Get_sa() != 0) {
            return true;
        }
    }

    // Code probably won't start with mthi or mtlo
    if (id == InstrId::cpu_mthi || id == InstrId::cpu_mtlo) {
        return true;
    }
    
    // Code shouldn't start with branches based on the cop1 condition flag (it won't have been set yet)
    if (id == InstrId::cpu_bc1t || id == InstrId::cpu_bc1f || id == InstrId::cpu_bc1tl || id == InstrId::cpu_bc1fl) {
        return true;
    }

    // Add and sub are good indicators that the bytes aren't actually instructions, since addu and subu would normally be used
    if (id == InstrId::cpu_add || id == InstrId::cpu_sub) {
        return true;
    }

    return false;
}

// Count the number of instructions at the beginning of a region with uninitialized register references
size_t count_invalid_start_instructions(const RomRegion& region, std::span<const uint8_t> rom_bytes) {
    GprRegisterStates gpr_reg_states{};
    FprRegisterStates fpr_reg_states{};

    // GPRs
    
    // Zero is always initialized (it's zero)
    gpr_reg_states[(size_t)RegisterId::GPR_O32_zero].initialized = true;

    // The stack pointer and return address always initialized
    gpr_reg_states[(size_t)RegisterId::GPR_O32_sp].initialized = true;
    gpr_reg_states[(size_t)RegisterId::GPR_O32_ra].initialized = true;

    // Treat all arg registers as initialized
    gpr_reg_states[(size_t)RegisterId::GPR_O32_a0].initialized = true;
    gpr_reg_states[(size_t)RegisterId::GPR_O32_a1].initialized = true;
    gpr_reg_states[(size_t)RegisterId::GPR_O32_a2].initialized = true;
    gpr_reg_states[(size_t)RegisterId::GPR_O32_a3].initialized = true;

    // Treat $v0 as initialized for gcc if enabled
    if (weak_uninitialized_check) {
        gpr_reg_states[(size_t)RegisterId::GPR_O32_v0].initialized = true;
    }
    
    // FPRs

    // Treat all arg registers as initialized
    fpr_reg_states[(size_t)FprRegisterId::COP1_O32_fa0];
    fpr_reg_states[(size_t)FprRegisterId::COP1_O32_fa0f];
    fpr_reg_states[(size_t)FprRegisterId::COP1_O32_fa1];
    fpr_reg_states[(size_t)FprRegisterId::COP1_O32_fa1f];

    // Treat $fv0 as initialized for gcc if enabled
    if (weak_uninitialized_check) {
        fpr_reg_states[(size_t)FprRegisterId::COP1_O32_fv0];
        fpr_reg_states[(size_t)FprRegisterId::COP1_O32_fv0f];
    }

    size_t instr_index = 0;

    while (true) {
        uint32_t instr_word = read32(rom_bytes, instruction_size * instr_index + region.rom_start);
        rabbitizer::InstructionCpu instr{instr_word, 0};

        if (!is_invalid_start_instruction(instr, gpr_reg_states, fpr_reg_states)) {
            break;
        }

        instr_index++;
    }

    return instr_index;
}
