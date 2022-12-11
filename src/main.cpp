#include <cstdlib>
#include <fstream>
#include <filesystem>
#include <vector>

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

    std::vector<RomRegion> code_regions = find_code_regions(rom_bytes);
    fmt::print("Found {} code regions:\n", code_regions.size());

    for (const auto& codeseg : code_regions) {
        size_t start = nearest_multiple_down<16>(codeseg.rom_start);
        size_t end   = nearest_multiple_up<16>(codeseg.rom_end);

        if constexpr (!show_true_ranges) {
            fmt::print("  0x{:08X} to 0x{:08X} (0x{:06X}) rsp: {}\n",
                start, end, end - start, codeseg.has_rsp);
        } else {
            fmt::print("  0x{:08X} to 0x{:08X} (0x{:06X}) rsp: {}\n",
                codeseg.rom_start, codeseg.rom_end, codeseg.rom_end - codeseg.rom_start, codeseg.has_rsp);
            if (codeseg.rom_start != start) {
                fmt::print("    Warn: code region doesn't start at 16 byte alignment");
            }
        }
    }
    
    return EXIT_SUCCESS;
}
