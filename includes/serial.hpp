#ifndef GBC_SERIAL_HPP
#define GBC_SERIAL_HPP

#include "memory/mmio/dmg.hpp"
#include "memory/mmio/mmio.hpp"

class AddressBus;

class SerialUnit {
public:
  explicit SerialUnit(AddressBus *bus);

private:
  MMIORegister serial_data;
  Serial::SerialCtrl serial_ctrl;
};

#endif // GBC_SERIAL_HPP
