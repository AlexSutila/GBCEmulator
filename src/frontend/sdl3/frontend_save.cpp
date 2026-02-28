#include "frontend/sdl3/frontend.hpp"
#include <algorithm>
#include <cctype>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace {
std::string strip_colons(const std::string &path) {
  if (const auto i = path.find(" ::"); i != std::string::npos) {
    return path.substr(0, i);
  }
  return path;
}

std::string normalize_hex_lower(std::string text) {
  for (char &c : text) {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  return text;
}

constexpr int kSavestateThumbWidth = 80;
constexpr int kSavestateThumbHeight = 72;
constexpr int kMaxQuickSavestates = 10;
constexpr std::size_t kSavestateGameDirNameMaxChars = 25;
constexpr std::size_t kSavestateChecksumShortChars = 12;

struct SavestateMeta {
  int version{1};
  std::string kind{"manual"};
  std::string label{};
  std::int64_t created_unix{0};
  int thumb_w{kSavestateThumbWidth};
  int thumb_h{kSavestateThumbHeight};
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(SavestateMeta, version, kind,
                                                label, created_unix, thumb_w,
                                                thumb_h)

std::filesystem::path
savestate_meta_path(const std::filesystem::path &state_path) {
  return std::filesystem::path(state_path.string() + ".json");
}

std::filesystem::path
savestate_thumb_path(const std::filesystem::path &state_path) {
  return std::filesystem::path(state_path.string() + ".thumb");
}

bool write_blob_file(const std::filesystem::path &path,
                     const std::vector<byte_t> &data) {
  if (path.empty() || data.empty())
    return false;
  if (const auto parent = path.parent_path(); !parent.empty()) {
    std::error_code ec;
    std::filesystem::create_directories(parent, ec);
  }
  std::ofstream f(path, std::ios::binary | std::ios::trunc);
  if (!f)
    return false;
  f.write(reinterpret_cast<const char *>(data.data()),
          static_cast<std::streamsize>(data.size()));
  return static_cast<bool>(f);
}

bool write_savestate_meta(const std::filesystem::path &path,
                          const SavestateMeta &meta) {
  std::ofstream f(path, std::ios::trunc);
  if (!f)
    return false;
  const nlohmann::json j = meta;
  f << j.dump(2);
  return static_cast<bool>(f);
}

std::optional<SavestateMeta>
read_savestate_meta(const std::filesystem::path &path) {
  std::ifstream f(path);
  if (!f)
    return std::nullopt;
  try {
    nlohmann::json j;
    f >> j;
    return j.get<SavestateMeta>();
  } catch (...) {
    return std::nullopt;
  }
}

std::time_t file_time_to_time_t(const std::filesystem::file_time_type &ft) {
  const auto sctp =
      std::chrono::time_point_cast<std::chrono::system_clock::duration>(
          ft - std::filesystem::file_time_type::clock::now() +
          std::chrono::system_clock::now());
  return std::chrono::system_clock::to_time_t(sctp);
}

std::string sanitize_savestate_label(std::string s,
                                     const std::size_t max_len = 32) {
  for (char &c : s) {
    const bool keep = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                      (c >= '0' && c <= '9') || c == '-' || c == '_';
    c = keep ? c : '_';
  }
  s.erase(std::remove(s.begin(), s.end(), '\0'), s.end());
  while (!s.empty() && s.front() == '_')
    s.erase(s.begin());
  if (s.size() > max_len)
    s.resize(max_len);
  while (!s.empty() && s.back() == '_')
    s.pop_back();
  return s;
}

std::string shorten_checksum(std::string checksum_hex,
                             const std::size_t max_len) {
  checksum_hex = normalize_hex_lower(std::move(checksum_hex));
  if (checksum_hex.size() > max_len)
    checksum_hex.resize(max_len);
  return checksum_hex;
}

std::filesystem::path resolve_save_root_dir(std::string dir_text) {
  if (dir_text.empty())
    dir_text = "./saves";

  std::filesystem::path root = dir_text;
  if (root.empty())
    root = "./saves";

  if (root.is_relative()) {
    std::error_code ec;
    if (const auto cwd = std::filesystem::current_path(ec); !ec) {
      root = cwd / root;
    }
  }
  return root.lexically_normal();
}

std::filesystem::path resolve_savestate_root_dir(std::string dir_text) {
  if (dir_text.empty())
    dir_text = "./savestates";

  std::filesystem::path root = std::move(dir_text);
  if (root.empty())
    root = "./savestates";

  if (root.is_relative()) {
    std::error_code ec;
    if (const auto cwd = std::filesystem::current_path(ec); !ec) {
      root = cwd / root;
    }
  }
  return root.lexically_normal();
}

std::string savestate_timestamp_slug(const std::time_t t) {
  std::tm tm{};
#if defined(_WIN32)
  localtime_s(&tm, &t);
#else
  localtime_r(&t, &tm);
#endif
  std::ostringstream oss;
  oss << std::put_time(&tm, "%Y%m%d_%H%M%S");
  return oss.str();
}

std::string format_timestamp_local(const std::time_t t) {
  if (t <= 0)
    return "Unknown";
  std::tm tm{};
#if defined(_WIN32)
  localtime_s(&tm, &t);
#else
  localtime_r(&t, &tm);
#endif
  std::ostringstream oss;
  oss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
  return oss.str();
}

bool write_thumb_raw_argb(const std::filesystem::path &path,
                          const std::vector<std::uint32_t> &pixels) {
  if (pixels.empty())
    return false;
  std::ofstream f(path, std::ios::binary | std::ios::trunc);
  if (!f)
    return false;
  f.write(reinterpret_cast<const char *>(pixels.data()),
          static_cast<std::streamsize>(pixels.size() * sizeof(std::uint32_t)));
  return static_cast<bool>(f);
}

std::optional<std::vector<std::uint32_t>>
read_thumb_raw_argb(const std::filesystem::path &path, const int w,
                    const int h) {
  if (w <= 0 || h <= 0)
    return std::nullopt;
  const std::size_t pixel_count =
      static_cast<std::size_t>(w) * static_cast<std::size_t>(h);
  const std::size_t byte_count = pixel_count * sizeof(std::uint32_t);
  std::ifstream f(path, std::ios::binary | std::ios::ate);
  if (!f)
    return std::nullopt;
  const auto size = static_cast<std::size_t>(f.tellg());
  if (size != byte_count)
    return std::nullopt;
  std::vector<std::uint32_t> out(pixel_count);
  f.seekg(0, std::ios::beg);
  if (!f.read(reinterpret_cast<char *>(out.data()),
              static_cast<std::streamsize>(byte_count)))
    return std::nullopt;
  return out;
}

bool is_savestate_file(const std::filesystem::directory_entry &de) {
  return de.is_regular_file() && de.path().extension() == ".state";
}
} // namespace

void SDL3Frontend::setup_save_context(const cart &c,
                                      const std::string &display_label,
                                      const std::string &rom_hash) {
  active_rom_hash = rom_hash;
  setup_savestate_context(c, display_label, rom_hash);

  std::string stem = c.file_path.stem().string();
  if (stem.empty())
    stem = std::filesystem::path(strip_colons(display_label)).stem().string();
  if (stem.empty())
    stem = c.header.title();
  if (stem.empty())
    stem = "cartridge";
  stem = sanitize_savestate_label(stem, kSavestateGameDirNameMaxChars);
  if (stem.empty())
    stem = "cartridge";

  const std::string checksum_short = shorten_checksum(
      normalize_hex_lower(rom_hash), kSavestateChecksumShortChars);
  const std::filesystem::path save_root =
      resolve_save_root_dir(gui.get_settings_c().save_root_dir);
  std::error_code ec;
  std::filesystem::create_directories(save_root, ec);
  if (checksum_short.empty()) {
    active_save_path = save_root / (stem + ".sav");
  } else {
    active_save_path = save_root / (stem + " - " + checksum_short + ".sav");
  }

  {
    std::lock_guard lk(save_mutex);
    latest_save_snapshot.clear();
    save_snapshot_ready = false;
  }
  deferred_save_data.clear();
  deferred_save_pending = false;
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

  if (!active_save_path.has_value()) {
    Logger::push(LogLevel::Warning, "Save", "Missing save path",
                 "Could not resolve the save destination.");
    deferred_save_pending = false;
    deferred_save_data.clear();
    return;
  }

  if (write_snapshot(*active_save_path, deferred_save_data)) {
    deferred_save_pending = false;
    deferred_save_data.clear();
  } else {
    Logger::push(LogLevel::Warning, "Save", "Failed to write save file",
                 "Could not write save data to: " + active_save_path->string() +
                     ".");
  }
}

void SDL3Frontend::release_savestate_textures_locked() {
  for (auto &entry : savestate_entries_) {
    if (entry.thumb_texture) {
      SDL_DestroyTexture(entry.thumb_texture);
      entry.thumb_texture = nullptr;
    }
    entry.thumb_texture_attempted = false;
  }
}

void SDL3Frontend::reset_savestate_context() {
  release_savestate_textures_locked();
  savestate_entries_.clear();
  savestate_selected_path_.reset();
  savestate_dir_.clear();
  savestate_manual_label_input_.fill('\0');
  {
    std::lock_guard lock(savestate_request_mutex);
    pending_savestate_load_path_.reset();
    pending_manual_savestate_label_.clear();
  }
  manual_savestate_requested.store(false, std::memory_order_relaxed);
  savestate_list_dirty.store(true, std::memory_order_relaxed);
  next_savestate_scan_ = Clock::now();
}

void SDL3Frontend::setup_savestate_context(const cart &c,
                                           const std::string &display_label,
                                           const std::string &rom_hash) {
  release_savestate_textures_locked();
  savestate_entries_.clear();
  savestate_selected_path_.reset();

  std::string stem = c.file_path.stem().string();
  if (stem.empty())
    stem = std::filesystem::path(strip_colons(display_label)).stem().string();
  if (stem.empty())
    stem = c.header.title();
  if (stem.empty())
    stem = "cartridge";
  stem = sanitize_savestate_label(stem, kSavestateGameDirNameMaxChars);
  if (stem.empty())
    stem = "cartridge";

  const std::string checksum_full = normalize_hex_lower(rom_hash);
  const std::string checksum_short =
      shorten_checksum(checksum_full, kSavestateChecksumShortChars);
  const std::filesystem::path root_dir =
      resolve_savestate_root_dir(gui.get_settings_c().savestate_root_dir);
  std::error_code ec;
  std::filesystem::create_directories(root_dir, ec);
  if (!checksum_short.empty())
    savestate_dir_ = root_dir / (stem + " - " + checksum_short);
  else
    savestate_dir_ = root_dir / stem;

  savestate_manual_label_input_.fill('\0');
  {
    std::lock_guard lock(savestate_request_mutex);
    pending_savestate_load_path_.reset();
    pending_manual_savestate_label_.clear();
  }
  manual_savestate_requested.store(false, std::memory_order_relaxed);
  savestate_list_dirty.store(true, std::memory_order_relaxed);
  next_savestate_scan_ = Clock::now();
}

void SDL3Frontend::queue_savestate_load_request(
    const std::filesystem::path &path) {
  if (path.empty())
    return;
  {
    std::lock_guard lock(savestate_request_mutex);
    pending_savestate_load_path_ = path;
  }
}

std::optional<std::filesystem::path>
SDL3Frontend::consume_savestate_load_request() {
  std::lock_guard lock(savestate_request_mutex);
  if (!pending_savestate_load_path_.has_value())
    return std::nullopt;
  auto out = pending_savestate_load_path_;
  pending_savestate_load_path_.reset();
  return out;
}

void SDL3Frontend::queue_manual_savestate_request(std::string label) {
  {
    std::lock_guard lock(savestate_request_mutex);
    pending_manual_savestate_label_ = std::move(label);
  }
  manual_savestate_requested.store(true, std::memory_order_release);
}

std::optional<std::string> SDL3Frontend::consume_manual_savestate_request() {
  if (!manual_savestate_requested.exchange(false, std::memory_order_acq_rel))
    return std::nullopt;
  std::lock_guard lock(savestate_request_mutex);
  auto label = pending_manual_savestate_label_;
  pending_manual_savestate_label_.clear();
  return label;
}

std::vector<std::uint32_t> SDL3Frontend::capture_savestate_thumbnail() const {
  constexpr int src_w = framebuf_width;
  constexpr int src_h = framebuf_height;
  std::vector<std::uint32_t> out(
      static_cast<std::size_t>(kSavestateThumbWidth * kSavestateThumbHeight));
  const std::uint32_t *src = get_front_buffer();
  const bool cgb_mode = is_cgb.load(std::memory_order_relaxed);
  for (int y = 0; y < kSavestateThumbHeight; ++y) {
    const int sy = (y * src_h) / kSavestateThumbHeight;
    for (int x = 0; x < kSavestateThumbWidth; ++x) {
      const int sx = (x * src_w) / kSavestateThumbWidth;
      out[static_cast<std::size_t>(y) * kSavestateThumbWidth + x] =
          SDLHost::format_pixel_data(src[sy * src_w + sx], cgb_mode, false);
    }
  }
  return out;
}

std::optional<std::filesystem::path>
SDL3Frontend::write_savestate_bundle(const std::vector<byte_t> &blob,
                                     const bool quick,
                                     const std::string &label) {
  if (blob.empty() || savestate_dir_.empty())
    return std::nullopt;

  std::error_code ec;
  std::filesystem::create_directories(savestate_dir_, ec);

  const std::time_t now = std::time(nullptr);
  const std::string prefix = quick ? "quick" : "manual";
  const std::string ts = savestate_timestamp_slug(now);
  const std::string sanitized_label = sanitize_savestate_label(label);

  std::filesystem::path state_path;
  for (int attempt = 0; attempt < 1000; ++attempt) {
    std::string stem = prefix + "_" + ts;
    if (!sanitized_label.empty())
      stem += "_" + sanitized_label;
    if (attempt > 0)
      stem += "_" + std::to_string(attempt);
    const auto candidate = savestate_dir_ / (stem + ".state");
    if (!std::filesystem::exists(candidate, ec)) {
      state_path = candidate;
      break;
    }
  }
  if (state_path.empty())
    return std::nullopt;

  if (!write_blob_file(state_path, blob))
    return std::nullopt;

  const auto thumb_pixels = capture_savestate_thumbnail();
  if (!thumb_pixels.empty()) {
    write_thumb_raw_argb(savestate_thumb_path(state_path), thumb_pixels);
  }

  SavestateMeta meta{};
  meta.kind = quick ? "quick" : "manual";
  meta.label = label;
  meta.created_unix = static_cast<std::int64_t>(now);
  meta.thumb_w = kSavestateThumbWidth;
  meta.thumb_h = kSavestateThumbHeight;
  write_savestate_meta(savestate_meta_path(state_path), meta);

  if (quick) {
    struct QuickCandidate {
      std::filesystem::path state_path;
      std::time_t created_at{};
    };
    std::vector<QuickCandidate> quick_entries;
    for (const auto &de :
         std::filesystem::directory_iterator(savestate_dir_, ec)) {
      if (!is_savestate_file(de))
        continue;
      const auto p = de.path();
      auto meta_opt = read_savestate_meta(savestate_meta_path(p));
      const bool is_quick = meta_opt.has_value()
                                ? (meta_opt->kind == "quick")
                                : (p.stem().string().rfind("quick_", 0) == 0);
      if (!is_quick)
        continue;
      std::time_t created_at = 0;
      if (meta_opt.has_value() && meta_opt->created_unix > 0)
        created_at = static_cast<std::time_t>(meta_opt->created_unix);
      else
        created_at = file_time_to_time_t(de.last_write_time(ec));
      quick_entries.push_back({p, created_at});
    }
    std::sort(quick_entries.begin(), quick_entries.end(),
              [](const QuickCandidate &a, const QuickCandidate &b) {
                if (a.created_at != b.created_at)
                  return a.created_at > b.created_at;
                return a.state_path.filename().string() >
                       b.state_path.filename().string();
              });
    for (std::size_t i = kMaxQuickSavestates; i < quick_entries.size(); ++i) {
      std::filesystem::remove(quick_entries[i].state_path, ec);
      std::filesystem::remove(savestate_meta_path(quick_entries[i].state_path),
                              ec);
      std::filesystem::remove(savestate_thumb_path(quick_entries[i].state_path),
                              ec);
    }
  }

  savestate_list_dirty.store(true, std::memory_order_release);
  return state_path;
}

std::optional<std::filesystem::path>
SDL3Frontend::latest_savestate_path() const {
  if (savestate_dir_.empty())
    return std::nullopt;
  std::error_code ec;
  if (!std::filesystem::exists(savestate_dir_, ec))
    return std::nullopt;

  std::optional<std::filesystem::path> best_path = std::nullopt;
  std::time_t best_time = 0;
  for (const auto &de :
       std::filesystem::directory_iterator(savestate_dir_, ec)) {
    if (!is_savestate_file(de))
      continue;
    std::time_t created_at = 0;
    if (const auto meta = read_savestate_meta(savestate_meta_path(de.path()));
        meta.has_value() && meta->created_unix > 0) {
      created_at = static_cast<std::time_t>(meta->created_unix);
    } else {
      created_at = file_time_to_time_t(de.last_write_time(ec));
    }
    if (!best_path.has_value() || created_at > best_time) {
      best_time = created_at;
      best_path = de.path();
    }
  }
  return best_path;
}

void SDL3Frontend::refresh_savestate_entries_locked(const bool force_refresh) {
  const auto now = Clock::now();
  if (!force_refresh && !savestate_list_dirty.load(std::memory_order_acquire) &&
      now < next_savestate_scan_) {
    return;
  }
  savestate_list_dirty.store(false, std::memory_order_release);
  next_savestate_scan_ = now + std::chrono::seconds(1);

  const auto prev_selected = savestate_selected_path_;
  release_savestate_textures_locked();
  savestate_entries_.clear();

  if (savestate_dir_.empty())
    return;

  std::error_code ec;
  if (!std::filesystem::exists(savestate_dir_, ec))
    return;

  for (const auto &de :
       std::filesystem::directory_iterator(savestate_dir_, ec)) {
    if (!is_savestate_file(de))
      continue;

    SavestateEntry entry{};
    entry.state_path = de.path();
    entry.thumb_path = savestate_thumb_path(entry.state_path);
    entry.file_size = de.file_size(ec);

    std::time_t fallback_time = file_time_to_time_t(de.last_write_time(ec));
    if (const auto meta =
            read_savestate_meta(savestate_meta_path(entry.state_path));
        meta.has_value()) {
      entry.kind = (meta->kind == "quick") ? "Quick" : "Manual";
      entry.label =
          meta->label.empty() ? entry.state_path.stem().string() : meta->label;
      entry.created_at = meta->created_unix > 0
                             ? static_cast<std::time_t>(meta->created_unix)
                             : fallback_time;
      entry.thumb_w = meta->thumb_w;
      entry.thumb_h = meta->thumb_h;
    } else {
      const auto stem = entry.state_path.stem().string();
      entry.kind = (stem.rfind("quick_", 0) == 0) ? "Quick" : "Manual";
      entry.label = stem;
      entry.created_at = fallback_time;
      entry.thumb_w = kSavestateThumbWidth;
      entry.thumb_h = kSavestateThumbHeight;
    }

    if (entry.thumb_w <= 0)
      entry.thumb_w = kSavestateThumbWidth;
    if (entry.thumb_h <= 0)
      entry.thumb_h = kSavestateThumbHeight;

    savestate_entries_.push_back(std::move(entry));
  }

  std::sort(savestate_entries_.begin(), savestate_entries_.end(),
            [](const SavestateEntry &a, const SavestateEntry &b) {
              if (a.created_at != b.created_at)
                return a.created_at > b.created_at;
              return a.state_path.filename().string() >
                     b.state_path.filename().string();
            });

  if (prev_selected.has_value()) {
    const auto it =
        std::ranges::find_if(savestate_entries_, [&](const SavestateEntry &e) {
          return e.state_path == *prev_selected;
        });
    if (it != savestate_entries_.end()) {
      savestate_selected_path_ = it->state_path;
      return;
    }
  }
  if (!savestate_entries_.empty())
    savestate_selected_path_ = savestate_entries_.front().state_path;
  else
    savestate_selected_path_.reset();
}

// Temporary place, should be moved to gui code
void SDL3Frontend::build_savestate_manager_window_locked() {
  if (!ui_state.show_savestate_manager)
    return;

  refresh_savestate_entries_locked(false);
  ImGui::SetNextWindowSize(ImVec2(920, 560), ImGuiCond_FirstUseEver);
  if (!ImGui::Begin("Savestate Manager", &ui_state.show_savestate_manager)) {
    ImGui::End();
    return;
  }

  const bool emulator_ready = static_cast<bool>(gbc);

  if (savestate_dir_.empty()) {
    ImGui::TextDisabled("Load a ROM to manage savestates.");
    ImGui::End();
    return;
  }

  ImGui::Text("Directory: %s", savestate_dir_.string().c_str());
  ImGui::InputText("Label", savestate_manual_label_input_.data(),
                   savestate_manual_label_input_.size());

  if (!emulator_ready)
    ImGui::BeginDisabled();
  if (ImGui::Button("Save")) {
    queue_manual_savestate_request(savestate_manual_label_input_.data());
  }
  ImGui::SameLine();
  if (ImGui::Button("Load Most Recent")) {
    quickload_requested.store(true, std::memory_order_release);
  }
  if (!emulator_ready)
    ImGui::EndDisabled();

  ImGui::SameLine();
  if (ImGui::Button("Refresh")) {
    savestate_list_dirty.store(true, std::memory_order_release);
    refresh_savestate_entries_locked(true);
  }

  ImGui::Separator();

  std::optional<std::filesystem::path> delete_path = std::nullopt;
  std::optional<std::filesystem::path> load_path = std::nullopt;

  if (ImGui::BeginTable("savestate_manager_layout", 2,
                        ImGuiTableFlags_Resizable |
                            ImGuiTableFlags_BordersInnerV)) {
    ImGui::TableSetupColumn("List", ImGuiTableColumnFlags_WidthStretch, 0.62f);
    ImGui::TableSetupColumn("Preview", ImGuiTableColumnFlags_WidthStretch,
                            0.38f);
    ImGui::TableNextRow();

    ImGui::TableSetColumnIndex(0);
    if (ImGui::BeginTable("savestate_table", 4,
                          ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders |
                              ImGuiTableFlags_SizingStretchProp |
                              ImGuiTableFlags_ScrollY,
                          ImVec2(0.0f, 0.0f))) {
      ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 70.0f);
      ImGui::TableSetupColumn("Taken", ImGuiTableColumnFlags_WidthFixed,
                              170.0f);
      ImGui::TableSetupColumn("Label");
      ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthFixed, 80.0f);
      ImGui::TableHeadersRow();

      for (std::size_t i = 0; i < savestate_entries_.size(); ++i) {
        auto &entry = savestate_entries_[i];
        const bool selected = savestate_selected_path_.has_value() &&
                              *savestate_selected_path_ == entry.state_path;

        ImGui::PushID(static_cast<int>(i));
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        if (ImGui::Selectable(entry.kind.c_str(), selected,
                              ImGuiSelectableFlags_SpanAllColumns)) {
          savestate_selected_path_ = entry.state_path;
        }
        ImGui::TableSetColumnIndex(1);
        ImGui::TextUnformatted(
            format_timestamp_local(entry.created_at).c_str());
        ImGui::TableSetColumnIndex(2);
        ImGui::TextUnformatted(entry.label.c_str());
        if (ImGui::IsItemHovered())
          ImGui::SetTooltip("%s", entry.state_path.filename().string().c_str());
        ImGui::TableSetColumnIndex(3);
        ImGui::Text("%zu KB", static_cast<std::size_t>(
                                  (entry.file_size + 1023u) / 1024u));
        ImGui::PopID();
      }
      ImGui::EndTable();
    }

    ImGui::TableSetColumnIndex(1);
    auto selected_it = savestate_entries_.end();
    if (savestate_selected_path_.has_value()) {
      selected_it = std::ranges::find_if(
          savestate_entries_, [&](const SavestateEntry &e) {
            return e.state_path == *savestate_selected_path_;
          });
    }

    if (selected_it == savestate_entries_.end()) {
      ImGui::TextDisabled("No savestate selected.");
    } else {
      auto &entry = *selected_it;
      ImGui::Text("Type: %s", entry.kind.c_str());
      ImGui::Text("Taken: %s",
                  format_timestamp_local(entry.created_at).c_str());
      ImGui::Text("File: %s", entry.state_path.filename().string().c_str());
      ImGui::Text("Size: %zu bytes", static_cast<std::size_t>(entry.file_size));
      ImGui::Spacing();

      if (!emulator_ready)
        ImGui::BeginDisabled();
      if (ImGui::Button("Load")) {
        load_path = entry.state_path;
      }
      if (!emulator_ready)
        ImGui::EndDisabled();
      ImGui::SameLine();
      if (ImGui::Button("Delete")) {
        delete_path = entry.state_path;
      }

      ImGui::SeparatorText("Thumbnail");
      if (!entry.thumb_texture && !entry.thumb_texture_attempted) {
        entry.thumb_texture_attempted = true;
        if (std::filesystem::exists(entry.thumb_path)) {
          if (const auto pixels = read_thumb_raw_argb(
                  entry.thumb_path, entry.thumb_w, entry.thumb_h);
              pixels.has_value()) {
            entry.thumb_texture = SDL_CreateTexture(
                host.get_renderer(), SDL_PIXELFORMAT_ARGB8888,
                SDL_TEXTUREACCESS_STATIC, entry.thumb_w, entry.thumb_h);
            if (entry.thumb_texture) {
              SDL_UpdateTexture(entry.thumb_texture, nullptr, pixels->data(),
                                entry.thumb_w *
                                    static_cast<int>(sizeof(std::uint32_t)));
              SDL_SetTextureScaleMode(entry.thumb_texture,
                                      SDL_SCALEMODE_NEAREST);
            }
          }
        }
      }
      if (entry.thumb_texture) {
        const float max_w = 260.0f;
        const float scale =
            std::min(max_w / static_cast<float>(entry.thumb_w), 4.0f);
        ImGui::Image(entry.thumb_texture,
                     ImVec2(entry.thumb_w * scale, entry.thumb_h * scale));
      } else {
        ImGui::TextDisabled("No thumbnail available.");
      }
    }

    ImGui::EndTable();
  }

  if (load_path.has_value()) {
    queue_savestate_load_request(*load_path);
  }
  if (delete_path.has_value()) {
    std::error_code ec;
    std::filesystem::remove(*delete_path, ec);
    std::filesystem::remove(savestate_meta_path(*delete_path), ec);
    std::filesystem::remove(savestate_thumb_path(*delete_path), ec);
    savestate_list_dirty.store(true, std::memory_order_release);
    refresh_savestate_entries_locked(true);
  }

  ImGui::End();
}
