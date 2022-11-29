#include <rabbitizer.hpp>
#include <fmt/format.h>

int main() {
    rabbitizer::InstructionCpu instr{0x03E00008, 0x80000000};
    fmt::print("Instr: {}\n", instr.disassemble(0));
    return 0;
}
