#include <pybind11/functional.h>
#include <pybind11/pybind11.h>

#include "cpu/interrupts.hpp"
#include "cpu/lr35902.hpp"
#include "cpu/registers/flags.hpp"
#include "cpu/registers/register.hpp"
#include "gbc.hpp"
#include "memory/boot.hpp"
#include "memory/bus.hpp"
#include "memory/mmio/dmg.hpp"
#include "memory/mmio/mmio.hpp"

namespace py = pybind11;

class PyCpuRegister : public CpuRegister {
public:
  using CpuRegister::CpuRegister;
  void write_lo(const byte_t value) override {
    PYBIND11_OVERRIDE(void, CpuRegister, write_lo, value);
  }
  void write_hi(const byte_t value) override {
    PYBIND11_OVERRIDE(void, CpuRegister, write_hi, value);
  }
  void write(const addr_t value) override {
    PYBIND11_OVERRIDE(void, CpuRegister, write, value);
  }
  byte_t read_lo() const override {
    PYBIND11_OVERRIDE(byte_t, CpuRegister, read_lo);
  }
  byte_t read_hi() const override {
    PYBIND11_OVERRIDE(byte_t, CpuRegister, read_hi);
  }
  addr_t read() const override { PYBIND11_OVERRIDE(addr_t, CpuRegister, read); }
};

PYBIND11_MODULE(gbc_py, m) {
  m.doc() = "Game Boy Color emulator bindings";

  py::class_<CpuRegister, PyCpuRegister>(m, "CpuRegister")
      .def(py::init<>())

      .def("write_lo", &CpuRegister::write_lo)
      .def("write_hi", &CpuRegister::write_hi)
      .def("write", &CpuRegister::write)

      .def("read_lo", &CpuRegister::read_lo)
      .def("read_hi", &CpuRegister::read_hi)
      .def("read", &CpuRegister::read)

      // Pythonic properties
      .def_property("lo", &CpuRegister::read_lo, &CpuRegister::write_lo)
      .def_property("hi", &CpuRegister::read_hi, &CpuRegister::write_hi)
      .def_property("full", &CpuRegister::read, &CpuRegister::write);

  // Generic MMIO register
  py::class_<MMIORegister>(m, "MMIORegister")
      .def(py::init<>())
      .def(py::init<byte_t>(), py::arg("init_state"))
      .def("write", &MMIORegister::write, py::arg("value"),
           "Write a byte to the MMIO register")
      .def("read", &MMIORegister::read, "Read a byte from the MMIO register");

  // Status flag masks for CpuFlagsRegister class
  py::enum_<StatusFlagMask>(m, "StatusFlagMask")
      .value("Z", StatusFlagMask::FLAG_Z_MASK)
      .value("N", StatusFlagMask::FLAG_N_MASK)
      .value("H", StatusFlagMask::FLAG_H_MASK)
      .value("C", StatusFlagMask::FLAG_C_MASK)
      .export_values();

  // Already inherits CpuRegister methods, so no need for trampoline class
  py::class_<CpuFlagsRegister, CpuRegister>(m, "CpuFlagsRegister")
      .def(py::init<>())
      .def("get_flag", &CpuFlagsRegister::get_flag)
      .def("set_flag", &CpuFlagsRegister::set_flag)
      .def("clr_flag", &CpuFlagsRegister::clr_flag);

  // Expose boot ROM for memory testing
  m.def("get_boot_rom", []() {
    const auto &rom = get_boot_rom();
    return py::bytes(reinterpret_cast<const char *>(rom.data()), rom.size());
  });

  // Expose main Address Bus class
  py::class_<AddressBus>(m, "AddressBus")
      .def(py::init<>())
      .def("write_byte", &AddressBus::write_byte, py::arg("addr"),
           py::arg("value"))
      .def("read_byte", &AddressBus::read_byte, py::arg("addr"));

  // Interrupt Master Enable class
  py::class_<InterruptMasterEnable>(m, "InterruptMasterEnable")
      .def(py::init<>())
      .def("enable", &InterruptMasterEnable::enable, py::arg("delayed") = false)
      .def("disable", &InterruptMasterEnable::disable)
      .def("step", &InterruptMasterEnable::step)
      .def_property_readonly("enabled", &InterruptMasterEnable::is_enabled);

  // Processor state and class
  py::class_<LR35902::ProcessorState>(m, "ProcessorState")
      .def(py::init<>())
      .def_readwrite("pc", &LR35902::ProcessorState::pc)
      .def_readwrite("sp", &LR35902::ProcessorState::sp)
      .def_readwrite("a", &LR35902::ProcessorState::a)
      .def_readwrite("b", &LR35902::ProcessorState::b)
      .def_readwrite("c", &LR35902::ProcessorState::c)
      .def_readwrite("d", &LR35902::ProcessorState::d)
      .def_readwrite("e", &LR35902::ProcessorState::e)
      .def_readwrite("f", &LR35902::ProcessorState::f)
      .def_readwrite("h", &LR35902::ProcessorState::h)
      .def_readwrite("l", &LR35902::ProcessorState::l)
      .def_readwrite("ime_enabled", &LR35902::ProcessorState::ime_enabled);
  py::class_<LR35902>(m, "LR35902")
      .def(py::init<AddressBus *>(), py::arg("bus"),
           py::keep_alive<1, 2>() // LR35902 keeps AddressBus alive
           )
      .def("step", &LR35902::step)
      .def("get_state", &LR35902::get_state)
      .def("load_state", &LR35902::load_state, py::arg("state"));

  // Pixel Processor STAT interrupt flags
  py::enum_<PPU::StatIntFlags>(m, "StatIntFlags", py::arithmetic())
      .value("LYC_EQ_LY", PPU::StatIntFlags::LYC_EQ_LY)
      .value("MODE_0_SEL", PPU::StatIntFlags::MODE_0_SEL)
      .value("MODE_1_SEL", PPU::StatIntFlags::MODE_1_SEL)
      .value("MODE_2_SEL", PPU::StatIntFlags::MODE_2_SEL)
      .value("LYC_SEL", PPU::StatIntFlags::LYC_SEL)
      .export_values();

  // Pixel Processor STAT modes
  py::enum_<PPU::StatModes>(m, "StatModes")
      .value("MODE_HBLANK", PPU::StatModes::MODE_HBLANK)
      .value("MODE_VBLANK", PPU::StatModes::MODE_VBLANK)
      .value("MODE_OAM_SCAN", PPU::StatModes::MODE_OAM_SCAN)
      .value("MODE_DRAWING", PPU::StatModes::MODE_DRAWING)
      .export_values();

  // Pixel Processor Status register
  py::class_<PPU::STAT, MMIORegister>(m, "STAT")
      .def(py::init<>())
      .def("write", &PPU::STAT::write, py::arg("value"))
      .def("read", &PPU::STAT::read)
      .def("int_enabled", &PPU::STAT::int_enabled, py::arg("flag"),
           "Check if a STAT interrupt source is enabled")
      .def_property("mode", &PPU::STAT::get_mode, &PPU::STAT::set_mode,
                    "Current PPU STAT mode");

  // Pixel Processor Scanline register
  py::class_<PPU::LY>(m, "LY")
      .def(py::init<>())
      .def("write", &PPU::LY::write, py::arg("value"))
      .def("read", &PPU::LY::read)
      .def("reset", &PPU::LY::reset)
      .def("inc", &PPU::LY::inc,
           "Increment LY; returns True on wraparound (153 -> 0)")
      .def_property_readonly("is_visible", &PPU::LY::is_visible,
                             "True when LY is in visible scanlines (0–143)")
      .def_property_readonly("is_vblank", &PPU::LY::is_vblank,
                             "True when LY is in VBlank (144–153)");

  // Pixel Processor class
  py::class_<PixelProcessor>(m, "PixelProcessor")
      .def(py::init<AddressBus *>(), py::arg("bus"),
           py::keep_alive<1, 2>()) // PixelProcessor keeps AddressBus alive
      .def("step", &PixelProcessor::step);

  // Master Emulator class
  py::class_<GameBoyColor>(m, "GameBoyColor")
      .def(py::init<>())
      .def("run", &GameBoyColor::run)
      .def("get_bus", &GameBoyColor::get_bus,
           py::return_value_policy::reference_internal)
      .def("get_cpu", &GameBoyColor::get_cpu,
           py::return_value_policy::reference_internal)
      .def("get_ppu", &GameBoyColor::get_ppu,
           py::return_value_policy::reference_internal);
}
