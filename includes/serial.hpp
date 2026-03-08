#ifndef GBC_SERIAL_HPP
#define GBC_SERIAL_HPP

#include "memory/mmio/dmg.hpp"
#include "memory/mmio/mmio.hpp"

class AddressBus;

class SerialUnit {
public:
  template <typename T> void parse_savestate(T &t);
  explicit SerialUnit(AddressBus *bus);

private:
  MMIORegister serial_data;
  Serial::SerialCtrl serial_ctrl;
};

#endif // GBC_SERIAL_HPP
