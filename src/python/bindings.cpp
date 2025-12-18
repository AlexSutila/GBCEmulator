#include <pybind11/functional.h>
#include <pybind11/pybind11.h>

#include "cpu/interrupts.hpp"
#include "cpu/registers/flags.hpp"
#include "cpu/registers/register.hpp"
#include "memory/boot.hpp"
#include "memory/bus.hpp"

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
}
