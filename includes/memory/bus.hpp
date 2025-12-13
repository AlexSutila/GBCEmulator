#ifndef __BUS_H
#define __BUS_H

#include <emu_types.hpp>
#include <memory>

class AddressBus {
public:
  void write_byte(const addr_t addr, const byte_t value);
  const byte_t read_byte(const addr_t addr);
  AddressBus();

private:
  std::unique_ptr<byte_t[]> mem; // Temporary
};

#endif // __BUS_H
