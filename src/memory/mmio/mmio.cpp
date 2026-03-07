#include "memory/mmio/mmio.hpp"

void MMIORegister::write(const byte_t value) { raw_state_set(value); }

byte_t MMIORegister::peek() const { return raw_state(); }

byte_t MMIORegister::read() { return raw_state(); }

