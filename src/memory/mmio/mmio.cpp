#include "memory/mmio/mmio.hpp"
#include "savestate/codec.hpp"

enum : std::uint16_t {
  F_STATE = 1,
};

/**
 * We do not consider each MMIO Register its own chunk, as this will likely end
 * up resulting in very polluted and bloated `chunk tag` enumeration namespace.
 */
template <typename T> void MMIORegister::parse_savestate(T &t) {
  t.field_generic(F_STATE, state_);
}

template void
MMIORegister::parse_savestate<Savestate::Writer>(Savestate::Writer &);
template void
MMIORegister::parse_savestate<Savestate::Reader>(Savestate::Reader &);
template void
MMIORegister::parse_savestate<Savestate::Sizer>(Savestate::Sizer &);
template void
MMIORegister::parse_savestate<Savestate::Checker>(Savestate::Checker &);

void MMIORegister::write(const byte_t value) { state_ = value; }

byte_t MMIORegister::peek() const { return state_; }

byte_t MMIORegister::read() { return state_; }
