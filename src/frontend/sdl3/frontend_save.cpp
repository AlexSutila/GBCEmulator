#include "frontend/sdl3/frontend.hpp"
#include <algorithm>
#include <atomic>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <unordered_set>
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
  std::erase(s, '\0');
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

  if (!active_save_path.has_value()) {
    Logger::push(LogLevel::Warning, "Save", "Missing save path",
                 "Could not resolve the save destination.");
    deferred_save_pending = false;
    deferred_save_data.clear();
    return;
  }

  if (write_blob_file(*active_save_path, deferred_save_data)) {
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
  clear_quick_savestate_cache();
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
  clear_quick_savestate_cache();

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
    manual_preempt_emu_loop.store(true, std::memory_order_release);
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
    manual_preempt_emu_loop.store(true, std::memory_order_release);
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

void SDL3Frontend::clear_quick_savestate_cache() {
  std::lock_guard lock(quick_savestate_cache_mutex_);
  quick_savestate_cache_.clear();
  quick_savestate_cache_valid_ = false;
}

void SDL3Frontend::remove_savestate_triplet(
    const std::filesystem::path &state_path) {
  std::error_code ec;
  std::filesystem::remove(state_path, ec);
  std::filesystem::remove(savestate_meta_path(state_path), ec);
  std::filesystem::remove(savestate_thumb_path(state_path), ec);
}

void SDL3Frontend::sync_quick_savestate_cache_locked() {
  if (savestate_dir_.empty()) {
    quick_savestate_cache_.clear();
    quick_savestate_cache_valid_ = true;
    return;
  }

  std::error_code exists_ec;
  if (!std::filesystem::exists(savestate_dir_, exists_ec)) {
    quick_savestate_cache_.clear();
    quick_savestate_cache_valid_ = true;
    return;
  }

  if (!quick_savestate_cache_valid_)
    quick_savestate_cache_.clear();

  std::unordered_set<std::filesystem::path> on_disk;

  std::error_code iter_ec;
  for (const auto &de : std::filesystem::directory_iterator(savestate_dir_, iter_ec)) {
    if (!is_savestate_file(de))
      continue;

    const auto &p = de.path();
    if (p.stem().string().rfind("quick_", 0) != 0)
      continue;

    on_disk.insert(p);

    const auto it = std::ranges::find_if(
        quick_savestate_cache_, [&](const SavestateEntry &e) {
          return e.state_path == p;
        });
    if (it != quick_savestate_cache_.end())
      continue;

    SavestateEntry entry{};
    entry.state_path = p;
    entry.thumb_path = savestate_thumb_path(p);
    entry.kind = "Quick";
    entry.label = p.stem().string();

    std::error_code time_ec;
    entry.created_at = file_time_to_time_t(de.last_write_time(time_ec));

    if (const auto meta = read_savestate_meta(savestate_meta_path(p));
        meta.has_value()) {
      if (meta->kind != "quick")
        continue;
      entry.label = meta->label.empty() ? entry.label : meta->label;
      if (meta->created_unix > 0)
        entry.created_at = static_cast<std::time_t>(meta->created_unix);
    }

    quick_savestate_cache_.push_back(std::move(entry));
  }

  std::erase_if(quick_savestate_cache_, [&](const SavestateEntry &e) {
    return !on_disk.contains(e.state_path);
  });

  quick_savestate_cache_valid_ = true;
}

void SDL3Frontend::upsert_quick_savestate_cache_locked(
    const std::filesystem::path &state_path, const std::string &label,
    const std::time_t created_at) {
  const auto it = std::ranges::find_if(
      quick_savestate_cache_,
      [&](const SavestateEntry &e) { return e.state_path == state_path; });

  const std::string resolved_label =
      label.empty() ? state_path.stem().string() : label;
  if (it == quick_savestate_cache_.end()) {
    SavestateEntry entry{};
    entry.state_path = state_path;
    entry.thumb_path = savestate_thumb_path(state_path);
    entry.kind = "Quick";
    entry.label = resolved_label;
    entry.created_at = created_at;
    quick_savestate_cache_.push_back(std::move(entry));
    return;
  }

  it->thumb_path = savestate_thumb_path(state_path);
  it->label = resolved_label;
  it->created_at = created_at;
}

void SDL3Frontend::enforce_max_quicksaves_locked() {
  const int configured_max = gui.get_settings_c().max_quicksaves;
  if (configured_max <= 0)
    return;

  const auto max_quick = static_cast<std::size_t>(configured_max);
  std::ranges::sort(quick_savestate_cache_,
                    [](const SavestateEntry &a, const SavestateEntry &b) {
                      if (a.created_at != b.created_at)
                        return a.created_at < b.created_at;
                      return a.state_path.filename().string() <
                             b.state_path.filename().string();
                    });

  while (quick_savestate_cache_.size() > max_quick) {
    remove_savestate_triplet(quick_savestate_cache_.front().state_path);
    quick_savestate_cache_.erase(quick_savestate_cache_.begin());
  }
}

void SDL3Frontend::erase_quick_savestate_cache_entry(
    const std::filesystem::path &state_path) {
  std::lock_guard lock(quick_savestate_cache_mutex_);
  std::erase_if(quick_savestate_cache_, [&](const SavestateEntry &entry) {
    return entry.state_path == state_path;
  });
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

  // ----- Pick output file name -----
  const std::time_t now = std::time(nullptr);
  const std::string prefix = quick ? "quick" : "manual";
  const std::string ts = savestate_timestamp_slug(now);
  const std::string sanitized_label = sanitize_savestate_label(label);

  std::filesystem::path state_path;
  for (int attempt = 0; attempt < 1000; ++attempt) {
    std::string stem = prefix + "_";
    stem.append(ts);
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

  // ----- Write files (no deletions before success) -----
  if (!write_blob_file(state_path, blob))
    return std::nullopt;

  if (const auto thumb_pixels = capture_savestate_thumbnail(); !thumb_pixels.empty())
    write_thumb_raw_argb(savestate_thumb_path(state_path), thumb_pixels);

  SavestateMeta meta{};
  meta.kind = quick ? "quick" : "manual";
  meta.label = label;
  meta.created_unix = static_cast<std::int64_t>(now);
  meta.thumb_w = kSavestateThumbWidth;
  meta.thumb_h = kSavestateThumbHeight;
  write_savestate_meta(savestate_meta_path(state_path), meta);

  // ----- Update quick cache + enforce limit once -----
  if (quick) {
    std::lock_guard lock(quick_savestate_cache_mutex_);
    sync_quick_savestate_cache_locked();
    upsert_quick_savestate_cache_locked(state_path, label, now);
    enforce_max_quicksaves_locked();
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

  std::ranges::sort(savestate_entries_,
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

void SDL3Frontend::build_savestate_manager_window_locked() {
  if (!ui_state.show_savestate_manager)
    return;
  refresh_savestate_entries_locked(false);
  GbcImGui::SavestateManagerCallbacks callbacks{};
  callbacks.queue_manual_save = [this](const std::string &label) {
    queue_manual_savestate_request(label);
  };
  callbacks.request_load_most_recent = [this]() {
    quickload_requested.store(true, std::memory_order_release);
  };
  callbacks.refresh = [this]() {
    savestate_list_dirty.store(true, std::memory_order_release);
    refresh_savestate_entries_locked(true);
  };
  callbacks.queue_load = [this](const std::filesystem::path &path) {
    queue_savestate_load_request(path);
  };
  callbacks.delete_state = [this](const std::filesystem::path &path) {
    remove_savestate_triplet(path);
    erase_quick_savestate_cache_entry(path);
    savestate_list_dirty.store(true, std::memory_order_release);
    refresh_savestate_entries_locked(true);
  };

  gui.build_savestate_manager_window(
      ui_state, host, static_cast<bool>(gbc), savestate_dir_,
      savestate_manual_label_input_, savestate_entries_, savestate_selected_path_,
      callbacks);
}

[[nodiscard]] bool SDL3Frontend::should_preempt_emu_loop() const {
  constexpr auto ord = std::memory_order_relaxed;
  const bool quick_save = quicksave_requested.load(ord);
  const bool quick_load = quickload_requested.load(ord);
  const bool manual = manual_preempt_emu_loop.load(ord);
  return quick_save || quick_load || manual;
}
