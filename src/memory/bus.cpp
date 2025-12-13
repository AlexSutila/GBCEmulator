#include <memory>
#include <memory/bus.hpp>

AddressBus::AddressBus() {
  mem = std::make_unique<byte_t[]>(0xFFFF);
}

void AddressBus::write_byte(const addr_t addr, const byte_t value) {
  mem[addr] = value;
}

const byte_t AddressBus::read_byte(const addr_t addr) {
  return mem[addr];
}
