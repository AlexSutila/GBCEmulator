#include <cstddef>
#include <memory>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <pybind11/stl/filesystem.h>

#include "cart/cart.hpp"
#include "cpu/interrupts.hpp"
#include "cpu/lr35902.hpp"
#include "cpu/registers/flags.hpp"
#include "cpu/registers/register.hpp"
#include "frontend/frontend.hpp"
#include "gbc.hpp"
#include "memory/bus.hpp"
#include "memory/mmio/dmg.hpp"
#include "memory/mmio/mmio.hpp"

namespace py = pybind11;

class PyFrontend final : public Frontend {
public:
  PyFrontend() : Frontend() {}
  std::array<std::uint32_t, 160 * 144> get_frame() override {
    return frame_data;
  }
  void put_pixel(int x, int y, std::uint32_t c) override {
    static constexpr int frame_width = 160;
    frame_data[y * frame_width + x] = c;
  }
  void clear(std::uint32_t c) override {}
  void queue_audio_samples(const float *, std::size_t) override {}
  void start() override {}

private:
  std::array<std::uint32_t, 160 * 144> frame_data{};
};

class PyGameBoyColor {
public:
  PyGameBoyColor() : fe_() {}
  std::array<std::uint32_t, 160 * 144> get_frame() { return fe_.get_frame(); }
  void insert_cartridge(cart c) { fe_.get()->insert_cartridge(c); }
  void init_test_bed() { fe_.get()->init_test_bed(); }
  void step_cycles(int cycles) {
    for (int i{0}; i < cycles; i++)
      step();
  }
  void step() { fe_.get()->step(); }

  AddressBus *get_bus() { return fe_.get()->get_bus(); };
  LR35902 *get_cpu() { return fe_.get()->get_cpu(); };
  PixelProcessingUnit *get_ppu() { return fe_.get()->get_ppu(); }
  TimerUnit *get_timer() { return fe_.get()->get_timer(); }
  void put_joyp_state(std::uint8_t state) {
    auto *joypad = dynamic_cast<Joypad::JOYP *>(
            fe_.get()->get_bus()->get_mmio(IORegisterMapping::MMIO_JOYPAD));
    joypad->set_state(state);
  }

private:
  PyFrontend fe_;
};

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
      .def_readonly("nintendo_logo", &rom_header::nintendo_logo)
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
  py::class_<cart>(m, "Cart")
      .def(py::init<>())
      .def_readonly("file_path", &cart::file_path)
      .def_readonly("header", &cart::header)
      .def_readonly("declared_rom_bytes", &cart::declared_rom_bytes)
      .def_readonly("declared_ram_bytes", &cart::declared_ram_bytes)
      .def_readonly("logo_ok", &cart::logo_ok)
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
      [](const fs::path &rom_path) {
        return load_cart_fs(rom_path);
      },
      py::arg("rom_path"), "Load a Game Boy cartridge from filesystem");

  // CPU Regsiter class
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

  // Expose main Address Bus class
  py::class_<AddressBus>(m, "AddressBus")
      .def("init_test_bed", &AddressBus::init_test_bed)
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
      .def(py::init<AddressBus *, runtime_sys_info &>(), py::arg("bus"),
           py::arg("sys"),
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

  // Timer class
  py::class_<TimerUnit>(m, "TimerUnit")
      .def(py::init<AddressBus *, runtime_sys_info &, bool>(), py::arg("bus"),
           py::arg("sys"), py::arg("cgb_model") = true)
      .def("reset", &TimerUnit::reset)
      .def("step", &TimerUnit::step)
      .def("read_div", &TimerUnit::read_div)
      .def("write_div", &TimerUnit::write_div)
      .def("read_tima", &TimerUnit::read_tima)
      .def("write_tima", &TimerUnit::write_tima)
      .def("read_tma", &TimerUnit::read_tma)
      .def("write_tma", &TimerUnit::write_tma)
      .def("read_tac", &TimerUnit::read_tac)
      .def("write_tac", &TimerUnit::write_tac);

  // Pixel Processor class
  py::class_<PixelProcessingUnit>(m, "PixelProcessor")
      .def(py::init<AddressBus *, Frontend &, runtime_sys_info &>(),
           py::arg("bus"), py::arg("fe"), py::arg("sys"),
           py::keep_alive<1, 2>()) // PixelProcessor keeps AddressBus alive
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

  // For testing
  m.def("poll_mooneye_test", &poll_mooneye_test, py::arg("gbc"),
        "Run the emulator until the Mooneye LD B,B end marker is reached");
}
