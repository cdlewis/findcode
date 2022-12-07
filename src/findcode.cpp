#include <cstdint>
#include <vector>
#include <span>

#include "rabbitizer.hpp"
#include "fmt/format.h"

#include "findcode.h"

constexpr uint32_t jr_ra = 0x03E00008;

// Search a span for any instances of the instruction `jr $ra`
std::vector<size_t> find_return_locations(std::span<const uint8_t> rom_bytes) {
    std::vector<size_t> ret{};
    ret.reserve(1024);

    for (size_t rom_addr = 0x1000; rom_addr < rom_bytes.size() / instruction_size; rom_addr += instruction_size) {
        uint32_t rom_word = *reinterpret_cast<const uint32_t*>(rom_bytes.data() + rom_addr);

        if (rom_word == jr_ra) {
            ret.push_back(rom_addr);
        }
    }

    return ret;
}

// Check if a given instruction id is a CPU store
bool is_store(InstrId id) {
    return
        id == InstrId::cpu_sb ||
        id == InstrId::cpu_sh ||
        id == InstrId::cpu_sw ||
        id == InstrId::cpu_sd ||
        id == InstrId::cpu_swc1 || 
        id == InstrId::cpu_sdc1;
}

// Check if a given instruction id is a CPU load
bool is_gpr_load(InstrId id) {
    return
        id == InstrId::cpu_lb ||
        id == InstrId::cpu_lbu || 
        id == InstrId::cpu_lh ||
        id == InstrId::cpu_lhu || 
        id == InstrId::cpu_lw ||
        id == InstrId::cpu_lwu || 
        id == InstrId::cpu_ld;
}

bool is_fpr_load(InstrId id) {
    return
        id == InstrId::cpu_lwc1 || 
        id == InstrId::cpu_ldc1;
}

// Check if the provided cop0 register index is valid
bool invalid_cop0_register(int reg) {
    return reg == 7 || (reg >= 21 && reg <= 25) || reg == 31;
}

bool is_unused_n64_instruction(InstrId id) {
    return
        id == InstrId::cpu_ll ||
        id == InstrId::cpu_sc ||
        id == InstrId::cpu_lld ||
        id == InstrId::cpu_scd ||
        id == InstrId::cpu_syscall;
}

// Check if a given instruction is valid via several metrics
bool is_valid(const rabbitizer::InstructionCpu& instr) {
    InstrId id = instr.getUniqueId();
    // Check for instructions with invalid bits or invalid opcodes
    if (!instr.isValid() || id == InstrId::cpu_INVALID) {
        return false;
    }

    bool instr_is_store = is_store(id);
    bool instr_is_gpr_load = is_gpr_load(id);
    bool instr_is_fpr_load = is_fpr_load(id);

    // Check for loads or stores with an offset from $zero
    if ((instr_is_store || instr_is_gpr_load || instr_is_fpr_load) && instr.GetO32_rs() == RegisterId::GPR_O32_zero) {
        return false;
    }

    // This check is disabled as some compilers can generate load to $zero for a volatile dereference
    // // Check for loads to $zero
    // if (instr_is_gpr_load && instr.GetO32_rt() == RegisterId::GPR_O32_zero) {
    //     return false;
    // }

    // Check for mtc0 or mfc0 with invalid registers
    if ((id == InstrId::cpu_mtc0 || id == InstrId::cpu_mfc0) && invalid_cop0_register((int)instr.GetO32_rd())) {
        return false;
    }

    // Check for instructions that wouldn't be in an N64 game, despite being valid
    if (is_unused_n64_instruction(id)) {
        return false;
    }

    // Check for cache instructions with invalid parameters
    if (id == InstrId::cpu_cache) {
        uint32_t cache_param = instr.Get_op();
        uint32_t cache_op = cache_param >> 2;
        uint32_t cache_type = cache_param & 0x3;

        // Only cache operations 0-6 and cache types 0-1 are valid
        if (cache_op > 6 || cache_type > 1) {
            return false;
        }
    }

    // Check for cop2 instructions, which are invalid for the N64's CPU
    if (id == InstrId::cpu_lwc2 || id == InstrId::cpu_ldc2 || id == InstrId::cpu_swc2 || id == InstrId::cpu_sdc2) {
        return false;
    }

    // Check for trap instructions
    if (id >= InstrId::cpu_tge && id <= InstrId::cpu_tltu) {
        return false;
    }

    // Check for ctc0 and cfc0, which aren't valid on the N64
    if (id == InstrId::cpu_ctc0 || id == InstrId::cpu_cfc0) {
        return false;
    }

    // Check for instructions that don't exist on the N64's CPU
    if (id == InstrId::cpu_pref) {
        return false;
    }

    return true;
}

// Searches backwards from the given rom address until it hits an invalid instruction
size_t find_code_start(std::span<const uint8_t> rom_bytes, size_t rom_addr) {
    while (rom_addr > 0x1000) {
        size_t cur_rom_addr = rom_addr - instruction_size;
        rabbitizer::InstructionCpu cur_instr{read32(rom_bytes, cur_rom_addr), 0};

        if (!is_valid(cur_instr)) {
            return rom_addr;
        }

        rom_addr = cur_rom_addr;
    }

    return rom_addr;
}

// Searches forwards from the given rom address until it hits an invalid instruction
size_t find_code_end(std::span<const uint8_t> rom_bytes, size_t rom_addr) {
    while (rom_addr > 0) {
        rabbitizer::InstructionCpu cur_instr{read32(rom_bytes, rom_addr), 0};

        if (!is_valid(cur_instr)) {
            return rom_addr;
        }

        rom_addr += instruction_size;
    }

    return rom_addr;
}

// Check if a given instruction word is an unconditional non-linking branch (i.e. `b`, `j`, or `jr`)
bool is_unconditional_branch(uint32_t instruction_word) {
    rabbitizer::InstructionCpu instr{instruction_word, 0};

    return instr.isUnconditionalBranch() || instr.getUniqueId() == rabbitizer::InstrId::UniqueId::cpu_jr;
}

// Trims zeroes from the start of a code region and "loose" instructions from the end
void trim_segment(RomRegion& codeseg, std::span<const uint8_t> rom_bytes) {
    size_t start = codeseg.rom_start;
    size_t end = codeseg.rom_end;
    size_t invalid_start_count = count_invalid_start_instructions(codeseg, rom_bytes);

    start += invalid_start_count * instruction_size;
    
    // Remove leading nops
    while (read32(rom_bytes, start) == 0 && end > start) {
        start += instruction_size;
    }
    
    // Any instruction that isn't eventually followed by an unconditional non-linking branch (b, j, jr) would run into
    // invalid code, so scan backwards until we see an unconditional branch and remove anything after it.
    // Scan two instructions back (8 bytes before the end) instead of one to include the delay slot.
    while (!is_unconditional_branch(read32(rom_bytes, end - 2 * instruction_size)) && end > start) {
        end -= instruction_size;
    }
    
    codeseg.rom_start = start;
    codeseg.rom_end = end;
}

// Check if a given rom range is valid CPU instructions
bool check_range_cpu(size_t rom_start, size_t rom_end, std::span<const uint8_t> rom_bytes) {
    for (size_t offset = rom_start; offset < rom_end; offset += instruction_size) {
        rabbitizer::InstructionCpu instr{read32(rom_bytes, offset), 0};
        if (!is_valid(instr)) {
            return false;
        }
    }
    return true;
}

// Find all the regions of code in the given rom
std::vector<RomRegion> find_code_regions(std::span<const uint8_t> rom_bytes) {
    std::vector<RomRegion> ret{};
    
    std::vector<size_t> return_addrs = find_return_locations(rom_bytes);

    auto it = return_addrs.begin();
    while (it != return_addrs.end()) {
        size_t region_start = find_code_start(rom_bytes, *it);
        size_t region_end = find_code_end(rom_bytes, *it);
        RomRegion& cur_segment = ret.emplace_back(region_start, region_end);
        
        while (it != return_addrs.end() && *it < cur_segment.rom_end) {
            it++;
        }
        
        trim_segment(cur_segment, rom_bytes);
        
        // If the current segment is close enough to the previous segment, check if there's valid RSP microcode between the two
        if (ret.size() > 1 && cur_segment.rom_start - ret[ret.size() - 2].rom_end < microcode_check_threshold) {
            // Check if there's a range of valid CPU instructions between these two segments
            bool valid_range = check_range_cpu(ret[ret.size() - 2].rom_end, cur_segment.rom_start, rom_bytes);
            // If there isn't check for RSP instructions
            if (!valid_range) {
                valid_range = check_range_rsp(ret[ret.size() - 2].rom_end, cur_segment.rom_start, rom_bytes);
                // If RSP instructions were found, mark the first segment as having RSP instructions
                if (valid_range) {
                    ret[ret.size() - 2].has_rsp = true;
                }
            }
            if (valid_range) {
                // If there is, merge the two segments
                size_t new_end = cur_segment.rom_end;
                ret.pop_back();
                ret.back().rom_end = new_end;
            }
        }

        // // If the segment has fewer than the minimum instructions, throw it out.
        // if (cur_segment.rom_end - cur_segment.rom_start < min_region_instructions * instruction_size) {
        //     ret.pop_back();
        // }
    }

    return ret;
}
