#ifndef __SERIAL_H
#define __SERIAL_H

#include "memory/mmio/dmg.hpp"
#include "memory/mmio/mmio.hpp"

class AddressBus;

class SerialUnit {
public:
  explicit SerialUnit(AddressBus *const bus);

private:
  MMIORegister serial_data;
  Serial::SerialCtrl serial_ctrl;
};

#endif // __SERIAL_H
