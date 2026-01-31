#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <pybind11/stl/filesystem.h>
#include "frontend/python/wrappers.hpp"

namespace py = pybind11;

bool poll_mooneye_test(PyGameBoyColor &gbc) {
  std::uint32_t max_cycles = 10000000, cycles = 0;
  byte_t op = 0;
  while (op != 0x40 && cycles < max_cycles) {
    const auto &bus = gbc.get_bus();
    const auto &cpu = gbc.get_cpu();

    const auto state = cpu->get_state();
    op = bus->read_byte(state.pc);

    gbc.step(); // Run until 'LD B, B'
    gbc.step();
    gbc.step();
    gbc.step();
    ++cycles;
  }
  return op == 0x40;
}

PYBIND11_MODULE(gbc_py, m) {
  m.doc() = "Game Boy Color emulator bindings";

  // Needed to initialize sone components one-off
  py::class_<runtime_sys_info>(m, "RuntimeSysInfo")
      .def(py::init<>())
      .def_readwrite("double_speed", &runtime_sys_info::double_speed)
      .def_readwrite("cgb_mode", &runtime_sys_info::cgb_mode)
      .def_readwrite("elapsed_clocks", &runtime_sys_info::elapsed_clocks);

  // Cartridge helper classes
  py::class_<rom_header>(m, "RomHeader")
      .def(py::init<>())
      .def_readonly("entry_point", &rom_header::entry_point)
      .def_readonly("title_area", &rom_header::title_area)
      .def_readonly("new_licensee_code", &rom_header::new_licensee_code)
      .def_readonly("sgb_flag", &rom_header::sgb_flag)
      .def_readonly("cartridge_type", &rom_header::cartridge_type)
      .def_readonly("rom_size_code", &rom_header::rom_size_code)
      .def_readonly("ram_size_code", &rom_header::ram_size_code)
      .def_readonly("destination_code", &rom_header::destination_code)
      .def_readonly("old_licensee_code", &rom_header::old_licensee_code)
      .def_readonly("mask_rom_version", &rom_header::mask_rom_version)
      .def_readonly("header_checksum", &rom_header::header_checksum)
      .def_readonly("global_checksum", &rom_header::global_checksum)
      .def("cgb_flag", &rom_header::cgb_flag)
      .def("title", &rom_header::title)
      .def("manufacturer_code", &rom_header::manufacturer_code);

  // Cartridge context
  py::class_<cart>(m, "Cart")
      .def(py::init<>())
      .def_readonly("file_path", &cart::file_path)
      .def_readonly("header", &cart::header)
      .def_readonly("declared_rom_bytes", &cart::declared_rom_bytes)
      .def_readonly("declared_ram_bytes", &cart::declared_ram_bytes)
      .def_readonly("header_checksum_ok", &cart::header_checksum_ok)
      .def_readonly("global_checksum_ok", &cart::global_checksum_ok)
      .def_property_readonly(
          "rom",
          [](const cart &c) {
            return py::bytes(reinterpret_cast<const char *>(c.rom_data()),
                             c.rom_size());
          },
          "ROM contents as immutable bytes")
      .def_property_readonly("rom_size", &cart::rom_size);

  // Generic MMIO register
  py::class_<MMIORegister>(m, "MMIORegister")
      .def(py::init<>())
      .def(py::init<byte_t>(), py::arg("init_state"))
      .def("write", &MMIORegister::write, py::arg("value"),
           "Write a byte to the MMIO register")
      .def("read", &MMIORegister::read, "Read a byte from the MMIO register");

  // Expose main Address Bus class
  py::class_<AddressBus>(m, "AddressBus")
      .def("init_test_bed", &AddressBus::init_test_bed)
      .def("write_byte", &AddressBus::write_byte, py::arg("addr"),
           py::arg("value"))
      .def("read_byte", &AddressBus::read_byte, py::arg("addr"));

  // Processor state interrogation
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

  // Processor
  py::class_<LR35902>(m, "LR35902")
      .def("step", &LR35902::step)
      .def("get_state", &LR35902::get_state)
      .def("load_state", &LR35902::load_state, py::arg("state"));

  // Timer class
  py::class_<TimerUnit>(m, "TimerUnit").def("step", &TimerUnit::step);

  // Pixel Processor class
  py::class_<PixelProcessingUnit>(m, "PixelProcessor")
      .def("step", &PixelProcessingUnit::step);

  // Master Emulator class
  py::class_<PyGameBoyColor>(m, "GameBoyColor")
      .def(py::init<>())
      .def("insert_cartridge", &PyGameBoyColor::insert_cartridge)
      .def("init_test_bed", &PyGameBoyColor::init_test_bed)
      .def("step_cycles", &PyGameBoyColor::step_cycles)
      .def("step", &PyGameBoyColor::step)
      .def("get_frame", &PyGameBoyColor::get_frame)
      .def("get_bus", &PyGameBoyColor::get_bus,
           py::return_value_policy::reference_internal)
      .def("get_cpu", &PyGameBoyColor::get_cpu,
           py::return_value_policy::reference_internal)
      .def("get_ppu", &PyGameBoyColor::get_ppu,
           py::return_value_policy::reference_internal)
      .def("get_timer", &PyGameBoyColor::get_timer,
           py::return_value_policy::reference_internal)
      .def("put_joyp_state", &PyGameBoyColor::put_joyp_state,
           py::return_value_policy::reference_internal);

  // Content loading
  m.def("load_cart_raw",
      [](py::bytes data) {
        std::string_view view = data;
        std::vector<byte_t> rom(view.begin(), view.end());
        return load_cart_raw(std::move(rom));
      },
      py::arg("rom_bytes"), "Load a Game Boy cartridge from raw ROM bytes");
  m.def("load_cart_fs",
      [](const fs::path &rom_path) { return load_cart_fs(rom_path); },
      py::arg("rom_path"), "Load a Game Boy cartridge from filesystem");

  // For testing
  m.def("poll_mooneye_test", &poll_mooneye_test, py::arg("gbc"),
        "Run the emulator until the Mooneye LD B,B end marker is reached");
}
