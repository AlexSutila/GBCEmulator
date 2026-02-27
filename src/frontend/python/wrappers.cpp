#include "frontend/python/wrappers.hpp"
#include "cart/cart.hpp"
#include "cpu/lr35902.hpp"
#include "debugger/breakpoint.hpp"
#include "frontend/python/frontend.hpp"
#include "memory/bus.hpp"
#include "memory/mmio/dmg.hpp"
#include "memory/mmio/mmio.hpp"
#include <optional>

PyGameBoyColor::PyGameBoyColor(const pybind11::function &callback) {
  const auto &gbc = fe_.get();

  // Need to wrap callback and make it Python-call safe
  cb_ = [callback]() -> Debug::BreakReason {
    pybind11::gil_scoped_acquire acquire;
    return callback().cast<Debug::BreakReason>();
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

void PyGameBoyColor::breakpoint_add(const addr_t addr,
                                    Debug::BreakReason reason) {
  auto &debugger = fe_.get()->get_debugger();
  debugger->breakpoint_add(addr, reason);
}

void PyGameBoyColor::breakpoint_del(const addr_t addr) {
  auto &debugger = fe_.get()->get_debugger();
  debugger->breakpoint_del(addr);
}

void PyGameBoyColor::step_cycles(int cycles) {
  auto &gbc = fe_.get();
  for (int i{0}; i < cycles; i++)
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
  auto *joyp = dynamic_cast<Joypad::JOYP *>(
      get_bus()->get_mmio(IORegisterMapping::MMIO_JOYPAD));
  joyp->set_state(state);
}
