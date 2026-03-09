#include "frontend/libretro/frontend.hpp"

// Emulator core includes
#include "cart/cart.hpp"
#include "gbc.hpp"
#include "libretro.h"
#include "memory/mmio/dmg.hpp"
#include "memory/mmio/mmio.hpp"

// Standard includes
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <vector>

/* ======================================================================
 * Start singleton LibretroFrontend implementation
 * ====================================================================== */

LibretroFrontend::LibretroFrontend() { audio_buffer.reserve(4096); }

LibretroFrontend::~LibretroFrontend() {}

std::array<std::uint32_t, 144 * 160> LibretroFrontend::get_frame() {
  return frame_buf.at(display_idx);
}

void LibretroFrontend::put_pixel(int x, int y, std::uint32_t c) {
  if (x < 0 || x >= fb_width || y < 0 || y >= fb_height) [[unlikely]]
    return;
  frame_buf.at(write_idx).at(y * fb_width + x) = c;

  // Swap as frame becomes ready to avoid screen tears
  if (x == fb_width - 1 && y == fb_height - 1) {
    display_idx = write_idx;
    write_idx = (write_idx + 1) % nbuf;
    frame_ready = true;
  }
}

void LibretroFrontend::clear(std::uint32_t c) {
  for (auto &buf : frame_buf)
    buf.fill(c);
  write_idx = display_idx = 0;
  frame_ready = false;
}

void LibretroFrontend::queue_audio_samples(const float *samples,
                                           std::size_t sample_count) {
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

void LibretroFrontend::load_game(cart &c) {
  gbc->insert_cartridge(c); // Save should be ready if it doesnt throw
  initial_state = gbc->savestate_serialize();
  state_size = gbc->savestate_size();
}

void LibretroFrontend::try_show_frame() {
  if (!frame_ready)
    return;
  frame_ready = false;

  /* Note: Do not use `get_frame()`, we need to point to the framebuffer itself
   * and NOT a copy otherwise we risk displaying use after free heap memory. */
  const auto &frame = frame_buf.at(display_idx);
  cb.video_cb(frame.data(), fb_width, fb_height,
              fb_width * sizeof(std::uint32_t));
}

void LibretroFrontend::try_poll_input() {
  std::uint8_t input_state{};
  auto joyp = get_joyp();
  cb.input_poll_cb();

  if (joyp && meta.controller_device == RETRO_DEVICE_JOYPAD) [[likely]] {
    for (const auto &btn : btn_mapping)
      input_state |= static_cast<std::uint8_t>(
                         -static_cast<std::uint8_t>(test_input(btn.retro_id))) &
                     btn.joypad_mask;
    joyp->set_state(input_state);
  }
}

void LibretroFrontend::reset() {
  while (!gbc->savestate_ready())
    gbc->step(); // Only a few hundred cycles max of wait time
  gbc->savestate_deserialize(initial_state);
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
