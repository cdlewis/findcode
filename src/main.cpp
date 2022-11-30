#include <cstdlib>
#include <cstdint>
#include <fstream>
#include <filesystem>
#include <vector>
#include <span>
#include <bit>

#include "rabbitizer.hpp"
#include "fmt/format.h"

#include "findcode.h"

// Read a rom file from the given path and swap it (if necessary) to little-endian
std::vector<uint8_t> read_rom(const char* path) {
    size_t rom_size;
    std::vector<uint8_t> ret;
    std::ifstream rom_file{path, std::ios::binary};

    rom_file.seekg(0, std::ios::end);
    rom_size = rom_file.tellg();
    rom_file.seekg(0, std::ios::beg);

    ret.resize(nearest_multiple_up<sizeof(uint32_t)>(rom_size));
    rom_file.read(reinterpret_cast<char*>(ret.data()), rom_size);

    if (rom_file.bad()) {
        fmt::print(stderr, "Failed to read rom file {}\n", path);
        exit(EXIT_FAILURE);
    }

    // Check rom endianness
    uint32_t first_word = *reinterpret_cast<uint32_t*>(ret.data());
    if (first_word == 0x40123780) {
        fmt::print("Detected big endian rom\n");
        // Byteswap rom to little endian
        for (size_t i = 0; i < ret.size() / instruction_size; i += instruction_size) {
            *reinterpret_cast<uint32_t*>(ret.data() + i) = byteswap(read32(ret, i));
        }
    } else if (first_word == 0x12408037) {
        fmt::print(stderr, "v64 (byteswapped) roms not supported\n");
        exit(EXIT_FAILURE);
    } else if (first_word == 0x80371240) {
        fmt::print("Detected little endian rom\n");
    } else {
        fmt::print(stderr, "File is not an N64 game: {}\n", path);
        exit(EXIT_FAILURE);
    }

    return ret;
}

constexpr uint32_t jr_ra = 0x03E00008;

// Search a span for any instances of the instruction `jr $ra`
std::vector<size_t> find_return_locations(std::span<uint8_t> rom_bytes) {
    std::vector<size_t> ret{};
    ret.reserve(1024);

    for (size_t rom_addr = 0x1000; rom_addr < rom_bytes.size() / instruction_size; rom_addr += instruction_size) {
        uint32_t rom_word = *reinterpret_cast<uint32_t*>(rom_bytes.data() + rom_addr);

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
bool is_load(InstrId id) {
    return
        id == InstrId::cpu_lb ||
        id == InstrId::cpu_lbu || 
        id == InstrId::cpu_lh ||
        id == InstrId::cpu_lhu || 
        id == InstrId::cpu_lw ||
        id == InstrId::cpu_lwu || 
        id == InstrId::cpu_ld ||
        id == InstrId::cpu_lwc1 || 
        id == InstrId::cpu_ldc1;
}

// Check if the provided cop0 register index is valid
bool invalid_cop0_register(int reg) {
    return reg == 7 || (reg >= 21 && reg <= 25) || reg == 31;
}

bool is_unused_n64_instruction(InstrId id) {
    return
        id == InstrId::cpu_ll;
}

// Check if a given instruction is valid via several metrics
bool is_valid(const rabbitizer::InstructionCpu& instr) {
    InstrId id = instr.getUniqueId();
    // Check for instructions with invalid bits or invalid opcodes
    if (!instr.isValid() || id == InstrId::cpu_INVALID) {
        return false;
    }

    // Check for loads or stores with an offset from $zero
    if ((is_store(id) || is_load(id)) && instr.GetO32_rs() == RegisterId::GPR_O32_zero) {
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

    return true;
}

// Searches backwards from the given rom address until it hits an invalid instruction
size_t find_code_start(std::span<uint8_t> rom_bytes, size_t rom_addr) {
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
size_t find_code_end(std::span<uint8_t> rom_bytes, size_t rom_addr) {
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
void trim_segment(RomRegion& codeseg, std::span<uint8_t> rom_bytes) {
    size_t start = codeseg.rom_start;
    size_t end = codeseg.rom_end;
    
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

// Find all the regions of code in the given rom given the list of `jr $ra` instructions located in the rom
std::vector<RomRegion> find_code_regions(std::span<uint8_t> rom_bytes, std::span<size_t> return_addrs) {
    std::vector<RomRegion> ret{};

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
            if (check_range_rsp(ret[ret.size() - 2].rom_end, cur_segment.rom_start, rom_bytes)) {
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

int main(int argc, char* argv[]) {
    if (argc != 2) {
        fmt::print("Usage: {} [rom]\n", argv[0]);
        exit(EXIT_SUCCESS);
    }

    const char* rom_path = argv[1]; 
    if (!std::filesystem::exists(rom_path)) {
        fmt::print(stderr, "No such file: {}\n", rom_path);
        exit(EXIT_FAILURE);
    }

    std::vector<uint8_t> rom_bytes = read_rom(rom_path);
    // fmt::print("Rom size: 0x{:08X}\n", rom_bytes.size());

    std::vector<size_t> return_addrs = find_return_locations(rom_bytes);
    // fmt::print("Found {} returns\n", return_addrs.size());

    std::vector<RomRegion> code_regions = find_code_regions(rom_bytes, return_addrs);
    fmt::print("Found {} code regions:\n", code_regions.size());

    for (const auto& codeseg : code_regions) {
        size_t start = nearest_multiple_up<16>(codeseg.rom_start);
        size_t end   = nearest_multiple_up<16>(codeseg.rom_end);
        fmt::print("  0x{:08X} to 0x{:08X} (0x{:06X})\n",
            start, end, end - start);
    }
    
    return EXIT_SUCCESS;
}
