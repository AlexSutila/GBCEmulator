#include "serial.hpp"
#include "cpu/interrupts.hpp"
#include "memory/bus.hpp"
#include "memory/mmio/dmg.hpp"
#include "memory/mmio/mmio.hpp"
#include "savestate/codec.hpp"
#include <cstdint>
#include <stdexcept>

enum : std::uint16_t {
  F_SERIAL_DATA = 1,
  F_SERIAL_CTRL,
};

template <typename T> void SerialUnit::parse_savestate(T &t) {
  constexpr auto version = 1; // Schema revision
  t.chunk_header(version, Savestate::C_SERIAL);

  t.field_complex(F_SERIAL_DATA, [&](T &t) { serial_data.parse_savestate(t); });
  t.field_complex(F_SERIAL_CTRL, [&](T &t) { serial_ctrl.parse_savestate(t); });
  t.eof();
}

template void
SerialUnit::parse_savestate<Savestate::Writer>(Savestate::Writer &);
template void
SerialUnit::parse_savestate<Savestate::Reader>(Savestate::Reader &);
template void SerialUnit::parse_savestate<Savestate::Sizer>(Savestate::Sizer &);

/*
 * TODO: We do not actually implement serial data transfers. The idea of doing
 * serial data transfers over network has crossed our minds, but we are unsure
 * of how bad the latency would be as compared to a literal wire connecting two
 * systems... hence, the serial unit doesn't actually do anything as of now.
 */
SerialUnit::SerialUnit(AddressBus *const bus)
    : serial_data(0), // Should be initialized first because of SC dependency
      serial_ctrl(serial_data) // Fires the interrupt immediately for now
{
  using mmio = IORegisterMapping;

  /* Configure MMIO register connections over address bus */
  bus->connect_mmio(static_cast<addr_t>(mmio::MMIO_SERIAL_DATA), &serial_data);
  bus->connect_mmio(static_cast<addr_t>(mmio::MMIO_SERIAL_CTRL), &serial_ctrl);

  /* Configure connection between SC and IF. This really shouldn't happen but
   * we're doing this because we just fire the interrupt on SC writes. */
  auto *const if_reg = dynamic_cast<InterruptBits *>(
      bus->get_mmio(IORegisterMapping::MMIO_INT_FLAGS));
  if (!if_reg)
    throw std::runtime_error("Failed to configure serial MMIO");
  serial_ctrl.set_interrupt_reg(if_reg);
}
