#include "frontend/renderer.hpp"
#include <SDL3/SDL.h>
#include <backends/imgui_impl_sdl3.h>
#include <backends/imgui_impl_sdlrenderer3.h>
#include <chrono>
#include <imgui.h>
#include <memory>
#include <misc/cpp/imgui_stdlib.h>
#include <stdexcept>

const char *filters =
    "GBC ROM files (*.gb *.gbc){.gb,.gbc},All files (*.*){.*}";

Renderer::Renderer(bool is_headless) : headless(is_headless) {
  if (!SDL_Init(SDL_INIT_VIDEO))
    throw std::runtime_error(SDL_GetError());

  if (!headless) {
    window = SDL_CreateWindow("GBC", framebuf_width * scale,
                              framebuf_height * scale, SDL_WINDOW_RESIZABLE);
    if (!window)
      throw std::runtime_error(SDL_GetError());

    renderer = SDL_CreateRenderer(window, nullptr);
    if (!renderer)
      throw std::runtime_error(SDL_GetError());

    texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
                                SDL_TEXTUREACCESS_STREAMING, framebuf_width,
                                framebuf_height);
    if (!texture)
      throw std::runtime_error(SDL_GetError());

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    if (!ImGui_ImplSDL3_InitForSDLRenderer(window, renderer))
      throw std::runtime_error("Failed to initialize ImGui SDL3 backend");
    if (!ImGui_ImplSDLRenderer3_Init(renderer))
      throw std::runtime_error(
          "Failed to initialize ImGui SDL renderer backend");

    /* For 60hz synchronization */
    elapsed_time = std::chrono::steady_clock::now();

    config.path = ".";
    config.flags =
        ImGuiFileDialogFlags_Modal | ImGuiFileDialogFlags_ReadOnlyFileNameField;
  }

  /* Still allocate frame buffer for snapshots in headless mode */
  framebuffers[0] =
      std::make_unique<std::uint32_t[]>(framebuf_height * framebuf_width);
  framebuffers[1] =
      std::make_unique<std::uint32_t[]>(framebuf_height * framebuf_width);
  pixels_rendered = 0;
  running = true;
  clear();
}

Renderer::~Renderer() {
  if (!headless) {
    ImGui_ImplSDLRenderer3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
  }
}

void Renderer::putPixel(int x, int y, std::uint32_t c) {
  if (x < 0 || x >= framebuf_width || y < 0 || y >= framebuf_height)
    return;
  const int back_index = 1 - front_index.load(std::memory_order_relaxed);
  framebuffers[back_index][y * framebuf_width + x] = c;
  ++pixels_rendered;

  if (pixels_rendered != framebuf_height * framebuf_width)
    return;
  pixels_rendered = 0;
  front_index.store(back_index, std::memory_order_release);
}

void Renderer::poll_events() {
  SDL_Event e;
  while (SDL_PollEvent(&e)) {
    if (!headless)
      ImGui_ImplSDL3_ProcessEvent(&e);
    if (e.type == SDL_EVENT_QUIT)
      running = false;
  }
}

inline auto calc_delta(const std::chrono::steady_clock::time_point &start) {
  return std::chrono::duration_cast<std::chrono::microseconds>(
             std::chrono::steady_clock::now() - start)
      .count();
}

void Renderer::present() {
  using namespace std::chrono;

  if (headless)
    return;

  const std::uint32_t *pixels = front_buffer();
  uint32_t *texturePixels;
  int pitch;
  SDL_LockTexture(texture, nullptr, reinterpret_cast<void **>(&texturePixels),
                  &pitch);

  pitch /= sizeof(uint32_t);
  for (int y = 0; y < framebuf_height; ++y)
    for (int x = 0; x < framebuf_width; ++x)
      texturePixels[y * pitch + x] = pixels[y * framebuf_width + x];

  SDL_UnlockTexture(texture);
  SDL_RenderClear(renderer);
  SDL_RenderTexture(renderer, texture, nullptr, nullptr);

  ImGui_ImplSDLRenderer3_NewFrame();
  ImGui_ImplSDL3_NewFrame();
  ImGui::NewFrame();
  build_ui();
  ImGui::Render();
  ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer);

  SDL_RenderPresent(renderer);
  poll_events();

  /* Sync to sixty herts */
  elapsed_time = std::chrono::steady_clock::now();
}

void Renderer::clear() {
  constexpr std::uint32_t black = 0xFF000000;
  for (int i = 0; i < framebuf_width * framebuf_height; ++i)
    for (auto &buffer : framebuffers)
      buffer[i] = black;
}

bool Renderer::consume_load_request(std::string &rom_path) {
  std::lock_guard<std::mutex> lock(ui_mutex);
  if (!ui_state.request_load)
    return false;
  ui_state.request_load = false;
  rom_path = ui_state.rom_path;
  return true;
}

void Renderer::set_status_message(std::string message) {
  std::lock_guard<std::mutex> lock(ui_mutex);
  ui_state.status_message = std::move(message);
}

void Renderer::build_ui() {
  std::lock_guard<std::mutex> lock(ui_mutex);
  ImGuiIO &io = ImGui::GetIO();
  float display_w = io.DisplaySize.x;
  float display_h = io.DisplaySize.y;

  max_size =
      ImVec2((float)display_w, (float)display_h); // The full display area
  min_size = ImVec2(400.0f, 250.0f);

  if (ImGui::BeginMainMenuBar()) {
    if (ImGui::BeginMenu("File")) {
      if (ImGui::MenuItem("Load ROM..."))
        ImGuiFileDialog::Instance()->OpenDialog(
            "RomFileDialog", "Choose a ROM file", filters, config);
      if (ImGui::MenuItem("Quit"))
        running = false;
      ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Settings")) {
      if (ImGui::MenuItem("Emulator Settings"))
        ui_state.show_settings_window = true;
      ImGui::EndMenu();
    }
    ImGui::EndMainMenuBar();
  }

  if (ImGuiFileDialog::Instance()->Display(
          "RomFileDialog", ImGuiWindowFlags_NoCollapse, min_size, max_size)) {
    if (ImGuiFileDialog::Instance()->IsOk()) {
      ui_state.rom_path = ImGuiFileDialog::Instance()->GetFilePathName();
      ui_state.request_load = true;
    }
    ImGuiFileDialog::Instance()->Close();
    ui_state.show_load_window = false;
  }

  if (ui_state.show_settings_window) {
    ImGui::Begin("Settings (Placeholder)", &ui_state.show_settings_window);
    ImGui::TextUnformatted("Settings UI coming soon.");
    ImGui::End();
  }
}

const std::uint32_t *Renderer::front_buffer() const {
  return framebuffers[front_index.load(std::memory_order_acquire)].get();
}
