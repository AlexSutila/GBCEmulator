#include "gbc.hpp"
#include "cart/cart.hpp"
#include "cpu/lr35902.hpp"
#include "frontend/renderer.hpp"
#include "memory/bus.hpp"
#include "ppu/ppu.hpp"
#include "timer/timer.hpp"
#include <chrono>
#include <memory>
#include <string>
#include <thread>

GameBoyColor::GameBoyColor(bool headless) {
  renderer = std::make_unique<Renderer>(headless);

  /* General system operation info */
  sys = {
      .double_speed = false,
      .cgb_mode = true,
      .elapsed_clocks = 0,
  };

  /* Component initializaiton */
  bus = std::make_unique<AddressBus>(sys);
  cpu = std::make_unique<LR35902>(bus.get(), sys);
  ppu = std::make_unique<PixelProcessingUnit>(bus.get(), renderer.get(), sys);
  timer = std::make_unique<TimerUnit>(bus.get(), sys);
  has_cartridge = false;
}

void GameBoyColor::insert_cartridge(cart c) {
  if (!bus)
    throw std::logic_error("Bus not initialized");
  bus->insert_cartridge(c);
  has_cartridge = true;
}

void GameBoyColor::init_test_bed() {
  if (!bus)
    throw std::logic_error("Bus not initialized");

  /* Init convenience RAM-only cartridge for testing */
  bus->init_test_bed();
  has_cartridge = true;
}

void GameBoyColor::step() {
  cpu->step();

  // Drives any DMA along that may be currently active
  bus->step_dma();

  // Step remaning components
  ppu->step();
  timer->step();

  // System clocks are maintained in unit `t-cycles`
  ++sys.elapsed_clocks;
}

void GameBoyColor::run() {
  auto emulation_loop = [this]() {
    constexpr std::uint64_t cycles_per_frame = 70224;
    auto last_frame_time = std::chrono::steady_clock::now();
    std::uint64_t last_frame_cycles = sys.elapsed_clocks;
    while (renderer->get_running()) [[likely]] {
      std::string rom_path;
      if (renderer->consume_load_request(rom_path)) {
        try {
          cart loaded = load_cart_fs(rom_path.c_str());
          insert_cartridge(loaded);
          renderer->set_status_message("ROM loaded.");
        } catch (const std::exception &e) {
          renderer->set_status_message(
              std::string("Failed to load ROM: ") + e.what());
        }
      }

      if (has_cartridge) {
        step();
        if (sys.elapsed_clocks - last_frame_cycles >= cycles_per_frame) {
          last_frame_cycles = sys.elapsed_clocks;
          const auto target_time =
              last_frame_time + std::chrono::microseconds(16667);
          std::this_thread::sleep_until(target_time);
          last_frame_time = std::chrono::steady_clock::now();
        }
      } else {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
      }
    }
  };

  if (renderer->is_headless()) {
    emulation_loop();
    return;
  }

  std::thread emu_thread(emulation_loop);
  while (renderer->get_running()) [[likely]] {
    renderer->present();
  }
  emu_thread.join();
}