#ifndef GBC_SDL3_FRONTEND_HPP
#define GBC_SDL3_FRONTEND_HPP

#include "common.hpp"
#include "debugger.hpp"
#include "frontend/frontend.hpp"
#include "gui.hpp"
#include "sdl_host.hpp"
#include <atomic>
#include <thread>

using Clock = std::chrono::steady_clock;

class SDL3Frontend final : public Frontend {
  static constexpr int framebuf_height{144};
  static constexpr int framebuf_width{160};
  static constexpr int framebuf_size{framebuf_width * framebuf_height};
  static constexpr int scale{4};
  static constexpr std::uint32_t black{0x03000000}; // Alpha bits are index
  static constexpr std::array<Joypad::JoypadButton, 8> button_order{
    Joypad::JoypadButton::RIGHT, Joypad::JoypadButton::LEFT,
    Joypad::JoypadButton::UP, Joypad::JoypadButton::DOWN,
    Joypad::JoypadButton::A, Joypad::JoypadButton::B,
    Joypad::JoypadButton::SELECT, Joypad::JoypadButton::START
  };
  static constexpr int max_catchup_cycles{
    70'224 / 4
  }; // 1/4 second worth of cycles at 4.19MHz
  static constexpr int target_queue_ms{20};

public:
  SDL3Frontend();
  ~SDL3Frontend() override;

  // Frontend Interface Overrides
  void start() override;
  std::array<std::uint32_t, 160 * 144> get_frame() override;
  void put_pixel(int x, int y, std::uint32_t c) override;
  void queue_audio_samples(const float* samples, size_t count) override;
  void clear(std::uint32_t c) override;

private:
  // Subsystems
  SDLHost host;
  GbcImGui gui;
  DebuggerImGui debugger;
  UiState ui_state;
  mutable std::mutex ui_mutex;

  // Emulation state
  std::jthread emulation_thread;
  std::atomic<bool> running{true};
  std::atomic<bool> is_cgb{false};
  std::atomic<bool> fast_forward{true};

  // FPS calculation
  std::atomic<uint64_t> emulated_frame_count{0};
  Clock::time_point last_fps_check = Clock::now();
  uint64_t last_frame_count = 0;

  // Video buffers
  std::array<std::unique_ptr<std::uint32_t[]>, 2> framebuffers;
  std::atomic<int> front_index{0};
  const std::uint32_t* get_front_buffer() const;

  // ROM loading
  bool consume_load_rom_request(std::string& rom_path);
  bool consume_load_bios_request(std::optional<std::string>& bios_path);
  bool consume_load_save_dialog_result(std::string& save_path, bool& accepted);
  bool consume_save_dialog_result(std::string& save_path, bool& accepted);

  void start_rom_io_job(const std::string& source);
  bool consume_rom_io_result(std::string& rom_path_on_disk, std::string& display_label);
  void sync_io_status_to_ui();
  void setup_save_context(const cart& c, const std::string& display_label,
                          const std::string& rom_hash);
  void process_pending_save();
  void enqueue_save_snapshot(std::vector<byte_t> snapshot);
  static std::filesystem::path suggest_save_path(const cart& c,
                                                 const std::string& display_label);
  void remember_save_path_for_active_rom(const std::filesystem::path& save_path);

  int request_zip_choice_blocking(const std::string& zip_label,
                                  const std::vector<std::string>& entries,
                                  const std::stop_token& st);
  void poll_zip_choice_response();

  void handle_drop(const SDL_Event& e);

  std::jthread rom_io_thread;
  std::mutex rom_io_mutex;
  std::optional<std::string> rom_ready_path;
  std::string rom_ready_label;

  std::atomic<bool> io_busy{false};
  std::atomic<float> io_progress{-1.0f};
  std::mutex io_status_mutex;
  std::string io_status;

  std::mutex zip_choice_mutex;
  std::condition_variable zip_choice_cv;
  bool zip_choice_pending{false};
  int zip_choice_result{-1};
  bool zip_choice_cancelled{false};

  std::filesystem::path tmp_root;
  std::optional<std::filesystem::path> last_tmp_rom;
  std::optional<std::filesystem::path> last_tmp_zip;
  std::optional<cart> pending_cart_for_save_prompt;
  std::string pending_cart_label;
  std::string pending_cart_rom_hash;
  bool waiting_for_load_save_dialog{false};

  // Battery save handling
  std::mutex save_mutex;
  std::vector<byte_t> latest_save_snapshot;
  bool save_snapshot_ready{false};
  std::optional<std::filesystem::path> active_save_path;
  std::filesystem::path suggested_save_path_;
  std::vector<byte_t> deferred_save_data;
  bool deferred_save_pending{false};
  bool save_dialog_inflight{false};
  std::string active_rom_hash;

  // Resize/move redraw tuning
  std::atomic<std::int64_t> suppress_vsync_until_ns{0};
  std::atomic<std::int64_t> last_forced_redraw_ns{0};
  std::atomic_flag render_guard = ATOMIC_FLAG_INIT;

  // Avoid redundant texture uploads
  std::atomic<bool> video_dirty{true};
  std::atomic<bool> force_redraw{false};
  bool last_force_mono_dmg{false};
  bool last_cgb_mode{false};
  bool startup_window_size_adjusted{false};

  // Main loop helpers
  void process_events();
  void render_frame();
  void emulation_thread_fn(const std::stop_token& st, const cart& c,
                           const std::optional<std::string>& bios,
                           const std::optional<std::filesystem::path>& initial_save_path);
  void join_emu_thread_if_running();
  byte_t button_mask_for_key(SDL_Keycode key) const;

  // Input helpers
  InputState input_state{};
  void handle_controller_press(SDL_GamepadButton key, bool pressed);
  void handle_keypress(SDL_Keycode key, bool pressed);
  static bool SDLCALL event_watcher(void* userdata, const SDL_Event* event);
};

#endif // GBC_SDL3_FRONTEND_HPP
