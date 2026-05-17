#include "frontend/python/wrappers.hpp"
#include "cart/cart.hpp"
#include "cpu/lr35902.hpp"
#include "debugger/breakpoint.hpp"
#include "frontend/python/frontend.hpp"
#include "memory/bus.hpp"
#include "memory/mmio/dmg.hpp"
#include "memory/mmio/mmio.hpp"
#include <optional>
#include <span>

PyGameBoyColor::PyGameBoyColor(const pybind11::function &callback) {
  const auto &gbc = fe_.get();

  // Need to wrap callback and make it Python-call safe
  cb_ = [callback](Debug::Context ctx) -> Debug::BreakReason {
    pybind11::gil_scoped_acquire acquire;
    return callback(ctx).cast<Debug::BreakReason>();
  };
  gbc->configure_debugger(Debug::Debugger(cb_));
}

PyGameBoyColor::PyGameBoyColor() : fe_() {}

void PyGameBoyColor::insert_cartridge(cart c) {
  auto &gbc = fe_.get();
  gbc->insert_cartridge(c);
}

using frame_buf_t = std::array<std::uint32_t, 160 * 144>;
frame_buf_t PyGameBoyColor::get_frame() { return fe_.get_frame(); }

[[nodiscard]] std::vector<byte_t> PyGameBoyColor::savestate_serialize() {
  const auto &gbc = fe_.get();
  if (!savestate_ready())
    return {}; // To avoid throwing an error within the core
  return gbc->savestate_serialize();
}

[[nodiscard]] Savestate::TreeRoot PyGameBoyColor::savestate_as_tree() {
  const auto &gbc = fe_.get();
  if (!savestate_ready())
    return {}; // Yeah... bruh moment
  return gbc->savestate_as_tree();
}

bool PyGameBoyColor::savestate_deserialize(std::span<const byte_t> data) {
  const auto &gbc = fe_.get();
  try {
    gbc->savestate_deserialize(data);
    return true;
  }

  // Savestate may fail if the savestate was created on a different architecture
  // or an older emulator version. TODO: Pass some informative error message?
  catch (...) {
    return false;
  }
}

[[nodiscard]] bool PyGameBoyColor::savestate_ready() {
  const auto &gbc = fe_.get();
  return gbc->savestate_ready();
}

void PyGameBoyColor::breakpoint_add(const addr_t addr, Debug::BreakReason reason) {
  auto &debugger = fe_.get()->get_debugger();
  debugger->breakpoint_add(addr, reason);
}

void PyGameBoyColor::breakpoint_del(const addr_t addr) {
  auto &debugger = fe_.get()->get_debugger();
  debugger->breakpoint_del(addr);
}

std::size_t PyGameBoyColor::big_step_cycles(std::size_t cycles) {
  auto &gbc = fe_.get();
  std::size_t elapsed_cycles{0};

  do {
    std::size_t sync_cycles = gbc->big_step();
    elapsed_cycles = elapsed_cycles + sync_cycles;
  } while (elapsed_cycles < cycles);

  // We return the number of cycles elapsed here because it is not garunteed to
  // always align perfectly with the number of cycles passed in.
  return elapsed_cycles;
}

std::size_t PyGameBoyColor::big_step() {
  auto &gbc = fe_.get();
  return gbc->big_step();
}

void PyGameBoyColor::step_cycles(std::size_t cycles) {
  auto &gbc = fe_.get();
  for (std::size_t i{0}; i < cycles; i++)
    gbc->step();
}

void PyGameBoyColor::step() {
  auto &gbc = fe_.get();
  gbc->step();
}

void PyGameBoyColor::init_test_bed() {
  auto &gbc = fe_.get();
  gbc->init_test_bed();
}

std::optional<Debug::Debugger> &PyGameBoyColor::get_debugger() {
  auto &gbc = fe_.get();
  return gbc->get_debugger();
}

PixelProcessingUnit *PyGameBoyColor::get_ppu() {
  auto &gbc = fe_.get();
  return gbc->get_ppu();
}

AddressBus *PyGameBoyColor::get_bus() {
  auto &gbc = fe_.get();
  return gbc->get_bus();
}

TimerUnit *PyGameBoyColor::get_timer() {
  auto &gbc = fe_.get();
  return gbc->get_timer();
}

LR35902 *PyGameBoyColor::get_cpu() {
  auto &gbc = fe_.get();
  return gbc->get_cpu();
}

void PyGameBoyColor::put_joyp_state(std::uint8_t state) {
  auto *joyp = dynamic_cast<Joypad::JOYP *>(get_bus()->get_mmio(IORegisterMapping::MMIO_JOYPAD));
  joyp->set_state(state);
}
