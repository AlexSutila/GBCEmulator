#ifndef __DEBUG_PRINT_H
#define __DEBUG_PRINT_H

#include "cpu/interrupts.hpp"
#include "cpu/lr35902.hpp"
#include "gbc.hpp"
#include "ppu/ppu.hpp"
#include <string>

namespace Debug {

[[nodiscard]] std::string hex8(byte_t v);
[[nodiscard]] std::string hex16(addr_t v);

[[nodiscard]] std::string to_string(const LR35902::ProcessorState &s);
[[nodiscard]] std::string to_string(const PixelProcessingUnit::PPUState &s);
[[nodiscard]] std::string to_string(const InterruptBits &i);
[[nodiscard]] std::string to_string(const runtime_sys_info &r);

// Simple lookups (return "Unknown" if not recognized)
std::string destination_name(byte_t code);
std::string cartridge_type_name(byte_t code);
std::string cgb_flag_desc(byte_t cgb_flag);
std::string sgb_flag_desc(byte_t sgb_flag);

// Licensee: choose "old" unless old == 0x33, then use "new"
std::string old_licensee_name(byte_t old_code);
std::string new_licensee_name(const std::string &two_chars);

// Pretty sizes (based on header tags)
std::string rom_size_pretty(byte_t rom_size_code);
std::string ram_size_pretty(byte_t ram_size_code);

// Render a full report
std::string describe_cart(const cart &c);

} // namespace Debug

#endif // __DEBUG_PRINT_H
