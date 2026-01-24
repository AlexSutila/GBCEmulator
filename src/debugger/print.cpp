#include "debugger/print.hpp"
#include "cpu/interrupts.hpp"
#include "cpu/lr35902.hpp"

static inline std::string hex8(byte_t v) {
  std::ostringstream o;
  o << "0x" << std::hex << std::uppercase // I hate writing UI lol
    << std::setw(2) << std::setfill('0') << +v;
  return o.str();
}

namespace Debug {

std::string to_string(const LR35902::ProcessorState &s) {
  auto hex8 = [](uint8_t v) {
    std::ostringstream o;
    o << "0x" << std::hex << std::uppercase << std::setw(2) << std::setfill('0')
      << +v;
    return o.str();
  };

  auto hex16 = [](uint16_t v) {
    std::ostringstream o;
    o << "0x" << std::hex << std::uppercase << std::setw(4) << std::setfill('0')
      << v;
    return o.str();
  };

  std::ostringstream out;
  out << "PC: " << hex16(s.pc) << "  "
      << "SP: " << hex16(s.sp) << "\n\n"
      << " A: " << hex8(s.a) << "  F: " << hex8(s.f) << "\n"
      << " B: " << hex8(s.b) << "  C: " << hex8(s.c) << "\n"
      << " D: " << hex8(s.d) << "  E: " << hex8(s.e) << "\n"
      << " H: " << hex8(s.h) << "  L: " << hex8(s.l) << "\n\n"
      << "IME: " << (s.ime_enabled ? "enabled" : "disabled");
  return out.str();
}

std::string to_string(const InterruptBits &i) {
  std::ostringstream out;

  auto flag = [&](InterruptFlagMask m) {
    return i.get_flag(m) ? "SET " : "clear";
  };

  out << "Raw: " << hex8(i.peek()) << "\n\n"
      << "Flags:\n"
      << " VBLANK (bit 0): " << flag(InterruptFlagMask::INT_FLAG_VBLANK) << "\n"
      << " LCD    (bit 1): " << flag(InterruptFlagMask::INT_FLAG_LCD) << "\n"
      << " TIMER  (bit 2): " << flag(InterruptFlagMask::INT_FLAG_TIMER) << "\n"
      << " SERIAL (bit 3): " << flag(InterruptFlagMask::INT_FLAG_SERIAL) << "\n"
      << " JOYPAD (bit 4): " << flag(InterruptFlagMask::INT_FLAG_JOYPAD)
      << "\n";

  return out.str();
}

[[nodiscard]] std::string to_string(const runtime_sys_info &r) {
  std::ostringstream out;

  out << "clocks=" << r.elapsed_clocks
      << " mode=" << (r.cgb_mode ? "CGB" : "DMG")
      << " speed=" << (r.double_speed ? "2x" : "1x")
      << (r.speed_switch_armed ? " (armed)" : "")
      << " halted=" << (r.halted ? "YES" : "NO");
  return out.str();
}

} // namespace Debug
