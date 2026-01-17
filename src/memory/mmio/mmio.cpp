#include "memory/mmio/mmio.hpp"

void MMIORegister::write(const byte_t value) { state = value; }

byte_t MMIORegister::peek() const { return state; }

byte_t MMIORegister::read() { return state; }
