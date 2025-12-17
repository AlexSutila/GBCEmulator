#ifndef GBC_CART_DISPLAY_HPP
#define GBC_CART_DISPLAY_HPP

#pragma once
#include "cart.hpp"

#include <iosfwd>
#include <string>

// Simple lookups (return "Unknown" if not recognized)
std::string destination_name(byte_t code);
std::string cartridge_type_name(byte_t code);
std::string cgb_flag_desc(byte_t cgb_flag);
std::string sgb_flag_desc(byte_t sgb_flag);

// Licensee: choose "old" unless old == 0x33, then use "new"
std::string old_licensee_name(byte_t old_code);
std::string new_licensee_name(const std::string& two_chars);

// Pretty sizes (based on header tags)
std::string rom_size_pretty(byte_t rom_size_code);
std::string ram_size_pretty(byte_t ram_size_code);

// Render a full report
std::string describe_cart(const cart& c);

// Print to stream
void print_cart(std::ostream& os, const cart& c);



#endif //GBC_CART_DISPLAY_HPP