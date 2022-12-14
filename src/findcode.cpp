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

    for (size_t rom_addr = 0x1000; rom_addr < rom_bytes.size(); rom_addr += instruction_size) {
        uint32_t rom_word = *reinterpret_cast<const uint32_t*>(rom_bytes.data() + rom_addr);

        if (rom_word == jr_ra) {
            // Found a jr $ra, make sure the delay slot is also a valid instruction and if so mark this as a code region
            uint32_t next_word = *reinterpret_cast<const uint32_t*>(rom_bytes.data() + rom_addr + instruction_size);

            // This may be microcode, so check instruction validity for both CPU and RSP
            rabbitizer::InstructionCpu next_instr_cpu{next_word, 0};
            rabbitizer::InstructionRsp next_instr_rsp{next_word, 0};
            if (is_valid(next_instr_cpu) || is_valid_rsp(next_instr_rsp)) {
                ret.push_back(rom_addr);
            }
        }
    }

    return ret;
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

    bool instr_is_store = instr.doesStore();
    bool instr_is_gpr_load = instr.doesLoad() && !instr.isFloat();
    bool instr_is_fpr_load = instr.doesLoad() && instr.isFloat();

    // Check for loads or stores with an offset from $zero
    if ((instr_is_store || instr_is_gpr_load || instr_is_fpr_load) && instr.GetO32_rs() == RegisterId::GPR_O32_zero) {
        return false;
    }

    // This check is disabled as some compilers can generate load to $zero for a volatile dereference
    // // Check for loads to $zero
    // if (instr_is_gpr_load && instr.GetO32_rt() == RegisterId::GPR_O32_zero) {
    //     return false;
    // }

    // Check for arithmetic that outputs to $zero
    if (instr.modifiesRd() && instr.GetO32_rd() == RegisterId::GPR_O32_zero) {
        return false;
    }
    if (instr.modifiesRt() && instr.GetO32_rt() == RegisterId::GPR_O32_zero) {
        return false;
    }

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
    if (instr.isTrap()) {
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
    InstrId id = instr.getUniqueId();

    return id == InstrId::cpu_b || id == InstrId::cpu_j || id == InstrId::cpu_jr;
}

// Trims zeroes from the start of a code region and "loose" instructions from the end
void trim_region(RomRegion& codeseg, std::span<const uint8_t> rom_bytes) {
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
        rabbitizer::InstructionCpu instr{cur_word, 0};
        // If there are 3 identical loads or stores in a row, it's not likely to be real code
        // Use 3 as the count because 2 could be plausible if it's a duplicated instruction by the compiler.
        // Only check for loads and stores because arithmetic could be duplicated to avoid more expensive operations,
        // e.g. x + x + x instead of 3 * x. 
        if (identical_count >= 3 && (instr.doesLoad() || instr.doesStore())) {
            return false;
        }
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
        ret.emplace_back(region_start, region_end);
        
        while (it != return_addrs.end() && *it < ret.back().rom_end) {
            it++;
        }
        
        trim_region(ret.back(), rom_bytes);
        
        // If the current region is close enough to the previous region, check if there's valid RSP microcode between the two
        if (ret.size() > 1 && ret.back().rom_start - ret[ret.size() - 2].rom_end < microcode_check_threshold) {
            // Check if there's a range of valid CPU instructions between these two regions
            bool valid_range = check_range_cpu(ret[ret.size() - 2].rom_end, ret.back().rom_start, rom_bytes);
            // If there isn't check for RSP instructions
            if (!valid_range) {
                valid_range = check_range_rsp(ret[ret.size() - 2].rom_end, ret.back().rom_start, rom_bytes);
                // If RSP instructions were found, mark the first region as having RSP instructions
                if (valid_range) {
                    ret[ret.size() - 2].has_rsp = true;
                }
            }
            if (valid_range) {
                // If there is, merge the two regions
                size_t new_end = ret.back().rom_end;
                ret.pop_back();
                ret.back().rom_end = new_end;
            }
        }

        // If the region has microcode, search forward until valid RSP instructions end
        if (ret.back().has_rsp) {
            // Keep advancing the region's end until either the stop point is reached or something
            // that isn't a valid RSP instruction is seen
            while (ret.back().rom_end < rom_bytes.size() && is_valid_rsp({read32(rom_bytes, ret.back().rom_end), 0})) {
                ret.back().rom_end += instruction_size;
            }

            // Trim the region again to get rid of any junk that may have been found after its end
            trim_region(ret.back(), rom_bytes);

            // Skip any return addresses that are now part of the region
            while (it != return_addrs.end() && *it < ret.back().rom_end) {
                it++;
            }
        }
    }

    return ret;
}
