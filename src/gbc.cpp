#include "gbc.hpp"
#include "cart/cart.hpp"
#include "cpu/lr35902.hpp"
#include "debugger/breakpoint.hpp"
#include "memory/bus.hpp"
#include "memory/dma.hpp"
#include "memory/mmio/dmg.hpp"
#include "memory/mmio/mmio.hpp"
#include "ppu/palette.hpp"
#include "ppu/ppu.hpp"
#include "savestate/codec.hpp"
#include "timer.hpp"

#include <initializer_list>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string_view>

namespace {
struct ParsedCheat {
  addr_t addr{};
  byte_t value{};
};

std::optional<unsigned> hex_nibble(const char c) {
  if (c >= '0' && c <= '9')
    return static_cast<unsigned>(c - '0');
  if (c >= 'A' && c <= 'F')
    return static_cast<unsigned>(c - 'A' + 10);
  if (c >= 'a' && c <= 'f')
    return static_cast<unsigned>(c - 'a' + 10);
  return std::nullopt;
}

std::optional<byte_t> parse_hex_byte(const std::string_view sv) {
  if (sv.size() != 2)
    return std::nullopt;
  const auto hi = hex_nibble(sv[0]);
  const auto lo = hex_nibble(sv[1]);
  if (!hi.has_value() || !lo.has_value())
    return std::nullopt;
  return static_cast<byte_t>((*hi << 4) | *lo);
}

std::optional<addr_t> parse_hex_addr(const std::string_view sv) {
  if (sv.size() != 4)
    return std::nullopt;
  addr_t out = 0;
  for (const char c : sv) {
    const auto nib = hex_nibble(c);
    if (!nib.has_value())
      return std::nullopt;
    out = static_cast<addr_t>((out << 4) | *nib);
  }
  return out;
}

std::optional<unsigned> parse_hex_nibble(const char c) { return hex_nibble(c); }

std::string strip_non_hex(const std::string_view text) {
  std::string out;
  out.reserve(text.size());
  for (const char c : text) {
    if (std::isxdigit(static_cast<unsigned char>(c)) != 0)
      out.push_back(
          static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
  }
  return out;
}

bool is_cheat_writable_addr(const addr_t addr) {
  if (addr <= 0x7FFF) // Cartridge ROM + mapper registers
    return false;
  if (addr >= 0xFEA0 && addr <= 0xFEFF) // Not usable area
    return false;
  if (addr == 0xFFFF) // IE register is often too destructive to freeze
    return false;
  return true;
}

bool is_cheat_overridable_addr(const addr_t addr) {
  if (addr >= 0xFEA0 && addr <= 0xFEFF) // Not usable area
    return false;
  if (addr == 0xFFFF) // IE register is often too destructive to freeze
    return false;
  return true;
}

bool is_game_genie_addr(const addr_t addr) { return addr <= 0x7FFF; }

std::optional<ParsedCheat> parse_raw_cheat(const std::string_view code) {
  const auto hex = strip_non_hex(code);
  if (hex.size() != 6)
    return std::nullopt;

  const auto addr = parse_hex_addr(std::string_view(hex).substr(0, 4));
  const auto value = parse_hex_byte(std::string_view(hex).substr(4, 2));
  if (!addr.has_value() || !value.has_value())
    return std::nullopt;
  if (!is_cheat_writable_addr(*addr))
    return std::nullopt;

  return ParsedCheat{
      .addr = *addr,
      .value = *value,
  };
}

std::optional<ParsedCheat> parse_gameshark_cheat(const std::string_view code) {
  // Common GB/GBC GameShark format: 01VVLLHH (address is little-endian LLHH)
  // Common Xploder format: 0DVVLLHH
  const auto hex = strip_non_hex(code);
  if (hex.size() != 8)
    return std::nullopt;

  const auto command = parse_hex_byte(std::string_view(hex).substr(0, 2));
  const auto value = parse_hex_byte(std::string_view(hex).substr(2, 2));
  const auto addr_lo = parse_hex_byte(std::string_view(hex).substr(4, 2));
  const auto addr_hi = parse_hex_byte(std::string_view(hex).substr(6, 2));
  if (!command.has_value() || !value.has_value() || !addr_lo.has_value() ||
      !addr_hi.has_value())
    return std::nullopt;

  if (*command != 0x01 && *command != 0x0D)
    return std::nullopt;

  const auto addr =
      static_cast<addr_t>((static_cast<addr_t>(*addr_hi) << 8) | *addr_lo);
  if (!is_cheat_writable_addr(addr))
    return std::nullopt;

  return ParsedCheat{
      .addr = addr,
      .value = *value,
  };
}

std::optional<ParsedCheat>
parse_codebreaker_cheat(const std::string_view code) {
  // mGBA-style GB CodeBreaker parsing:
  //   XXXXXX-YY
  // where XXXXXX = command/address bytes and YY = value byte.
  // The high command byte is accepted but ignored for now.
  std::string normalized;
  normalized.reserve(code.size());
  for (const char c : code) {
    if (std::isspace(static_cast<unsigned char>(c)) != 0)
      continue;
    normalized.push_back(c);
  }

  if (normalized.size() != 9 || normalized[6] != '-')
    return std::nullopt;

  const auto cmd = parse_hex_byte(std::string_view(normalized).substr(0, 2));
  const auto addr_hi =
      parse_hex_byte(std::string_view(normalized).substr(2, 2));
  const auto addr_lo =
      parse_hex_byte(std::string_view(normalized).substr(4, 2));
  const auto value = parse_hex_byte(std::string_view(normalized).substr(7, 2));
  if (!cmd.has_value() || !addr_hi.has_value() || !addr_lo.has_value() ||
      !value.has_value())
    return std::nullopt;

  const auto addr =
      static_cast<addr_t>((static_cast<addr_t>(*addr_hi) << 8) | *addr_lo);
  if (!is_cheat_writable_addr(addr))
    return std::nullopt;

  return ParsedCheat{
      .addr = addr,
      .value = *value,
  };
}

std::optional<AddressBus::CheatOverride>
parse_gamegenie_cheat(const std::string_view code) {
  // Game Boy Game Genie:
  // - 6 digits:  ABC-DEF
  //   value=AB, addr=(F xor F)CDE
  // - 9 digits:  ABC-DEF-GHI
  //   optional compare byte: ROR2(GI) xor BA (middle H nibble is ignored)
  const auto hex = strip_non_hex(code);
  if (hex.size() != 6 && hex.size() != 9)
    return std::nullopt;

  const auto value = parse_hex_byte(std::string_view(hex).substr(0, 2));
  const auto c = parse_hex_nibble(hex[2]);
  const auto d = parse_hex_nibble(hex[3]);
  const auto e = parse_hex_nibble(hex[4]);
  const auto f = parse_hex_nibble(hex[5]);
  if (!value.has_value() || !c.has_value() || !d.has_value() ||
      !e.has_value() || !f.has_value())
    return std::nullopt;

  const auto addr_hi = *f ^ 0xF;
  const auto addr =
      static_cast<addr_t>((addr_hi << 12) | (*c << 8) | (*d << 4) | *e);
  if (!is_game_genie_addr(addr))
    return std::nullopt;

  AddressBus::CheatOverride out{
      .addr = addr,
      .value = *value,
      .has_compare = false,
      .compare = 0,
  };

  if (hex.size() == 9) {
    const auto g = parse_hex_nibble(hex[6]);
    const auto i = parse_hex_nibble(hex[8]);
    if (!g.has_value() || !i.has_value())
      return std::nullopt;
    const auto encoded = static_cast<byte_t>((*g << 4) | *i);
    const auto ror2 = static_cast<byte_t>((encoded >> 2) | (encoded << 6));
    out.has_compare = true;
    out.compare = static_cast<byte_t>(ror2 ^ 0xBA);
  }

  return out;
}

bool looks_like_gamegenie(const std::string_view code) {
  // Keep this lenient; explicit format selection still exists in UI.
  return code.find('-') != std::string_view::npos;
}

std::optional<AddressBus::CheatOverride>
to_override(const std::optional<ParsedCheat> parsed) {
  if (!parsed.has_value())
    return std::nullopt;
  return AddressBus::CheatOverride{
      .addr = parsed->addr,
      .value = parsed->value,
  };
}

std::optional<AddressBus::CheatOverride>
parse_gameshark_override(const std::string_view code) {
  return to_override(parse_gameshark_cheat(code));
}

std::optional<AddressBus::CheatOverride>
parse_raw_override(const std::string_view code) {
  // Extended raw compare format:
  //   AAAA?CC:VV
  // Applies VV only when the original byte at AAAA equals CC.
  if (const auto qmark = code.find('?'); qmark != std::string_view::npos) {
    const auto colon = code.find(':', qmark + 1);
    if (colon == std::string_view::npos)
      return std::nullopt;

    const auto addr_hex = strip_non_hex(code.substr(0, qmark));
    const auto cmp_hex =
        strip_non_hex(code.substr(qmark + 1, colon - (qmark + 1)));
    const auto value_hex = strip_non_hex(code.substr(colon + 1));
    if (addr_hex.size() != 4 || cmp_hex.size() != 2 || value_hex.size() != 2)
      return std::nullopt;

    const auto addr = parse_hex_addr(addr_hex);
    const auto compare = parse_hex_byte(cmp_hex);
    const auto value = parse_hex_byte(value_hex);
    if (!addr.has_value() || !compare.has_value() || !value.has_value())
      return std::nullopt;
    if (!is_cheat_overridable_addr(*addr))
      return std::nullopt;

    return AddressBus::CheatOverride{
        .addr = *addr,
        .value = *value,
        .has_compare = true,
        .compare = *compare,
    };
  }

  return to_override(parse_raw_cheat(code));
}

std::optional<AddressBus::CheatOverride>
parse_codebreaker_override(const std::string_view code) {
  return to_override(parse_codebreaker_cheat(code));
}

using CheatParser =
    std::optional<AddressBus::CheatOverride> (*)(std::string_view);

std::optional<AddressBus::CheatOverride>
first_match(const std::string_view code,
            const std::initializer_list<CheatParser> parsers) {
  for (const auto parser : parsers) {
    if (const auto compiled = parser(code); compiled.has_value())
      return compiled;
  }
  return std::nullopt;
}

std::optional<AddressBus::CheatOverride>
compile_cheat(const std::string_view code,
              const GameBoyColor::CheatFormat format) {
  using fmt = GameBoyColor::CheatFormat;
  switch (format) {
  case fmt::CHEAT_GAMESHARK:
    return parse_gameshark_override(code);
  case fmt::CHEAT_RAW:
    return parse_raw_override(code);
  case fmt::CHEAT_CODEBREAKER:
    return parse_codebreaker_override(code);
  case fmt::CHEAT_GAME_GENIE:
    return parse_gamegenie_cheat(code);
  case fmt::CHEAT_AUTO:
    if (looks_like_gamegenie(code)) {
      if (const auto gg = parse_gamegenie_cheat(code); gg.has_value())
        return gg;
    }
    return first_match(code,
                       {parse_codebreaker_override, parse_gameshark_override,
                        parse_raw_override, parse_gamegenie_cheat});
  default:
    return first_match(code,
                       {parse_gameshark_override, parse_raw_override,
                        parse_codebreaker_override, parse_gamegenie_cheat});
  }
}
} // namespace

GameBoyColor::GameBoyColor(Frontend &frontend, const std::string &bios_path)
    : Debuggable(debugger_), debugger_(std::nullopt), fe_(frontend) {
  system_init(); // Connects all system components to each other

  /* We set CGB mode based on the size of the boot ROM. This is the best way
   * to make sure we get the coloring right, but it will likely cause strange
   * behavior in-game. Behavior should be okay with any CGB bios though. */
  try {
    bios_ = BootROM(bios_path);
    // Strange behavior if you use CGB game with DMG bios. Nothing you can do!
    sys_.cgb_mode = bios_->is_large_rom();
  }

  /* If this fails for whatever reason, simply continue as if we didn't have a
     BIOS configured. Emulator will start in CGB mode (obviously). */
  catch (std::runtime_error &) {
    bios_ = std::nullopt;
    skip_bios();
  }
  cram_init_mono(); // Just in case BIOS does not init CRAM
}

GameBoyColor::GameBoyColor(Frontend &frontend, const BootROM &rom)
    : Debuggable(debugger_), debugger_(std::nullopt), fe_(frontend) {
  system_init();
  bios_ = rom; // We assume rom is already valid
  sys_.cgb_mode = bios_->is_large_rom();
  cram_init_mono();
}

GameBoyColor::GameBoyColor(Frontend &frontend)
    : Debuggable(debugger_), debugger_(std::nullopt), bios_(std::nullopt),
      fe_(frontend) {
  system_init(); // Connects all system components
  skip_bios();   // BIOS is left unconfigured
  /* We still kind of have to do this here in case we run DMG games. Will likely
   * end up staring at a pure black screen in such cases if we don't. */
  cram_init_mono();
}

void GameBoyColor::system_init() {
  /* General system operation info */
  sys_ = {
      .elapsed_clocks = 0,
      .cgb_mode = true,
      .halted = false,
      .speed_switch_armed = false,
      .double_speed = false,
  };

  /* Component initialization */
  bus = std::make_unique<AddressBus>(sys_, debugger_, bios_);
  cpu = std::make_unique<LR35902>(bus.get(), debugger_, sys_);
  apu = std::make_unique<APU>(*bus, fe_);
  ppu = std::make_unique<PixelProcessingUnit>(bus.get(), fe_, debugger_, sys_);
  timer = std::make_unique<TimerUnit>(bus.get());
  serial = std::make_unique<SerialUnit>(bus.get());

  /* Joypad initialization */
  auto *const joypad_reg = dynamic_cast<Joypad::JOYP *>(
      bus->get_mmio(IORegisterMapping::MMIO_JOYPAD));
  auto *const if_reg = dynamic_cast<InterruptBits *>(
      bus->get_mmio(IORegisterMapping::MMIO_INT_FLAGS));
  if (!joypad_reg || !if_reg)
    throw std::logic_error("Failed to configure joypad MMIO");

  /* To avoid running into problems with other registers it depends on in time,
   * we have to invoke this method to configure the dependencies it needs after
   * we can guarantee they have been instantiated. */
  joypad_reg->set_interrupt_reg(if_reg);
}

void GameBoyColor::skip_bios() const {
  using mmio = IORegisterMapping;

  /* We cannot simply start executing without a BIOS for numerous reasons. So,
   * if we want to skip the BIOS, we need the system to 'pretend' like it really
   * did run through the BIOS. */
  constexpr LR35902::ProcessorState boot_regs = {
      .pc = 0x0100,
      .sp = 0xFFFE,
      .a = 0x11,
      .b = 0x00,
      .c = 0x14,
      .d = 0x00,
      .e = 0x00,
      .f = 0x00,
      .h = 0xC0,
      .l = 0x60,
  };
  cpu->load_state(boot_regs); // Fake CPU register values

  // Load hardware MMIO registers with post-BIOS initial conditions
  bus->write_byte(static_cast<addr_t>(mmio::MMIO_JOYPAD), 0xC7);
  // TODO: Serial registers FF01 and FF02
  bus->write_byte(static_cast<addr_t>(mmio::MMIO_TIMER_TIMA), 0x00);
  bus->write_byte(static_cast<addr_t>(mmio::MMIO_TIMER_TMA), 0x00);
  bus->write_byte(static_cast<addr_t>(mmio::MMIO_TIMER_TAC), 0xF8);
  bus->write_byte(static_cast<addr_t>(mmio::MMIO_INT_FLAGS), 0xE1);
  // Audio registers handled in APU
  bus->write_byte(static_cast<addr_t>(mmio::MMIO_LCD_CTRL), 0x91);
  bus->write_byte(static_cast<addr_t>(mmio::MMIO_LCD_SCY), 0x00);
  bus->write_byte(static_cast<addr_t>(mmio::MMIO_LCD_SCX), 0x00);
  bus->write_byte(static_cast<addr_t>(mmio::MMIO_LCD_Y_COMP), 0x00);
  bus->write_byte(static_cast<addr_t>(mmio::MMIO_LCD_Y_COMP), 0x00);
  // DMA (0xFF46) left blank intentionally - dont want to trigger it
  bus->write_byte(static_cast<addr_t>(mmio::MMIO_LCD_BGP), 0xFC);
  bus->write_byte(static_cast<addr_t>(mmio::MMIO_LCD_WY), 0x00);
  bus->write_byte(static_cast<addr_t>(mmio::MMIO_LCD_WX), 0x00);
  // KEY0 (0xFF4C) left blank intentionally - primed during cart insertion
  // KEY1 (0xFF4D) left blank intentionally - not touched in real cgb bios
  bus->write_byte(static_cast<addr_t>(mmio::MMIO_VRAM_BANK), 0xFE);
  bus->write_byte(static_cast<addr_t>(mmio::MMIO_VDMA1), 0xFF);
  bus->write_byte(static_cast<addr_t>(mmio::MMIO_VDMA2), 0xFF);
  bus->write_byte(static_cast<addr_t>(mmio::MMIO_VDMA3), 0xFF);
  bus->write_byte(static_cast<addr_t>(mmio::MMIO_VDMA4), 0xFF);
  // VDMA5 (0xFF55) left blank intentionally - dont want to trigger it
  bus->write_byte(static_cast<addr_t>(mmio::MMIO_VDMA4), 0xFF);
  bus->write_byte(static_cast<addr_t>(mmio::MMIO_WRAM_BANK), 0xF8);
  bus->write_byte(static_cast<addr_t>(mmio::MMIO_INT_ENABLE), 0x00);

  // Finally, perform a synthetic write to unmap the boot ROM and terminate
  // the execution of our fake basic input-output system.
  bus->write_byte(static_cast<addr_t>(mmio::MMIO_VRAM_BANK), 0xFE);

  // Color ram has to be initialized for both sprites and background
  cram_init_mono(mmio::MMIO_LCD_BGPI, mmio::MMIO_LCD_BGPD);
  cram_init_mono(mmio::MMIO_LCD_OBPI, mmio::MMIO_LCD_OBPD);
}

void GameBoyColor::cram_init_mono() const {
  using mmio = IORegisterMapping;
  cram_init_mono(mmio::MMIO_LCD_BGPI, mmio::MMIO_LCD_BGPD);
  cram_init_mono(mmio::MMIO_LCD_OBPI, mmio::MMIO_LCD_OBPD);
}

void GameBoyColor::cram_init_mono(IORegisterMapping index,
                                  IORegisterMapping data) const {
  constexpr auto nr_palettes = 8;

  /* Convert index and data enumerations into 16-bit addresses */
  const auto cram_index = static_cast<addr_t>(index);
  const auto cram_data = static_cast<addr_t>(data);
  bus->write_byte(cram_index, 0x80); // Write high bit for auto increment

  /* This is where CRAM is actually populated, by writing the actual values in
   * that would normally be written in over the address bus. */
  for (auto pal{0}; pal < nr_palettes; pal++) {
    constexpr auto nr_colors = 4;
    for (byte_t color{0}; color < nr_colors; color++) {
      const std::uint32_t argb8888 = get_mono_color(color & 0x3);
      const std::uint16_t rgb555 = argb8888_to_rgb555(argb8888);
      bus->write_byte(cram_data, static_cast<byte_t>(rgb555 & 0xFF));
      bus->write_byte(cram_data, static_cast<byte_t>((rgb555 >> 8) & 0xFF));
    }
  }
}

void GameBoyColor::insert_cartridge(const cart &c) {
  const byte_t &cgb_flag = c.header.cgb_flag();
  if (!bus)
    throw std::logic_error("Bus not initialized");
  bus->insert_cartridge(c);

  /* IMPORTANT: on the real hardware, this flag is set via KEY0 during the BIOS
   * based on the cartridge header. If we are skipping the BIOS, this init step
   * will never happen. Hence, we must do it ourselves when we skip the BIOS.
   * --------------------------------------------------------------------------
   * Another side note, KEY0 uses a reference to this boolean to formulate the
   * actual value of the register read over the address bus. Hence, by changing
   * this we are also updating the value of KEY0 respectively. */
  if (!bios_.has_value())
    sys_.cgb_mode = cgb_enabled(cgb_flag);
}

void GameBoyColor::init_test_bed() const {
  if (!bus)
    throw std::logic_error("Bus not initialized");

  /* Init convenience RAM-only cartridge for testing */
  bus->init_test_bed();
}

void GameBoyColor::step_dma(const bool fast_cycle) const {
  bus->get_oam_dma().step(); // Runs 2X in double speed

  /* As described elsewhere, HDMA and GDMA have an initialization phase that
   * does run fast in double speed mode, but the transfers themselves don't */
  if (fast_cycle)
    bus->get_vdma().step_fast_cycle();
  else
    bus->get_vdma().step();
}
bool GameBoyColor::vdma_enabled() const { return bus->get_vdma().enabled(); }

void GameBoyColor::step_processor() const {
  if (const auto &vdma = bus->get_vdma();
      !vdma.enabled()) // CPU is halted until VDMA is complete
    cpu->step();
}

void GameBoyColor::step() {
  try_brk(Debug::BreakReason::BRK_STEP_CLOCK_CYCLE);

  step_processor();
  step_dma(false);
  ppu->step();
  timer->step();
  apu->step();

  // System clocks are maintained in unit `t-cycles`
  ++sys_.elapsed_clocks;

  // If we are in double speed mode, step affected components again
  if (sys_.double_speed) {
    step_processor();
    step_dma(true);
    timer->step();
  }
}

GameBoyColor::CheatStats
GameBoyColor::configure_cheats(const std::vector<CheatCode> &cheats) {
  std::vector<AddressBus::CheatOverride> overrides{};
  overrides.reserve(cheats.size());
  cheat_stats_ = {};
  cheat_stats_.total = cheats.size();

  for (const auto &[enabled, code, format] : cheats) {
    if (!enabled)
      continue;
    cheat_stats_.enabled += 1;

    const auto compiled = compile_cheat(code, static_cast<CheatFormat>(format));

    if (!compiled.has_value()) {
      cheat_stats_.rejected += 1;
      continue;
    }
    overrides.push_back(*compiled);
  }

  if (bus)
    bus->set_cheat_overrides(overrides);

  cheat_stats_.active = overrides.size();
  return cheat_stats_;
}

bool GameBoyColor::savestate_ready() const {
  return cpu && cpu->savestate_ready();
}

std::vector<byte_t> GameBoyColor::savestate_serialize() const {
  Savestate::Writer out{};
  cpu->parse_savestate(out);
  timer->parse_savestate(out);
  ppu->parse_savestate(out);
  return out.get();
}

void GameBoyColor::savestate_deserialize(const std::span<const byte_t> data) {
  Savestate::Reader in(data);
  cpu->parse_savestate(in);
  timer->parse_savestate(in);
  ppu->parse_savestate(in);
}

std::size_t GameBoyColor::savestate_size() const {
  Savestate::Sizer sz{};
  cpu->parse_savestate(sz);
  timer->parse_savestate(sz);
  ppu->parse_savestate(sz);
  return sz.get();
}

#undef SS_WALK
