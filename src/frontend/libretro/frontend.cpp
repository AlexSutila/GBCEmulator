#include "frontend/libretro/frontend.hpp"
#include "frontend/libretro/frontend.h"

// Emulator core includes
#include "cart/cart.hpp"
#include "emu_types.hpp"
#include "gbc.hpp"
#include "libretro.h"
#include "memory/mmio/dmg.hpp"
#include "memory/mmio/mmio.hpp"

// Standard includes
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

struct ButtonMap {
  unsigned retro_id;
  std::uint8_t joypad_mask;
};

static constexpr ButtonMap btn_mapping[] = {
    {RETRO_DEVICE_ID_JOYPAD_A,
     static_cast<std::uint8_t>(Joypad::JoypadButton::A)},
    {RETRO_DEVICE_ID_JOYPAD_B,
     static_cast<std::uint8_t>(Joypad::JoypadButton::B)},
    {RETRO_DEVICE_ID_JOYPAD_START,
     static_cast<std::uint8_t>(Joypad::JoypadButton::START)},
    {RETRO_DEVICE_ID_JOYPAD_SELECT,
     static_cast<std::uint8_t>(Joypad::JoypadButton::SELECT)},
    {RETRO_DEVICE_ID_JOYPAD_UP,
     static_cast<std::uint8_t>(Joypad::JoypadButton::UP)},
    {RETRO_DEVICE_ID_JOYPAD_DOWN,
     static_cast<std::uint8_t>(Joypad::JoypadButton::DOWN)},
    {RETRO_DEVICE_ID_JOYPAD_LEFT,
     static_cast<std::uint8_t>(Joypad::JoypadButton::LEFT)},
    {RETRO_DEVICE_ID_JOYPAD_RIGHT,
     static_cast<std::uint8_t>(Joypad::JoypadButton::RIGHT)},
};

/* ======================================================================
 * Start implementation of C-header (exposed directly to libretro)
 * ====================================================================== */

void irogb_retro_init(void) {}

void irogb_retro_deinit(void) {}

void irogb_retro_set_controller_port_device(unsigned port, unsigned device) {
  auto &meta = LibretroFrontend::get_instance().get_meta();
  if (port < 1)
    meta.controller_device = device;
}

void iorgb_retro_set_environment(retro_environment_t cb) {
  auto &callbacks = LibretroFrontend::get_instance().get_callbacks();
  callbacks.environ_cb = cb;

  static const retro_controller_description port1[] = {
      {"Game Boy Joypad", RETRO_DEVICE_JOYPAD}, {nullptr, 0}};
  static const retro_controller_info ports[] = {{port1, 1}, {nullptr, 0}};
  if (!callbacks.environ_cb(RETRO_ENVIRONMENT_SET_CONTROLLER_INFO,
                            (void *)ports)) {
    fprintf(stderr, "Failed to configure controller\n");
  }

  enum retro_pixel_format fmt = RETRO_PIXEL_FORMAT_XRGB8888;
  if (!callbacks.environ_cb(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &fmt)) {
    fprintf(stderr, "Failed to set pixel format\n");
  }
}

void iorgb_retro_set_audio_sample(retro_audio_sample_t cb) {
  auto &callbacks = LibretroFrontend::get_instance().get_callbacks();
  callbacks.audio_cb = cb;
}

void iorgb_retro_set_audio_sample_batch(retro_audio_sample_batch_t cb) {
  auto &callbacks = LibretroFrontend::get_instance().get_callbacks();
  callbacks.audio_batch_cb = cb;
}

void iorgb_retro_set_input_poll(retro_input_poll_t cb) {
  auto &callbacks = LibretroFrontend::get_instance().get_callbacks();
  callbacks.input_poll_cb = cb;
}

void iorgb_retro_set_input_state(retro_input_state_t cb) {
  auto &callbacks = LibretroFrontend::get_instance().get_callbacks();
  callbacks.input_state_cb = cb;
}

void iorgb_retro_set_video_refresh(retro_video_refresh_t cb) {
  auto &callbacks = LibretroFrontend::get_instance().get_callbacks();
  callbacks.video_cb = cb;
}

bool irogb_retro_load_game(const void *data, size_t size) {
  auto data_ptr = reinterpret_cast<const byte_t *>(data);

  if (!data_ptr || size == 0)
    return false;

  std::vector<byte_t> raw(data_ptr, data_ptr + size);
  auto &gbc = LibretroFrontend::get_instance().get();

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
  auto &instance = LibretroFrontend::get_instance();
  for (std::size_t i{0}; i < cycles_per_frame; i++)
    instance.get()->step(); // Step one 'frame'

  instance.try_show_frame();
  instance.try_poll_input();
}

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

void LibretroFrontend::try_show_frame() {
  if (!frame_ready)
    return;
  frame_ready = false;

  const auto frame = get_frame();
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
