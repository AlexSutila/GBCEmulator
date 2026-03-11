#ifndef DEBUG_PRINT_HPP
#define DEBUG_PRINT_HPP

#include "cpu/interrupts.hpp"
#include "cpu/lr35902.hpp"
#include "gbc.hpp"
#include "memory/dma.hpp"
#include "ppu/ppu.hpp"
#include <string>

namespace Debug {

[[nodiscard]] std::string hex8(byte_t v, bool compact);
[[nodiscard]] std::string hex16(addr_t v);

/* A nice utility for displaying address bus contents in an easy manner */
[[nodiscard]] std::string create_hex_view(addr_t base_addr, const std::vector<byte_t> &vec);

[[nodiscard]] std::string to_string(const LR35902::ProcessorState &s);
[[nodiscard]] std::string to_string(const InterruptBits &i);
[[nodiscard]] std::string to_string(const runtime_sys_info &r);

[[nodiscard]] std::string to_string(const PixelProcessingUnit::PPUState &s);
[[nodiscard]] std::string to_string(const ObjAttrDMA::DMAState &s);
[[nodiscard]] std::string to_string(const VDMA::DMAState &s);

[[nodiscard]] std::string destination_name(byte_t code);
[[nodiscard]] std::string cartridge_type_name(byte_t code, SpecialMbc special);
[[nodiscard]] std::string cgb_flag_desc(byte_t cgb_flag);
[[nodiscard]] std::string sgb_flag_desc(byte_t sgb_flag);

[[nodiscard]] std::string old_licensee_name(byte_t old_code);
[[nodiscard]] std::string new_licensee_name(const std::string &two_chars);
[[nodiscard]] std::string rom_size_pretty(byte_t rom_size_code);
[[nodiscard]] std::string ram_size_pretty(byte_t ram_size_code);
[[nodiscard]] std::string describe_cart(const cart &c);

} // namespace Debug

#endif // DEBUG_PRINT_HPP
