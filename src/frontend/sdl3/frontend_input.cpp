#include "frontend/sdl3/frontend.hpp"
#include <algorithm>

namespace {
byte_t controller_mask_for_button(const SDL_GamepadButton button) {
  switch (button) {
  case SDL_GAMEPAD_BUTTON_DPAD_UP:
    return static_cast<byte_t>(Joypad::JoypadButton::UP);
  case SDL_GAMEPAD_BUTTON_DPAD_DOWN:
    return static_cast<byte_t>(Joypad::JoypadButton::DOWN);
  case SDL_GAMEPAD_BUTTON_DPAD_LEFT:
    return static_cast<byte_t>(Joypad::JoypadButton::LEFT);
  case SDL_GAMEPAD_BUTTON_DPAD_RIGHT:
    return static_cast<byte_t>(Joypad::JoypadButton::RIGHT);
  case SDL_GAMEPAD_BUTTON_SOUTH:
  case SDL_GAMEPAD_BUTTON_WEST:
    return static_cast<byte_t>(Joypad::JoypadButton::B);
  case SDL_GAMEPAD_BUTTON_NORTH:
  case SDL_GAMEPAD_BUTTON_EAST:
    return static_cast<byte_t>(Joypad::JoypadButton::A);
  case SDL_GAMEPAD_BUTTON_START:
    return static_cast<byte_t>(Joypad::JoypadButton::START);
  case SDL_GAMEPAD_BUTTON_BACK:
    return static_cast<byte_t>(Joypad::JoypadButton::SELECT);
  default:
    return 0;
  }
}
} // namespace

void SDL3Frontend::update_input_buttons(const byte_t mask, const bool pressed) {
  if (mask == 0) {
    return;
  }

  byte_t current = input_state.buttons.load(std::memory_order_relaxed);
  if (pressed) {
    current |= mask;
  } else {
    current &= static_cast<byte_t>(~mask);
  }
  input_state.buttons.store(current, std::memory_order_relaxed);
}

void SDL3Frontend::handle_controller_press(const SDL_GamepadButton button, const bool pressed) {
  // Controllers never drive frontend widgets directly. They only update the
  // emulated joypad state seen by the core.
  update_input_buttons(controller_mask_for_button(button), pressed);
}

void SDL3Frontend::handle_keypress(const SDL_Keycode key, const bool pressed) {
  update_input_buttons(button_mask_for_key(key), pressed);

  const auto &binds = gui.get_settings_c().general_keybinds;
  auto &settings = gui.get_settings();
  const auto matches = [key, &binds](const GeneralKeybindIndex index) {
    return key == binds[index];
  };
  const auto adjust_volume = [this, &settings](const float delta) {
    settings.volume = std::clamp(settings.volume + delta, 0.0f, 1.5f);
    host.set_volume(settings.volume);
  };

  if (pressed && matches(GK_FF_TOGGLE)) {
    ui_state.fast_forward = !ui_state.fast_forward;
  }

  // Hold-to-fast-forward intentionally overrides the toggle state while the
  // key is down, then releases back to normal speed on key-up.
  if (matches(GK_FF_HOLD)) {
    ui_state.fast_forward = pressed;
  }

  if (pressed && matches(GK_VOL_UP)) {
    adjust_volume(0.05f);
  }
  if (pressed && matches(GK_VOL_DOWN)) {
    adjust_volume(-0.05f);
  }
  if (pressed && matches(GK_MONOCHROME)) {
    settings.force_mono_dmg = !settings.force_mono_dmg;
  }
  if (pressed && matches(GK_QUICKSAVE)) {
    quicksave_requested.store(true, std::memory_order_release);
  }
  if (pressed && matches(GK_QUICKLOAD)) {
    quickload_requested.store(true, std::memory_order_release);
  }
}

byte_t SDL3Frontend::button_mask_for_key(const SDL_Keycode key) const {
  for (std::size_t i = 0; i < gui.get_settings_c().keybinds.size(); ++i) {
    if (gui.get_settings_c().keybinds[i] == key) {
      return static_cast<byte_t>(button_order[i]);
    }
  }
  return 0;
}
