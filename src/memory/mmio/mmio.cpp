#include "memory/mmio/mmio.hpp"
#include "savestate/codec.hpp"

void MMIORegister::write(const byte_t value) { raw_state_set(value); }

byte_t MMIORegister::peek() const { return raw_state(); }

byte_t MMIORegister::read() { return raw_state(); }

void MMIORegister::savestate_serialize(Savestate::Writer &out) const {
  out.field_u8(1, raw_state());
}

void MMIORegister::savestate_deserialize(Savestate::Reader &in) {
  while (const auto field = in.next_field()) {
    auto [id, payload] = *field;
    // Another silly placeholder
    switch (id) {
    case 1:
      raw_state_set(payload.u8());
      break;
    default:
      payload.skip(payload.remaining());
      break;
    }
    payload.expect_eof();
  }
}
