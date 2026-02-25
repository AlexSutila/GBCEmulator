#ifndef GBC_SERIAL_HPP
#define GBC_SERIAL_HPP

#include "memory/mmio/dmg.hpp"
#include "memory/mmio/mmio.hpp"

class AddressBus;
namespace Savestate {
class Reader;
class Writer;
}

class SerialUnit {
public:
  explicit SerialUnit(AddressBus *bus);
  static void savestate_serialize(Savestate::Writer &out);
  static void savestate_deserialize(Savestate::Reader &in);

private:
  MMIORegister serial_data;
  Serial::SerialCtrl serial_ctrl;
};

#endif // GBC_SERIAL_HPP
