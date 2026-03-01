#include "frontend/libretro/frontend.hpp"
#include "frontend/libretro/frontend.h"

// Emulator core includes
#include "cart/cart.hpp"
#include "emu_types.hpp"
#include "libretro.h"

// Standard includes
#include <cstddef>
#include <cstdint>
#include <vector>

void irogb_retro_init(void) {}

void irogb_retro_deinit(void) {}

void iorgb_retro_set_environment(retro_environment_t cb) {
  auto &meta = LibretroFrontend::getInstance().get_libretro_meta();
  meta.environ_cb = cb;

  enum retro_pixel_format fmt = RETRO_PIXEL_FORMAT_XRGB8888;
  if (!meta.environ_cb(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &fmt))
    fprintf(stderr, "Failed to set pixel format\n");
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
  constexpr std::size_t cycles_per_frame = 70224;
  auto &instance = LibretroFrontend::getInstance();
  auto &gbc = instance.get();

  for (std::size_t i{0}; i < cycles_per_frame; i++)
    gbc->step();
  instance.present();

  // TODO: This is a hack to get the framerate right for now
  constexpr auto fps = 60.0f;
  constexpr auto sample_rate = 44100.0;
  constexpr std::size_t samples_per_frame = sample_rate / fps;
  static std::int16_t silence[samples_per_frame * 2] = {0};
  instance.get_libretro_meta().audio_batch_cb(silence, samples_per_frame);
}

LibretroFrontend::LibretroFrontend() {}

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

void LibretroFrontend::present() {
  if (!frame_ready)
    return;
  frame_ready = false;

  const auto frame = get_frame();
  libretro.video_cb(frame.data(), 160, 144, 160 * sizeof(std::uint32_t));
}

void LibretroFrontend::queue_audio_samples(const float *samples,
                                           std::size_t sample_count) {}

void LibretroFrontend::start() { /* unused */ }
