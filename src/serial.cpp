#include "serial.hpp"
#include "cpu/interrupts.hpp"
#include "memory/bus.hpp"
#include "memory/mmio/dmg.hpp"
#include "memory/mmio/mmio.hpp"
#include <stdexcept>

/*
 * TODO: We do not actually implement serial data transfers. The idea of doing
 * serial data transfers over network has crossed our minds, but we are unsure
 * of how bad the latency would be as compared to a literal wire connecting two
 * systems... hence, the serial unit doesn't acutally do anything as of now.
 */
SerialUnit::SerialUnit(AddressBus *const bus)
    : serial_data(0), // Should be initialized first becaue of SC dependency
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
