#include "frontend/sdl3/frontend.hpp"
#include <fstream>

namespace {
std::string strip_colons(const std::string &path) {
  if (const auto i = path.find(" ::"); i != std::string::npos) {
    return path.substr(0, i);
  }
  return path;
}
} // namespace

bool SDL3Frontend::consume_load_save_dialog_result(std::string &save_path,
                                                   bool &accepted) {
  std::lock_guard lock(ui_mutex);
  if (!ui_state.load_save_dialog_result_ready)
    return false;
  accepted = ui_state.load_save_dialog_accepted;
  save_path = ui_state.load_save_dialog_path;
  ui_state.load_save_dialog_result_ready = false;
  ui_state.load_save_dialog_accepted = false;
  ui_state.load_save_dialog_path.clear();
  return true;
}

std::filesystem::path
SDL3Frontend::suggest_save_path(const cart &c,
                                const std::string &display_label) {
  const auto name_from_label = [&]() -> std::string {
    if (const auto p = display_label.find(" :: ");
        p != std::string::npos && p + 4 < display_label.size()) {
      return display_label.substr(p + 4);
    }
    if (!c.file_path.empty())
      return c.file_path.filename().string();
    return std::filesystem::path(strip_colons(display_label))
        .filename()
        .string();
  };

  std::string stem = std::filesystem::path(name_from_label()).stem().string();
  if (stem.empty())
    stem = c.header.title().empty() ? "cartridge" : c.header.title();
  if (stem.empty())
    stem = "cartridge";

  std::filesystem::path dir = c.file_path.parent_path();
  if (dir.empty())
    dir = std::filesystem::current_path();
  return dir / (stem + ".sav");
}

void SDL3Frontend::remember_save_path_for_active_rom(
    const std::filesystem::path &save_path) {
  if (active_rom_hash.empty() || save_path.empty())
    return;

  const std::string normalized = save_path.lexically_normal().string();
  if (normalized.empty())
    return;

  auto &settings = gui.get_settings();
  if (const auto it = settings.save_path_by_rom_hash.find(active_rom_hash);
      it != settings.save_path_by_rom_hash.end() && it->second == normalized) {
    return;
  }
  settings.save_path_by_rom_hash[active_rom_hash] = normalized;
  settings.save();
}

void SDL3Frontend::setup_save_context(const cart &c,
                                      const std::string &display_label,
                                      const std::string &rom_hash) {
  active_rom_hash = rom_hash;
  const std::filesystem::path default_suggested =
      suggest_save_path(c, display_label);
  suggested_save_path_ = default_suggested;
  active_save_path.reset();

  std::optional<std::filesystem::path> mapped_path = std::nullopt;
  if (!active_rom_hash.empty()) {
    if (const auto it =
            gui.get_settings_c().save_path_by_rom_hash.find(active_rom_hash);
        it != gui.get_settings_c().save_path_by_rom_hash.end() &&
        !it->second.empty()) {
      mapped_path = std::filesystem::path(it->second);
      suggested_save_path_ = *mapped_path;
    }
  }

  if (mapped_path.has_value()) {
    std::error_code ec;
    if (std::filesystem::exists(*mapped_path, ec))
      active_save_path = *mapped_path;
  }
  if (!active_save_path.has_value() && !default_suggested.empty()) {
    std::error_code ec;
    if (std::filesystem::exists(default_suggested, ec)) {
      active_save_path = default_suggested;
      suggested_save_path_ = default_suggested;
    }
  }

  if (active_save_path.has_value()) {
    remember_save_path_for_active_rom(*active_save_path);
  }

  {
    std::lock_guard lk(save_mutex);
    latest_save_snapshot.clear();
    save_snapshot_ready = false;
  }
  deferred_save_data.clear();
  deferred_save_pending = false;
  load_save_dialog_inflight = false;

  std::lock_guard lock(ui_mutex);
  ui_state.request_open_load_save_dialog = false;
  ui_state.load_save_dialog_result_ready = false;
  ui_state.load_save_dialog_accepted = false;
  ui_state.load_save_dialog_path.clear();
  ui_state.load_save_dialog_default_name.clear();
  ui_state.load_save_dialog_start_dir.clear();
}

void SDL3Frontend::enqueue_save_snapshot(std::vector<byte_t> snapshot) {
  std::lock_guard lk(save_mutex);
  latest_save_snapshot = std::move(snapshot);
  save_snapshot_ready = true;
}

void SDL3Frontend::process_pending_save() {
  std::vector<byte_t> snapshot;
  {
    std::lock_guard lk(save_mutex);
    if (save_snapshot_ready) {
      snapshot = std::move(latest_save_snapshot);
      latest_save_snapshot.clear();
      save_snapshot_ready = false;
    }
  }
  if (!snapshot.empty()) {
    deferred_save_data = std::move(snapshot);
    deferred_save_pending = true;
  }

  if (load_save_dialog_inflight) {
    std::string dialog_path;
    bool accepted = false;
    if (consume_load_save_dialog_result(dialog_path, accepted)) {
      load_save_dialog_inflight = false;
      if (accepted && !dialog_path.empty()) {
        active_save_path = std::filesystem::path(dialog_path);
        suggested_save_path_ = *active_save_path;
        remember_save_path_for_active_rom(*active_save_path);
      } else {
        Logger::push(LogLevel::Info, "Save", "Save cancelled",
                     "Battery save data changed but no save file was selected.");
      }
    }
  }

  if (!deferred_save_pending)
    return;

  const auto write_snapshot = [](const std::filesystem::path &path,
                                 const std::vector<byte_t> &data) -> bool {
    if (path.empty() || data.empty())
      return false;
    const auto parent = path.parent_path();
    if (!parent.empty()) {
      std::error_code ec;
      std::filesystem::create_directories(parent, ec);
    }
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f)
      return false;
    f.write(reinterpret_cast<const char *>(data.data()),
            static_cast<std::streamsize>(data.size()));
    return static_cast<bool>(f);
  };

  if (active_save_path.has_value()) {
    if (write_snapshot(*active_save_path, deferred_save_data)) {
      deferred_save_pending = false;
      deferred_save_data.clear();
    } else {
      Logger::push(LogLevel::Warning, "Save", "Failed to write save file",
                   "Could not write battery save data to: " +
                       active_save_path->string());
    }
    return;
  }

  if (load_save_dialog_inflight)
    return;

  std::string start_dir = suggested_save_path_.parent_path().string();
  if (start_dir.empty())
    start_dir = gui.get_settings_c().rom_dir;
  std::string default_name = suggested_save_path_.filename().string();
  if (default_name.empty())
    default_name = "cartridge.sav";

  {
    std::lock_guard lock(ui_mutex);
    ui_state.load_save_dialog_start_dir = start_dir;
    ui_state.load_save_dialog_default_name = default_name;
    ui_state.load_save_dialog_path.clear();
    ui_state.load_save_dialog_result_ready = false;
    ui_state.load_save_dialog_accepted = false;
    ui_state.request_open_load_save_dialog = true;
  }
  load_save_dialog_inflight = true;
}

