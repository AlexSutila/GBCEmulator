#include "frontend/python/testing.hpp"
#include "frontend/python/wrappers.hpp"
#include "ppu/ppu.hpp"
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <pybind11/stl/filesystem.h>

namespace py = pybind11;

static void bind_cart(py::module_ &m) {
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

  // Included rom header object
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

  // For content loading
  m.def(
      "load_cart_raw",
      [](py::bytes data) {
        std::string_view view = data;
        std::vector<byte_t> rom(view.begin(), view.end());
        return load_cart_raw(std::move(rom));
      },
      py::arg("rom_bytes"), "Load a Game Boy cartridge from raw ROM bytes");
  m.def(
      "load_cart_fs",
      [](const fs::path &rom_path) { return load_cart_fs(rom_path); },
      py::arg("rom_path"), "Load a Game Boy cartridge from filesystem");
}

static void bind_address_bus(py::module_ &m) {
  py::class_<AddressBus>(m, "AddressBus")
      .def("init_test_bed", &AddressBus::init_test_bed)
      .def("write_byte", &AddressBus::write_byte, py::arg("addr"),
           py::arg("value"))
      .def("read_byte", &AddressBus::read_byte, py::arg("addr"));
}

static void bind_processor(py::module_ &m) {
  py::class_<LR35902>(m, "LR35902")
      .def("step", &LR35902::step)
      .def("get_state", &LR35902::get_state)
      .def("load_state", &LR35902::load_state, py::arg("state"));

  // For processor state interrogation
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
}

static void bind_timer(py::module_ &m) {
  py::class_<TimerUnit>(m, "TimerUnit").def("step", &TimerUnit::step);
}

static void bind_ppu(py::module_ &m) {
  py::class_<PixelProcessingUnit>(m, "PixelProcessor")
      .def("step", &PixelProcessingUnit::step)
      .def("get_state", &PixelProcessingUnit::get_state);

  // Bind stateful enumerations
  py::enum_<PPU::StatModes>(m, "StatModes")
      .value("HBLANK", PPU::StatModes::MODE_HBLANK)
      .value("VBLANK", PPU::StatModes::MODE_VBLANK)
      .value("OAM_SCAN", PPU::StatModes::MODE_OAM_SCAN)
      .value("DRAWING", PPU::StatModes::MODE_DRAWING)
      .export_values();
  py::class_<PixelProcessingUnit::PPUState>(m, "PPUState")
      .def_readonly("state", &PixelProcessingUnit::PPUState::state)
      .def_readonly("lcdc", &PixelProcessingUnit::PPUState::lcdc)
      .def_readonly("stat", &PixelProcessingUnit::PPUState::stat)
      .def_readonly("scx", &PixelProcessingUnit::PPUState::scx)
      .def_readonly("scy", &PixelProcessingUnit::PPUState::scy)
      .def_readonly("wx", &PixelProcessingUnit::PPUState::wx)
      .def_readonly("wy", &PixelProcessingUnit::PPUState::wy)
      .def_readonly("lyc", &PixelProcessingUnit::PPUState::lyc)
      .def_readonly("ly", &PixelProcessingUnit::PPUState::ly);
}

static void bind_debugger(py::module_ &m) {
  py::enum_<Debug::BreakReason>(m, "BreakReason")
      .value("BRK_CONTINUE", Debug::BreakReason::BRK_CONTINUE)
      .value("BRK_ADDRESS_EXECUTED", Debug::BreakReason::BRK_ADDRESS_EXECUTED)
      .value("BRK_ADDRESS_READ", Debug::BreakReason::BRK_ADDRESS_READ)
      .value("BRK_ADDRESS_WRITTEN", Debug::BreakReason::BRK_ADDRESS_WRITTEN)
      .value("BRK_STEP_INSTRUCTION", Debug::BreakReason::BRK_STEP_INSTRUCTION)
      .value("BRK_STEP_SCANLINE", Debug::BreakReason::BRK_STEP_SCANLINE)
      .value("BRK_STEP_FRAME", Debug::BreakReason::BRK_STEP_FRAME);
}

static void bind_gbc(py::module_ &m) {
  py::class_<PyGameBoyColor>(m, "GameBoyColor")
      .def(py::init<pybind11::function>())
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
           py::return_value_policy::reference_internal)
      .def("breakpoint_add", &PyGameBoyColor::breakpoint_add)
      .def("breakpoint_del", &PyGameBoyColor::breakpoint_del);
}

PYBIND11_MODULE(gbc_py, m) {
  m.doc() = "Game Boy Color emulator bindings";
  bind_debugger(m);
  bind_gbc(m);

  // Expose components
  bind_address_bus(m);
  bind_processor(m);
  bind_timer(m);
  bind_cart(m);
  bind_ppu(m);

  // Expose testing helpers, may add more in the future
  m.def("poll_mooneye_test", &poll_mooneye_test, py::arg("gbc"),
        "Run the emulator until the Mooneye LD B,B end marker is reached");
}
