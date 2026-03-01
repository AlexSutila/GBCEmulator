#include "frontend/libretro/frontend.hpp"
#include "frontend/libretro/frontend.h"

// Emulator core includes
#include "cart/cart.hpp"
#include "emu_types.hpp"

// Standard includes
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <vector>

void irogb_retro_init(void) {}

void irogb_retro_deinit(void) {}

void iorgb_retro_set_environment(retro_environment_t cb) {
  auto &meta = LibretroFrontend::getInstance().get_libretro_meta();
  meta.environ_cb = cb;
}

void iorgb_retro_set_audio_sample(retro_audio_sample_t cb) {
  auto &meta = LibretroFrontend::getInstance().get_libretro_meta();
  meta.audio_cb = cb;
}

void iorgb_retro_set_audio_sample_batch(retro_audio_sample_batch_t cb) {
  auto &meta = LibretroFrontend::getInstance().get_libretro_meta();
  meta.audio_batch_cb = cb;
}

void iorgb_retro_set_input_poll(retro_input_poll_t cb) {
  auto &meta = LibretroFrontend::getInstance().get_libretro_meta();
  meta.input_poll_cb = cb;
}

void iorgb_retro_set_input_state(retro_input_state_t cb) {
  auto &meta = LibretroFrontend::getInstance().get_libretro_meta();
  meta.input_state_cb = cb;
}

void iorgb_retro_set_video_refresh(retro_video_refresh_t cb) {
  auto &meta = LibretroFrontend::getInstance().get_libretro_meta();
  meta.video_cb = cb;
}

bool irogb_retro_load_game(const void *data, size_t size) {
  auto data_ptr = reinterpret_cast<const byte_t *>(data);

  if (!data_ptr || size == 0)
    return false;

  std::vector<byte_t> raw(data_ptr, data_ptr + size);
  auto &gbc = LibretroFrontend::getInstance().get();

  try {
    cart c = load_cart_raw(raw);
    gbc->insert_cartridge(c);
  } catch (...) {
    return false;
  }

  return true;
}

void irogb_retro_run(void) {
  auto &instance = LibretroFrontend::getInstance();
  auto frame = instance.get_frame();
  instance.get_libretro_meta().video_cb(frame.data(), 160, 144,
                                        160 * sizeof(std::uint32_t));
}

LibretroFrontend::LibretroFrontend() {}

LibretroFrontend::~LibretroFrontend() {}

std::array<std::uint32_t, 144 * 160> LibretroFrontend::get_frame() {
  return frame_buf.at(display_idx);
}

void LibretroFrontend::put_pixel(int x, int y, std::uint32_t c) {}

void LibretroFrontend::clear(std::uint32_t c) {}

void LibretroFrontend::start() {}

void LibretroFrontend::queue_audio_samples(const float *samples,
                                           std::size_t sample_count) {}

