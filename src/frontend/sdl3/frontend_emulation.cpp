#include "frontend/sdl3/frontend.hpp"
#include "memory/mmio/mmio.hpp"
#include <algorithm>
#include <fstream>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <thread>

namespace {
std::optional<std::vector<byte_t>>
read_binary_blob(const std::filesystem::path &path) {
  std::ifstream file(path, std::ios::binary | std::ios::ate);
  if (!file) {
    return std::nullopt;
  }

  const auto size = file.tellg();
  if (size <= 0) {
    return std::nullopt;
  }

  std::vector<byte_t> buffer(static_cast<std::size_t>(size));
  file.seekg(0, std::ios::beg);
  if (!file.read(reinterpret_cast<char *>(buffer.data()), size)) {
    return std::nullopt;
  }
  return buffer;
}

int queued_audio_ms(const SDL_AudioSpec &spec, const int queued_bytes) {
  const int bytes_per_sample_frame =
      spec.channels * static_cast<int>(sizeof(float));
  const int bytes_per_second = spec.freq * bytes_per_sample_frame;
  return bytes_per_second > 0 ? queued_bytes * 1000 / bytes_per_second : 0;
}
} // namespace

std::tuple<AddressBus *const, Cartridge *const, Joypad::JOYP *const>
SDL3Frontend::build_emulator_instance(
    const cart &cart, const std::optional<std::string> &bios,
    const std::optional<std::filesystem::path> &initial_save_path) {
  (void)initial_save_path;

  if (bios.has_value()) {
    try {
      auto bios_rom = BootROM(bios.value());
      gbc = std::make_unique<GameBoyColor>(*this, bios_rom);
    } catch (const std::runtime_error &e) {
      Logger::push(LogLevel::Warning, "BIOS", "Failed to load BIOS", e.what());
      gui.clear_bios_path();
      gbc = std::make_unique<GameBoyColor>(*this);
    }
  }
  gbc->insert_cartridge(cart);

  auto *const bus_ptr = gbc->get_bus();
  if (!bus_ptr) {
    throw std::runtime_error("Failed to acquire AddressBus resource");
  }

  auto *const cart_ptr = bus_ptr->get_cartridge();
  auto *joyp_ptr = dynamic_cast<Joypad::JOYP *>(
      gbc->get_bus()->get_mmio(IORegisterMapping::MMIO_JOYPAD));
  if (!joyp_ptr) {
    throw std::runtime_error("Failed to acquire Cartridge resource");
  }

  return {bus_ptr, cart_ptr, joyp_ptr};
}

std::vector<byte_t> SDL3Frontend::prime_sram_saves(
    const std::optional<std::filesystem::path> &initial_save_path,
    Cartridge *const cart_ptr) {
  using namespace std::filesystem;

  std::vector<byte_t> save_snapshot;
  if (cart_ptr->has_battery() && initial_save_path.has_value()) {
    const auto &save_path = initial_save_path.value();
    if (!exists(save_path) || !cart_ptr->load_save_file(save_path)) {
      Logger::push(LogLevel::Warning, "Save", "Failed to load save file",
                   "Could not load save data from: " + save_path.string());
    }
  }

  if (cart_ptr->has_battery()) {
    const auto ram_view = cart_ptr->ram();
    save_snapshot.assign(ram_view.begin(), ram_view.end());
  }
  return save_snapshot;
}

void SDL3Frontend::process_sram_save_events(std::vector<byte_t> save_snapshot,
                                            Cartridge *const cart_ptr) {
  if (!cart_ptr->has_battery() || !cart_ptr->consume_sram_save()) {
    return;
  }

  const auto ram_view = cart_ptr->ram();
  if (ram_view.empty()) [[likely]] {
    return;
  }

  const bool altered =
      ram_view.size() != save_snapshot.size() ||
      !std::equal(ram_view.begin(), ram_view.end(), save_snapshot.begin());
  if (altered) [[unlikely]] {
    save_snapshot.assign(ram_view.begin(), ram_view.end());
    enqueue_save_snapshot(std::vector(ram_view.begin(), ram_view.end()));
  }
}

void SDL3Frontend::process_save_state_events() {
  constexpr auto acq = std::memory_order_acq_rel;
  const bool quick_save = quicksave_requested.exchange(false, acq);
  const bool quick_load = quickload_requested.exchange(false, acq);
  const bool manual = manual_preempt_emu_loop.exchange(false, acq);

  const auto save_savestate =
      [this](const bool quick, const std::string_view label) {
        try {
          const auto blob = gbc->savestate_serialize();
          const auto path = write_savestate_bundle(blob, quick,
                                                   std::string(label));
          if (!path.has_value()) {
            Logger::push(LogLevel::Warning, "Savestate", "Failed to create",
                         quick
                             ? "Could not create a quicksave. Check that the "
                               "savestate directory is accessible."
                             : "Could not create a savestate. Check that the "
                               "savestate directory is accessible.");
            return;
          }

          if (quick) {
            Logger::push(LogLevel::Status, "Savestate", "Savestate created",
                         path->string());
          }
        } catch (const std::exception &e) {
          Logger::push(LogLevel::Warning, "Savestate", "Failed to create",
                       e.what());
        }
      };

  const auto load_savestate = [this](const std::filesystem::path &path,
                                     const std::string &missing_summary,
                                     const std::string &missing_message) {
    try {
      const auto blob = read_binary_blob(path);
      if (!blob.has_value()) {
        Logger::push(LogLevel::Warning, "Savestate", missing_summary,
                     missing_message);
        return;
      }

      gbc->savestate_deserialize(*blob);
      host.clear_audio_stream();
      video_dirty.store(true, std::memory_order_release);
      Logger::push(LogLevel::Status, "Savestate", "Savestate loaded",
                   path.string());
    } catch (const std::exception &e) {
      Logger::push(LogLevel::Warning, "Savestate", "Load failed", e.what());
    }
  };

  if (quick_save) [[unlikely]] {
    save_savestate(true, {});
    return;
  }

  if (quick_load) [[unlikely]] {
    const auto latest_path = latest_savestate_path();
    if (!latest_path.has_value()) {
      Logger::push(LogLevel::Status, "Savestate", "No savestate available",
                   "No savestate is available to load.");
      return;
    }

    load_savestate(*latest_path,
                   "Inaccessible",
                   "Could not read savestate from: " + latest_path->string() +
                       ". It may have been moved or deleted.");
    return;
  }

  if (!manual) [[likely]] {
    return;
  }

  // Manual actions are queued from the UI thread but executed only once the
  // core reports a savestate-safe boundary. That avoids serializing or
  // deserializing half-updated subsystem state.
  if (const auto manual_save_label = consume_manual_savestate_request();
      manual_save_label.has_value()) {
    save_savestate(false, *manual_save_label);
    return;
  }

  if (const auto manual_load_path = consume_savestate_load_request();
      manual_load_path.has_value()) {
    load_savestate(*manual_load_path, "File not found",
                   "Could not read savestate from: " +
                       manual_load_path->string());
  }
}

std::vector<GameBoyColor::CheatCode> SDL3Frontend::snapshot_cheats_locked() const {
  std::vector<GameBoyColor::CheatCode> out;
  const auto &src = gui.get_settings_c().cheats;
  out.reserve(src.size());
  for (const auto &entry : src) {
    out.push_back(GameBoyColor::CheatCode{
        .enabled = entry.enabled,
        .code = entry.code,
        .format = entry.format,
    });
  }
  return out;
}

void SDL3Frontend::sync_cheats_to_core(std::uint64_t &last_revision) const {
  if (!gbc) {
    return;
  }

  const auto revision = cheats_revision_.load(std::memory_order_acquire);
  if (revision == last_revision) {
    return;
  }

  std::vector<GameBoyColor::CheatCode> snapshot;
  {
    std::lock_guard lock(ui_mutex);
    snapshot = snapshot_cheats_locked();
  }
  gbc->configure_cheats(snapshot);
  last_revision = revision;
}

void SDL3Frontend::emulation_thread_fn(
    const std::stop_token &st, const cart &cart,
    const std::optional<std::string> &bios,
    const std::optional<std::filesystem::path> &initial_save_path) {
  auto next_save_poll = Clock::now();
  std::uint64_t cheat_revision_seen = 0;
  bool ff = false;

  clear(black);
  front_index.store(0, std::memory_order_relaxed);
  host.clear_audio_stream();

  quicksave_requested.store(false, std::memory_order_relaxed);
  quickload_requested.store(false, std::memory_order_relaxed);

  const auto [bus_ptr, cart_ptr, joyp_ptr] =
      build_emulator_instance(cart, bios, initial_save_path);
  (void)bus_ptr;

  gbc->configure_debugger(Debug::Debugger([this, st]() -> Debug::BreakReason {
    return debugger.on_breakpoint(st, gbc);
  }));
  sync_cheats_to_core(cheat_revision_seen);

  std::vector<byte_t> last_saved_snapshot =
      prime_sram_saves(initial_save_path, cart_ptr);

  while (!st.stop_requested()) [[likely]] {
    sync_cheats_to_core(cheat_revision_seen);

    // The queued audio depth acts as our pacing clock. If audio is already
    // sufficiently buffered, let the host drain it before emulating more time.
    if (!ff && queued_audio_ms(host.get_audio_spec(),
                               host.get_queued_audio_bytes()) > target_queue_ms) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
      continue;
    }

    joyp_ptr->set_state(input_state.buttons.load(std::memory_order_relaxed));
    advance_emulator_core(max_catchup_cycles);

    is_cgb.store(gbc->get_sys().cgb_mode, std::memory_order_relaxed);
    ff = fast_forward.load(std::memory_order_relaxed);
    debugger.forward_stop(gbc);

    if (const auto now = Clock::now(); now >= next_save_poll) {
      constexpr auto polling_period = std::chrono::milliseconds(250);
      process_sram_save_events(last_saved_snapshot, cart_ptr);
      next_save_poll = now + polling_period;
    }
  }

  process_sram_save_events(last_saved_snapshot, cart_ptr);
}

void SDL3Frontend::advance_emulator_core(int cycles) {
  const bool preempt = should_preempt_emu_loop();

  // Save/load requests are honored only once the core says its savestate view
  // is coherent. If that boundary is reached, stop the current catch-up slice
  // instead of running more cycles on stale input.
  for (auto i = 0; i < cycles; ++i) {
    gbc->step();
    if (preempt && gbc->savestate_ready()) [[unlikely]] {
      process_save_state_events();
      return;
    }
  }
}

void SDL3Frontend::join_emu_thread_if_running() {
  if (!emulation_thread.joinable()) {
    return;
  }

  emulation_thread.request_stop();
  debugger.request_stop();
  emulation_thread.join();
}
