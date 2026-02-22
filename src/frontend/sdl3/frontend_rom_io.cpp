#include "frontend/sdl3/frontend.hpp"
#include <algorithm>
#include <chrono>
#include <curl/curl.h>
#include <fstream>
#include <miniz.h>
#include <random>

namespace {
bool looks_like_url(const std::string &s) {
  return s.rfind("http://", 0) == 0 || s.rfind("https://", 0) == 0;
}

std::string to_lower(std::string s) {
  for (char &c : s)
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

bool has_ext(const std::string &path, const std::string &ext) {
  const auto p = to_lower(path);
  const auto e = to_lower(ext);
  if (p.size() < e.size())
    return false;
  return p.compare(p.size() - e.size(), e.size(), e) == 0;
}

bool file_starts_with_zip_magic(const std::filesystem::path &p) {
  std::ifstream f(p, std::ios::binary);
  if (!f)
    return false;
  unsigned char sig[4]{};
  f.read(reinterpret_cast<char *>(sig), 4);
  return sig[0] == 0x50 && sig[1] == 0x4B && sig[2] == 0x03 && sig[3] == 0x04;
}

std::filesystem::path make_temp_file(const std::filesystem::path &root,
                                     std::string_view suffix) {
  std::random_device rd;
  std::mt19937_64 gen(rd());
  std::uniform_int_distribution<std::uint64_t> dis;

  for (int attempt = 0; attempt < 32; ++attempt) {
    const auto name = "gbc_" + std::to_string(dis(gen)) + std::string(suffix);
    auto p = root / name;
    if (std::error_code ec; !std::filesystem::exists(p, ec))
      return p;
  }

  const auto name = "gbc_" +
                    std::to_string(std::chrono::high_resolution_clock::now()
                                       .time_since_epoch()
                                       .count()) +
                    std::string(suffix);
  return root / name;
}

struct CurlDownloadCtx {
  std::atomic<float> *progress{};
  std::stop_token st;
};

size_t curl_write_file_cb(const char *ptr, const size_t size,
                          const size_t nmemb, void *userdata) {
  auto *fp = static_cast<FILE *>(userdata);
  return std::fwrite(ptr, size, nmemb, fp) * size;
}

int curl_xferinfo_cb(void *clientp, const curl_off_t dltotal,
                     const curl_off_t dlnow, curl_off_t, curl_off_t) {
  const auto *ctx = static_cast<CurlDownloadCtx *>(clientp);
  if (ctx && ctx->st.stop_requested())
    return 1;
  if (!ctx || !ctx->progress)
    return 0;

  if (dltotal > 0) {
    const float p = static_cast<float>(dlnow) / static_cast<float>(dltotal);
    ctx->progress->store(std::clamp(p, 0.0f, 1.0f), std::memory_order_relaxed);
  } else {
    ctx->progress->store(-1.0f, std::memory_order_relaxed);
  }
  return 0;
}

std::string process_path(const std::string &path) {
  if (path.find("file:/", 0) == 0) {
    return path.substr(6);
  }
  return path;
}
} // namespace

void SDL3Frontend::sync_io_status_to_ui() {
  ui_state.io_busy = io_busy.load(std::memory_order_relaxed);
  ui_state.io_progress = io_progress.load(std::memory_order_relaxed);
  {
    std::lock_guard lk(io_status_mutex);
    ui_state.io_status = io_status;
  }
}

bool SDL3Frontend::consume_rom_io_result(std::string &rom_path_on_disk,
                                         std::string &display_label) {
  std::lock_guard lk(rom_io_mutex);
  if (!rom_ready_path)
    return false;
  rom_path_on_disk = std::move(*rom_ready_path);
  display_label = std::move(rom_ready_label);
  rom_ready_path.reset();
  rom_ready_label.clear();
  return true;
}


int SDL3Frontend::request_zip_choice_blocking(
    const std::string &zip_label, const std::vector<std::string> &entries,
    const std::stop_token &st) {
  {
    std::lock_guard lock(ui_mutex);
    ui_state.zip_picker_title = zip_label;
    ui_state.zip_rom_entries = entries;
    ui_state.zip_rom_selected_idx = 0;
    ui_state.zip_picker_action = 0;
    ui_state.show_zip_picker_popup = true;
  }

  std::unique_lock lk(zip_choice_mutex);
  zip_choice_pending = true;
  zip_choice_result = -1;
  zip_choice_cancelled = false;

  zip_choice_cv.wait(
      lk, [&] { return !zip_choice_pending || st.stop_requested(); });

  if (st.stop_requested() || zip_choice_cancelled)
    return -1;
  return zip_choice_result;
}

// Must be called with ui_mutex held
void SDL3Frontend::poll_zip_choice_response() {
  int action = 0;
  int idx = 0;
  action = ui_state.zip_picker_action;
  idx = ui_state.zip_rom_selected_idx;
  if (action != 0) {
    ui_state.zip_picker_action = 0;
    ui_state.zip_picker_title.clear();
    ui_state.zip_rom_entries.clear();
  }
  if (action == 0)
    return;

  {
    std::lock_guard lk(zip_choice_mutex);
    if (!zip_choice_pending)
      return;
    zip_choice_result = idx;
    zip_choice_cancelled = (action == 2);
    zip_choice_pending = false;
  }
  zip_choice_cv.notify_all();
}

void SDL3Frontend::handle_drop(const SDL_Event &e) {
  if (!e.drop.data)
    return;
  std::lock_guard lock(ui_mutex);
  ui_state.load_rom_path = process_path(e.drop.data); // file path or URL text
  ui_state.load_rom_name = "";
  ui_state.request_load_rom = true;
}

void SDL3Frontend::start_rom_io_job(const std::string &source) {
  if (rom_io_thread.joinable()) {
    {
      std::lock_guard lk(zip_choice_mutex);
      zip_choice_cancelled = true;
      zip_choice_pending = false;
    }
    zip_choice_cv.notify_all();
    rom_io_thread.request_stop();
    rom_io_thread.join();
  }

  io_busy.store(true, std::memory_order_relaxed);
  io_progress.store(-1.0f, std::memory_order_relaxed);
  {
    std::lock_guard lk(io_status_mutex);
    io_status = "Preparing ROM...";
  }

  rom_io_thread = std::jthread([this, source](const std::stop_token &st) {
    auto set_status = [&](std::string s) {
      std::lock_guard lk(io_status_mutex);
      io_status = std::move(s);
    };

    auto cleanup_temp = [&] {
      std::error_code ec;
      if (last_tmp_rom)
        std::filesystem::remove(*last_tmp_rom, ec);
      if (last_tmp_zip)
        std::filesystem::remove(*last_tmp_zip, ec);
      last_tmp_rom.reset();
      last_tmp_zip.reset();
    };

    cleanup_temp();

    auto fail = [&](const std::string &msg) {
      Logger::push(LogLevel::Warning, "ROM", "ROM load failed", msg);
      set_status(msg);
      io_busy.store(false, std::memory_order_relaxed);
    };

    auto succeed = [&](const std::string &rom_on_disk,
                       const std::string &label) {
      {
        std::lock_guard lk(rom_io_mutex);
        rom_ready_path = rom_on_disk;
        rom_ready_label = label;
      }
      io_busy.store(false, std::memory_order_relaxed);
      io_progress.store(-1.0f, std::memory_order_relaxed);
      set_status("Ready");
    };

    std::filesystem::path local_path;
    std::string label = source;

    if (looks_like_url(source)) {
      set_status("Downloading...");
      io_progress.store(0.0f, std::memory_order_relaxed);

      std::string suffix = ".bin";
      {
        const auto q = source.find_first_of("?#");
        const std::string base =
            (q == std::string::npos) ? source : source.substr(0, q);
        const auto slash = base.find_last_of('/');
        const auto dot = base.find_last_of('.');
        if (dot != std::string::npos &&
            (slash == std::string::npos || dot > slash)) {
          suffix = base.substr(dot);
          if (suffix.size() > 16)
            suffix = ".bin";
        }
      }

      local_path = make_temp_file(tmp_root, suffix);
      FILE *fp = std::fopen(local_path.string().c_str(), "wb");
      if (!fp) {
        fail("Failed to create temp file for download");
        return;
      }

      CURL *curl = curl_easy_init();
      if (!curl) {
        std::fclose(fp);
        fail("curl_easy_init() failed");
        return;
      }

      CurlDownloadCtx ctx;
      ctx.progress = &io_progress;
      ctx.st = st;

      curl_easy_setopt(curl, CURLOPT_URL, source.c_str());
      curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
      curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);
      curl_easy_setopt(curl, CURLOPT_USERAGENT, "gbc_full/1.0");
      curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_file_cb);
      curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);
      curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
      curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, curl_xferinfo_cb);
      curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &ctx);

      const CURLcode res = curl_easy_perform(curl);
      curl_easy_cleanup(curl);
      std::fclose(fp);

      if (st.stop_requested()) {
        std::error_code ec;
        std::filesystem::remove(local_path, ec);
        set_status("Download cancelled");
        io_busy.store(false, std::memory_order_relaxed);
        return;
      }

      if (res != CURLE_OK) {
        std::error_code ec;
        std::filesystem::remove(local_path, ec);
        fail(std::string("Download failed: ") + curl_easy_strerror(res));
        return;
      }

      last_tmp_zip = local_path; // downloaded file (rom or zip)
    } else {
      local_path = source;
    }

    if (st.stop_requested()) {
      set_status("Cancelled");
      io_busy.store(false, std::memory_order_relaxed);
      return;
    }

    const bool is_zip = has_ext(local_path.string(), ".zip") ||
                        file_starts_with_zip_magic(local_path);

    if (!is_zip) {
      if (looks_like_url(source))
        last_tmp_rom = local_path;
      succeed(local_path.string(), label);
      return;
    }

    set_status("Scanning ZIP...");
    io_progress.store(-1.0f, std::memory_order_relaxed);

    std::vector<int> candidate_indices;
    std::vector<std::string> candidate_names;

    {
      mz_zip_archive zip{};
      if (!mz_zip_reader_init_file(&zip, local_path.string().c_str(), 0)) {
        fail("Failed to open ZIP file");
        return;
      }
      const int n = static_cast<int>(mz_zip_reader_get_num_files(&zip));
      for (int i = 0; i < n; ++i) {
        if (mz_zip_reader_is_file_a_directory(&zip, i))
          continue;
        mz_zip_archive_file_stat stt{};
        if (!mz_zip_reader_file_stat(&zip, i, &stt))
          continue;
        const std::string name =
            stt.m_filename[0] != '\0' ? stt.m_filename : "";
        if (has_ext(name, ".gb") || has_ext(name, ".gbc")) {
          candidate_indices.push_back(i);
          candidate_names.push_back(name);
        }
      }
      mz_zip_reader_end(&zip);
    }

    if (candidate_indices.empty()) {
      fail("ZIP did not contain any .gb/.gbc files");
      return;
    }

    int chosen = 0;
    if (candidate_indices.size() > 1) {
      set_status("Choose ROM from ZIP...");
      chosen = request_zip_choice_blocking(
          std::string("ROMs found in ZIP (") +
              std::filesystem::path(local_path).filename().string() + ")",
          candidate_names, st);
      if (chosen < 0 || chosen >= static_cast<int>(candidate_indices.size())) {
        set_status("ZIP selection cancelled");
        io_busy.store(false, std::memory_order_relaxed);
        return;
      }
    }

    const int chosen_idx = candidate_indices[chosen];
    const std::string chosen_name = candidate_names[chosen];

    set_status("Extracting ROM...");
    io_progress.store(-1.0f, std::memory_order_relaxed);

    const std::string out_suffix =
        has_ext(chosen_name, ".gbc") ? ".gbc" : ".gb";
    const auto out_rom = make_temp_file(tmp_root, out_suffix);

    {
      mz_zip_archive zip{};
      if (!mz_zip_reader_init_file(&zip, local_path.string().c_str(), 0)) {
        fail("Failed to re-open ZIP for extraction");
        return;
      }
      const bool ok = mz_zip_reader_extract_to_file(
                          &zip, chosen_idx, out_rom.string().c_str(), 0) != 0;
      mz_zip_reader_end(&zip);
      if (!ok) {
        std::error_code ec;
        std::filesystem::remove(out_rom, ec);
        fail("Failed to extract ROM from ZIP");
        return;
      }
    }

    last_tmp_rom = out_rom;
    label = source + " :: " + chosen_name;
    succeed(out_rom.string(), label);
  });
}

