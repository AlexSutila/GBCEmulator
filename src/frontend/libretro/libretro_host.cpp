#include "frontend/libretro/frontend.hpp"
#include <algorithm>
#include <memory>
#include <optional>
#include <stdarg.h>
#include <stdexcept>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "emu_types.hpp"
#include "gbc.hpp"
#include "memory/boot.hpp"

static Cartridge *const get_cart(void) {
  auto &gbc = LibretroFrontend::get_instance().get();
  if (!gbc)
    return nullptr;

  auto bus = gbc->get_bus();
  if (!bus)
    return nullptr;

  // Caller should check for `NULL` or `nullptr`
  return bus->get_cartridge();
}

#ifdef __cplusplus
extern "C" {
#endif

/* ======================================================================
 * Start implementation of C-header (exposed directly to libretro)
 * ====================================================================== */
#include "frontend/libretro/libretro.h"

static std::string get_system_dir() {
  auto &callbacks = LibretroFrontend::get_instance().get_callbacks();

  const char *dir = nullptr;
  if (callbacks.environ_cb && callbacks.environ_cb(RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY, &dir))
    return dir;

  // Fine, bios is optional
  return {};
}

static std::optional<BootROM> load_bios(const std::string &name) {
  const auto system_dir = get_system_dir();
  if (system_dir.empty())
    return std::nullopt;

  // const auto path = std::filesystem::path(system_dir) / name;
  std::string path = system_dir;
  if (!path.empty() && path.back() != '/')
    path += '/';
  path += name;

  /* Really wish I could leverage std::filesystem here, but we run into problems
   * when compiling for older iOS versions, so we do this for the pure sake of
   * maintaining compatability */
  try {
    auto bios = BootROM(path);
    return bios;
  }

  // Ignore invalid BIOS files
  catch (std::runtime_error &e) {
    return std::nullopt;
  }
}

static void initialize_gbc_instance(LibretroFrontend &instance) {
  auto mode = instance.get_bios_option();

  // DMG bios, or custom stand-in
  if (mode == "dmg") {
    auto bios = load_bios("dmg_boot.bin");
    instance.make_gbc(bios);
  }

  // CGB bios, or custom stand-in
  else if (mode == "cgb") {
    auto bios = load_bios("cgb_boot.bin");
    instance.make_gbc(bios);
  }

  // Select between CGB and DMG based on what is available, could fallback to
  // none, prefers CGB over DMG if it is present (coloring could get bizzare).
  else if (mode == "auto") {
    auto bios = load_bios("cgb_boot.bin");
    if (!bios.has_value())
      bios = load_bios("dmg_boot.bin");
    instance.make_gbc(bios);
  }

  // Ignore BIOS entirely, just uses our "faked" built-in initialization state
  else
    instance.make_gbc(std::nullopt);
}

void retro_init(void) {}

void retro_deinit(void) {}

unsigned retro_api_version(void) { return RETRO_API_VERSION; }

void retro_set_controller_port_device(unsigned port, unsigned device) {
  auto &meta = LibretroFrontend::get_instance().get_meta();
  if (port < 1)
    meta.controller_device = device;
}

void retro_get_system_info(struct retro_system_info *info) {
  memset(info, 0, sizeof(*info));
  info->library_name = "IroGB";
  info->library_version = "v1.0.0";
  info->need_fullpath = false;
  info->valid_extensions = "gb|gbc|zip";
}

void retro_get_system_av_info(struct retro_system_av_info *info) {
  memset(info, 0, sizeof(*info));
  info->timing = (struct retro_system_timing){
      .fps = 60.0,
      .sample_rate = 48000.0,
  };
  info->geometry = (struct retro_game_geometry){
      .base_width = 160,
      .base_height = 144,
      .max_width = 160,
      .max_height = 144,
      .aspect_ratio = 160.0f / 144.0f,
  };
}

void retro_set_environment(retro_environment_t cb) {
  auto &callbacks = LibretroFrontend::get_instance().get_callbacks();
  callbacks.environ_cb = cb;

  static struct retro_core_option_v2_definition option_defs[] = {
      {.key = "irogb_bios",
       .desc = "Select which BIOS to use (if available).",
       .desc_categorized = NULL,
       .info_categorized = NULL,
       .category_key = NULL,
       .values =
           {
               {"auto", "Auto"},
               {"dmg", "DMG BIOS"},
               {"cgb", "CGB BIOS"},
               {"none", "Skip BIOS"},
               {NULL, NULL},
           }, // ...
       .default_value = "auto"},
      {.key = "irogb_monochrome_dmg",
       .desc = "Disable re-coloring of DMG games (only applicable with CGB BIOS)",
       .desc_categorized = NULL,
       .info_categorized = NULL,
       .category_key = NULL,
       .values =
           {
               {"enabled", "enabled"},
               {"disabled", "disabled"},
               {NULL, NULL},
           }, // ...
       .default_value = "disabled"},
      {0}
  };
  static struct retro_core_options_v2 options = {
      NULL,
      option_defs,
  };

  static const retro_controller_description port1[] = {
      {"Game Boy Joypad", RETRO_DEVICE_JOYPAD},
      {          nullptr,                   0}
  };
  static const retro_controller_info ports[] = {
      {  port1, 1},
      {nullptr, 0}
  };
  callbacks.environ_cb(RETRO_ENVIRONMENT_SET_CONTROLLER_INFO, (void *)ports);

  enum retro_pixel_format fmt = RETRO_PIXEL_FORMAT_XRGB8888;
  callbacks.environ_cb(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &fmt);
  callbacks.environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2, (void *)&options);
}

void retro_set_audio_sample(retro_audio_sample_t cb) {
  auto &callbacks = LibretroFrontend::get_instance().get_callbacks();
  callbacks.audio_cb = cb;
}

void retro_set_audio_sample_batch(retro_audio_sample_batch_t cb) {
  auto &callbacks = LibretroFrontend::get_instance().get_callbacks();
  callbacks.audio_batch_cb = cb;
}

void retro_set_input_poll(retro_input_poll_t cb) {
  auto &callbacks = LibretroFrontend::get_instance().get_callbacks();
  callbacks.input_poll_cb = cb;
}

void retro_set_input_state(retro_input_state_t cb) {
  auto &callbacks = LibretroFrontend::get_instance().get_callbacks();
  callbacks.input_state_cb = cb;
}

void retro_set_video_refresh(retro_video_refresh_t cb) {
  auto &callbacks = LibretroFrontend::get_instance().get_callbacks();
  callbacks.video_cb = cb;
}

void retro_reset(void) {
  Cartridge *cartridge = get_cart();
  if (cartridge == nullptr)
    return;
  std::vector<byte_t> sram_backup{};
  cart c = cartridge->image();

  /* Here, we back up the existing SRAM content so that the save data is not lost
   * in case libretro's periodic sampling misses it or samples fresh SRAM */
  if (cartridge->has_battery()) {
    std::span<byte_t> src_sram = cartridge->ram();
    sram_backup.assign(src_sram.begin(), src_sram.end());
  }

  /* CGB models do not have soft reset buttons, so we resort to hard reset only.
   * To support this, we have to pull the original image back down, recreate the
   * emulator instance, and re-insert the cartridge. */
  auto &instance = LibretroFrontend::get_instance();
  initialize_gbc_instance(instance);
  instance.load_game(c); // Cart will be valid

  // Cartridge was re-allocated, so have to pull down it again
  cartridge = get_cart();
  if (cartridge == nullptr)
    return;

  /* Finally, restore sram content if it was saved */
  if (!sram_backup.empty()) {
    std::span<byte_t> dest_sram = cartridge->ram();
    std::copy(sram_backup.begin(), sram_backup.end(), dest_sram.begin());
  }
}

void retro_run(void) {
  constexpr std::size_t cycles_per_frame = 70224;
  auto &instance = LibretroFrontend::get_instance();
  for (std::size_t i{0}; i < cycles_per_frame; i++)
    instance.get()->step(); // Step one 'frame'

  instance.try_show_frame();
  instance.try_poll_input();
}

bool retro_load_game(const struct retro_game_info *info) {
  if (!info)
    return false;
  const void *const data = info->data;
  const auto size = info->size;

  auto data_ptr = reinterpret_cast<const byte_t *>(data);
  if (!data_ptr || size == 0)
    return false;

  std::vector<byte_t> raw(data_ptr, data_ptr + size);
  auto &instance = LibretroFrontend::get_instance();

  /* Attempt to load a BIOS file, we check two locations. If any of these fail,
   * for any reason, it is equivalent to starting without a BIOS file. */
  initialize_gbc_instance(instance);

  try {
    cart c = load_cart_raw(raw);
    instance.load_game(c);
  } catch (...) {
    return false;
  }
  return true;
}

/* You technically should not remove a cartridge before completely powering off
 * the system, so we ignore this. Nothing needs to happen within the core. */
void retro_unload_game(void) {}

/* Does not matter, GBC does not rely on such television standards */
unsigned retro_get_region(void) { return RETRO_REGION_NTSC; }

/* Not applicable */
bool retro_load_game_special(unsigned type, const struct retro_game_info *info, size_t num) {
  return false;
}

size_t retro_serialize_size(void) {
  auto &instance = LibretroFrontend::get_instance();
  return instance.get_state_size();
}

bool retro_serialize(void *data_, size_t size) {
  auto &instance = LibretroFrontend::get_instance();
  const auto snapshot = instance.take_snapshot();
  if (size < snapshot.size()) [[unlikely]]
    return false;

  // std::memcpy(data_, snapshot.data(), snapshot.size()); - lolno
  std::span<byte_t> dst(static_cast<byte_t *>(data_), snapshot.size());
  std::ranges::copy(snapshot, dst.begin());
  return true;
}

bool retro_unserialize(const void *data_, size_t size) {
  auto &instance = LibretroFrontend::get_instance();
  try {
    std::span<const byte_t> snapshot(static_cast<const byte_t *>(data_), size);
    instance.restore_snapshot(snapshot);
    return true;

    // Could be invalid field/chunk version or save state corruption
  } catch (...) {
    return false;
  }
}

void *retro_get_memory_data(unsigned id) {
  if (id == RETRO_MEMORY_SAVE_RAM) {
    if (auto cart = get_cart(); cart && cart->has_battery()) {
      const auto sram = cart->ram();
      return sram.data();
    }
  }

  // TODO: Restore RTC
  return NULL;
}

size_t retro_get_memory_size(unsigned id) {
  if (id == RETRO_MEMORY_SAVE_RAM) {
    if (auto cart = get_cart(); cart && cart->has_battery()) {
      const auto sram = cart->ram();
      return sram.size_bytes();
    }
  }

  // TODO: Restore RTC
  return 0;
}

void retro_cheat_reset(void) {
  auto &instance = LibretroFrontend::get_instance();
  instance.cheat_reset();
}

void retro_cheat_set(unsigned index, bool enabled, const char *code) {
  auto &instance = LibretroFrontend::get_instance();
  if (code) {
    std::string code_str(code);
    instance.cheat_set(index, enabled, code_str);
  }
}

#ifdef __cplusplus
}
#endif
