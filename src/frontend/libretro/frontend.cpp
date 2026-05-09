#include "frontend/libretro/frontend.hpp"

// Emulator core includes
#include "cart/cart.hpp"
#include "frontend/libretro/libretro.h"
#include "frontend/logger.hpp"
#include "gbc.hpp"
#include "memory/mmio/dmg.hpp"
#include "memory/mmio/mmio.hpp"
#include "ppu/palette.hpp"

// Standard includes
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

/* ======================================================================
 * Start singleton LibretroFrontend implementation
 * ====================================================================== */

LibretroFrontend::LibretroFrontend() {
  audio_buffer.reserve(4096);
  cheat_codes.clear();
}

LibretroFrontend::~LibretroFrontend() {}

void LibretroFrontend::make_gbc(std::optional<BootROM> bios) {
  if (bios.has_value()) {
    gbc = std::make_unique<GameBoyColor>(*this, bios.value());
  } else {
    gbc = std::make_unique<GameBoyColor>(*this);
  }
}

std::array<std::uint32_t, 144 * 160> LibretroFrontend::get_frame() {
  return frame_buf.at(display_idx);
}

std::uint32_t LibretroFrontend::format_pixel_data(const std::uint32_t px) {
  constexpr std::uint32_t alpha_mask = 0xFF000000; // Still abusing alpha bits lol
  const bool is_cgb = gbc->get_sys().cgb_mode;

  if (!is_cgb && force_mono_dmg) {
    const auto mono_pal_idx = static_cast<byte_t>((px >> 24) & 0xFF);
    return get_mono_color(mono_pal_idx) | alpha_mask;
  }
  return px | alpha_mask;
}

void LibretroFrontend::put_pixel(int x, int y, std::uint32_t c) {
  frame_buf.at(write_idx).at(y * fb_width + x) = c;

  // Swap as frame becomes ready to avoid screen tears
  if (x == fb_width - 1 && y == fb_height - 1) [[unlikely]] {
    display_idx = write_idx;
    write_idx = (write_idx + 1) & (nbuf - 1);
    frame_ready = true;
  }
}

void LibretroFrontend::clear(std::uint32_t c) {
  for (auto &buf : frame_buf)
    buf.fill(c);
  write_idx = display_idx = 0;
  frame_ready = false;
}

void LibretroFrontend::queue_audio_samples(const float *samples, std::size_t sample_count) {
  if (!samples || sample_count == 0 || sample_count % 2 != 0)
    return;
  const std::size_t frames = sample_count / 2;
  audio_buffer.resize(sample_count);

  for (std::size_t i{0}; i < sample_count; ++i) {
    float s = samples[i];
    s = std::clamp(s, -1.0f, 1.0f);
    audio_buffer[i] = static_cast<std::int16_t>(s * 16383.0f);
  }
  cb.audio_batch_cb(audio_buffer.data(), frames);
}

[[nodiscard]] std::vector<byte_t> LibretroFrontend::take_snapshot() const {
  while (!gbc->savestate_ready())
    gbc->step(); // Only a few hundred cycles max of wait time max
  return gbc->savestate_serialize();
}

void LibretroFrontend::restore_snapshot(std::span<const byte_t> snapshot) {
  gbc->savestate_deserialize(snapshot);
}

const std::string LibretroFrontend::get_bios_option() {
  retro_variable var = {
      .key = "irogb_bios",
      .value = nullptr,
  };

  auto &callbacks = LibretroFrontend::get_instance().get_callbacks();
  if (callbacks.environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
    return var.value;
  return "auto";
}

const bool LibretroFrontend::get_force_mono_option() {
  retro_variable var = {
      .key = "irogb_monochrome_dmg",
      .value = nullptr,
  };

  auto &callbacks = LibretroFrontend::get_instance().get_callbacks();
  if (callbacks.environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
    return strcmp(var.value, "enabled") == 0; // hate it but whatever lol
  return false;
}

void LibretroFrontend::cheat_set(std::size_t index, bool enabled, std::string &code) {
  if (index >= cheat_codes.size())
    cheat_codes.resize(index + 1);

  cheat_codes.at(index) = {
      .enabled = enabled,
      .code = code,
      .format = GameBoyColor::CheatFormat::CHEAT_AUTO,
  };
  gbc->configure_cheats(cheat_codes);
}

void LibretroFrontend::cheat_reset() {
  cheat_codes.clear(); // Wipe internal data structures clean
  gbc->configure_cheats(cheat_codes);
}

void LibretroFrontend::load_game(cart &c) {
  gbc->insert_cartridge(c); // Save should be ready if it doesnt throw
  initial_state = take_snapshot();
  state_size = gbc->savestate_size();
}

void LibretroFrontend::try_show_frame() {
  if (!frame_ready)
    return;

  // We modify the frame in place to update replace the colors based on
  // configurable color palette options.
  auto &frame = frame_buf.at(display_idx);
  frame_ready = false;

  // Apply the color transformation to the entire frame once its complete
  std::transform(frame.begin(), frame.end(), frame.begin(), [&](std::uint32_t px) {
    constexpr std::uint32_t alpha_mask = 0xFF000000;
    const bool is_cgb = gbc->get_sys().cgb_mode;

    if (!is_cgb && get_force_mono_option()) {
      const auto mono_pal_idx = static_cast<byte_t>((px >> 24) & 0xFF);
      px = get_mono_color(mono_pal_idx); // Substitution
    }

    // Always bring the alpha bits back
    return px | alpha_mask;
  });

  cb.video_cb(frame.data(), fb_width, fb_height, fb_width * sizeof(std::uint32_t));
}

void LibretroFrontend::try_poll_input() {
  std::uint8_t input_state{};
  auto joyp = get_joyp();
  cb.input_poll_cb();

  if (joyp && meta.controller_device == RETRO_DEVICE_JOYPAD) [[likely]] {
    for (const auto &btn : btn_mapping)
      input_state |=
          static_cast<std::uint8_t>(-static_cast<std::uint8_t>(test_input(btn.retro_id))) &
          btn.joypad_mask;
    joyp->set_state(input_state);
  }
}

void LibretroFrontend::show_message(std::string msg, unsigned millis, retro_log_level level) {
  const retro_message_target target =
      level == RETRO_LOG_DEBUG ? RETRO_MESSAGE_TARGET_LOG : RETRO_MESSAGE_TARGET_ALL;

  retro_message_ext ext = {
      .msg = msg.c_str(),
      .duration = millis,
      .priority = 0,
      .level = level,
      .target = target,
      .type = RETRO_MESSAGE_TYPE_NOTIFICATION,
      .progress = 0,
  };
  get_callbacks().environ_cb(RETRO_ENVIRONMENT_SET_MESSAGE_EXT, &ext);
}

void LibretroFrontend::log(std::string msg, unsigned millis, retro_log_level level) {}

void LibretroFrontend::clean_msg_queue() {
  const auto message_queue = Logger::consume();
  if (message_queue.empty()) [[likely]]
    return;

  for (const auto &msg : message_queue) {
    retro_log_level level{};

    switch (msg.level) {
    case LogLevel::Debug:
      level = RETRO_LOG_DEBUG;
      break;
    case LogLevel::Info:
      level = RETRO_LOG_INFO;
      break;
    case LogLevel::Status:
      level = RETRO_LOG_INFO;
      break;
    case LogLevel::Warning:
      level = RETRO_LOG_WARN;
      break;
    case LogLevel::Error:
      level = RETRO_LOG_ERROR;
      break;
    }
    show_message(msg.message, msg_duration_sec(5), level);
  }
}

bool LibretroFrontend::test_input(unsigned id) const {
  return cb.input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, id);
}

Joypad::JOYP *LibretroFrontend::get_joyp() const {
  auto bus = gbc->get_bus();
  if (!bus)
    return nullptr;

  auto joyp = bus->get_mmio(IORegisterMapping::MMIO_JOYPAD);
  return static_cast<Joypad::JOYP *>(joyp);
}

void LibretroFrontend::start() { /* unused */ }
