#include "frontend/python/testing.hpp"
#include "frontend/python/wrappers.hpp"
#include <cstdint>

#include "emu_types.hpp" // For `byte_t`

constexpr std::uint32_t max_cycles = 80000000;
constexpr byte_t breakpoint_opcode = 0x40; // LD B, B

bool poll_mooneye_regular(PyGameBoyColor &gbc) {
  std::uint32_t cycles{0};
  byte_t op = 0;
  while (op != breakpoint_opcode && cycles < max_cycles) {
    const auto &cpu = gbc.get_cpu();
    op = cpu->cur_opcode();

    gbc.step();  // Run until 'LD B, B'
    cycles += 2; // Two to account for double speeds fast cycle
  }
  return op == breakpoint_opcode;
}

bool poll_mooneye_big_step(PyGameBoyColor &gbc) {
  std::uint32_t cycles{0};
  byte_t op = 0;

  while (op != breakpoint_opcode && cycles < max_cycles) {
    const auto &cpu = gbc.get_cpu();
    op = cpu->cur_opcode();
    cycles += gbc.big_step();
  }
  return op == breakpoint_opcode;
}

[[nodiscard]] bool poll_mooneye_test(PyGameBoyColor &gbc, bool big_step) {
  return big_step ? poll_mooneye_big_step(gbc) : poll_mooneye_regular(gbc);
}
