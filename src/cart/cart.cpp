#include "cart/cart.hpp"
#include "debugger/print.hpp"
#include "format.hpp"
#include "frontend/logger.hpp"
#include "savestate/codec.hpp"

#include <algorithm>
#include <array>
#include <bitset>
#include <cstring>
#include <fstream>
#include <stdexcept>

enum : std::uint16_t {
  F_GLOBAL_CHECKSUM = 1,
  F_HEADER_CHECKSUM,
  F_CART_TYPE,
  F_RAM_BYTES,
  F_MAPPER,
};

template <typename T> void Cartridge::parse_savestate(T &t) {
  constexpr auto version = 1; // Schema revision
  t.chunk_header(version, Savestate::C_CART);
  if (!mbc_)
    throw std::runtime_error("Cartridge::parse_savestate() no mapper");

  // Here we go off an assumption that things like ROM/RAM will not change in
  // size, since you should only be able to load savestates from a given game
  // if that cartridge is actually inserted.
  t.field_generic(F_CART_TYPE, image_.header.cartridge_type);
  t.field_bytes(F_RAM_BYTES, mbc_->ram());
  t.field_complex(F_MAPPER, [&](T &t) { mbc_->parse_savestate(t); });

  // Might not need to load these, but keeping this anyway
  t.field_generic(F_GLOBAL_CHECKSUM, image_.computed_global_checksum);
  t.field_generic(F_HEADER_CHECKSUM, image_.header.header_checksum);
  t.eof();
}

template void Cartridge::parse_savestate<Savestate::Writer>(Savestate::Writer &);
template void Cartridge::parse_savestate<Savestate::Reader>(Savestate::Reader &);
template void Cartridge::parse_savestate<Savestate::Sizer>(Savestate::Sizer &);
template void Cartridge::parse_savestate<Savestate::Checker>(Savestate::Checker &);

static bool is_ascii_upper_alnum(const byte_t b) {
  const unsigned c = b;
  return (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
}

static std::string ascii_ztrim(const byte_t *p, const std::size_t n) {
  std::size_t len = 0;
  while (len < n && p[len] != 0x00)
    ++len;
  return {reinterpret_cast<const char *>(p), len};
}

// Header checksum algorithm from boot ROM
static byte_t compute_header_checksum(const std::span<const byte_t> rom, const size_t offset) {
  byte_t checksum = 0;
  for (addr_t addr = 0x0134; addr <= 0x014C; ++addr) {
    checksum = static_cast<byte_t>(checksum - rom[addr + offset] - 1);
  }
  return checksum;
}

// Global checksum is a simple 16-bit sum excluding bytes 014E-014F
static std::uint16_t compute_global_checksum(const std::span<const byte_t> rom,
                                             const size_t offset) {
  std::uint32_t sum = 0;
  for (std::size_t i = 0; i < rom.size(); ++i) {
    if (i == 0x014E + offset || i == 0x014F + offset)
      continue;
    sum += rom[i];
  }
  return static_cast<std::uint16_t>(sum & 0xFFFF);
}

std::size_t rom_bytes_from_code(const byte_t code) {
  // Usually 32 KiB * (1 << value) for 00-08; plus some "unofficial" 52-54
  if (code <= 0x08) {
    return (32ull * 1024ull) << code;
  }
  switch (code) {
  case 0x52:
    return 72ull * 16ull * 1024ull; // 72 banks * 16 KiB
  case 0x53:
    return 80ull * 16ull * 1024ull;
  case 0x54:
    return 96ull * 16ull * 1024ull;
  default:
    return 0; // unknown/unhandled
  }
}

std::size_t ram_bytes_from_code(const byte_t code) {
  switch (code) {
  case 0x00:
    return 0;
  case 0x02:
    return 8ull * 1024ull;
  case 0x03:
    return 32ull * 1024ull;
  case 0x04:
    return 128ull * 1024ull;
  case 0x05:
    return 64ull * 1024ull;
  default:
    return 0; // treat unknown as 0
  }
}

// As per Pan Docs:
// - 0x80: Is used for games which support both CGB and monochrome systems
// - 0xC0: Is used for systems which only work on CGBs
// Any other values will result with monochrome backwards compatability
bool cgb_enabled(const byte_t cgb_flag) { return (cgb_flag == 0x80 || cgb_flag == 0xC0); }

std::string rom_header::manufacturer_code() const {
  // Manufacturer code uses bytes 013F-0142 on "newer" carts
  const byte_t *m = &title_area[0x013F - 0x0134]; // offset within title_area
  if (is_ascii_upper_alnum(m[0]) && is_ascii_upper_alnum(m[1]) && is_ascii_upper_alnum(m[2]) &&
      is_ascii_upper_alnum(m[3])) {
    return {reinterpret_cast<const char *>(m), 4};
  }
  return {};
}

std::string rom_header::title() const {
  // 0134-0143 is "Title", but parts are repurposed on later carts
  const bool cgb = (cgb_flag() & 0x80) != 0;

  // Heuristic:
  // - Non-CGB: take all 16 bytes (0134-0143) as title area
  // - CGB: if manufacturer code looks valid, title is 11 bytes (0134-013E);
  // else 15 bytes (0134-0142)
  constexpr std::size_t mfg_off = 0x013F - 0x0134;
  const bool has_mfg = cgb && !manufacturer_code().empty();

  const std::size_t title_len =
      (!cgb) ? 16 : (has_mfg ? mfg_off : (0x0143 - 0x0134)); // 15 bytes up to 0142

  return ascii_ztrim(title_area.data(), title_len);
}

static rom_header parse_header(const std::span<const byte_t> rom, const size_t offset) {
  if (rom.size() < kMinRomSize)
    throw std::runtime_error("The ROM file is too small (< 0x0150 bytes)");
  rom_header h{};

  std::copy_n(rom.data() + 0x0100 + offset, 4, h.entry_point.begin());
  std::copy_n(rom.data() + 0x0134 + offset, 16, h.title_area.begin());

  h.new_licensee_code[0] = rom[0x0144 + offset];
  h.new_licensee_code[1] = rom[0x0145 + offset];
  h.sgb_flag = rom[0x0146 + offset];
  h.cartridge_type = rom[0x0147 + offset];
  h.rom_size_code = rom[0x0148 + offset];
  h.ram_size_code = rom[0x0149 + offset];
  h.destination_code = rom[0x014A + offset];
  h.old_licensee_code = rom[0x014B + offset];
  h.mask_rom_version = rom[0x014C + offset];
  h.header_checksum = rom[0x014D + offset];

  h.global_checksum =
      static_cast<std::uint16_t>((static_cast<std::uint16_t>(rom[0x014E + offset]) << 8) |
                                 static_cast<std::uint16_t>(rom[0x014F + offset]));
  return h;
}

/* Weird carts detection */
// Here, we evaluate the possibility of MMM01 by peeking at the last 0x8000 bank of ROM and
// seeing if it contains a valid ROM header. If so, it is probably a multicart ROM.
static std::optional<std::tuple<rom_header, std::uint16_t, byte_t>>
maybe_mmm01(const std::span<const byte_t> rom) {
  if (rom.size() < 0x8000)
    return std::nullopt; // Multicarts always bank

  const auto last_bank_offset = rom.size() - 0x8000; // Check for valid header at end
  const auto header = parse_header(rom, last_bank_offset);

  const std::uint16_t global_checksum = compute_global_checksum(rom, last_bank_offset);
  const byte_t header_checksum = compute_header_checksum(rom, last_bank_offset);

  if (global_checksum != header.global_checksum || header_checksum != header.header_checksum)
    return std::nullopt;

  // TODO: We still miss one MMM01 Mani 4 in 1 cart, could potentially run a heuristic which
  // evaluates all banks and determines if its multicart based on the number of valid headers
  return std::make_tuple(header, global_checksum, header_checksum);
}

// Wisdom Tree detection because it's autistic :(
static bool maybe_wisdom_tree(const std::span<const byte_t> rom) {
  if (rom.size() <= 0x8000)
    return false;

  // scan the first chunk to catch the init code
  const std::size_t limit = std::min<std::size_t>(rom.size(), 0x40000); // 256 KiB

  std::uint32_t ea_total = 0;
  std::uint32_t ea_cart = 0;
  std::bitset<256> low_bytes{};

  for (std::size_t i = 0; i + 2 < limit; ++i) {
    if (rom[i] != 0xEA)
      continue; // LD (a16),A

    ++ea_total;
    const auto lo = static_cast<std::uint8_t>(rom[i + 1]);
    const auto hi = static_cast<std::uint8_t>(rom[i + 2]);
    // address = hi<<8 | lo
    if (hi < 0x80) { // 0000-7FFF (cartridge / mapper control area)
      ++ea_cart;
      low_bytes.set(lo);
    }
  }
  const std::size_t distinct_lo = low_bytes.count();

  // Heuristic thresholds:
  // - need some evidence of cart-area stores
  // - need multiple distinct low bytes (since WT bank is low byte of address)
  // - and a decent fraction of EA stores going to cart area
  if (ea_cart < 8)
    return false;
  if (distinct_lo < 6)
    return false;

  if (ea_total > 0) {
    const double frac = static_cast<double>(ea_cart) / static_cast<double>(ea_total);
    if (frac < 0.35)
      return false;
  }

  return true;
}

// Noooo, not you M161 too :(
static bool maybe_m161(const std::span<const byte_t> rom) {
  // M161 maps 32 KiB banks into 0000-7FFF, bank number is 3 bits (00-07)
  // So if the ROM is bigger than 32 KiB but doesn't have a whole number of 32
  // KiB banks, it's likely not M161 HOWEVER this is still not very foolproof.
  // Need more research...
  if (rom.size() <= 0x8000)
    return false;
  if (rom.size() % 0x8000 != 0)
    return false; // whole number of 32 KiB banks
  if (rom.size() > 0x8000 * 8)
    return false; // max 8 banks

  return true;
}

// MBC1M uhhhhgh
static bool maybe_mbc1m(const std::span<const byte_t> rom) {
  // Typical for 1 MiB MBC1 multicarts
  if (rom.size() < 1 * 1024 * 1024)
    return false;
  if (std::memcmp(&rom[0x104], &rom[0x40104], 0x30) == 0)
    return true;
  return false;
}

// They jumble the header on these cartridges, so we have to undo it. If we
// read off a valid checksum for the nintento logo, we ball c:
static bool maybe_sachen(const cart &c, const std::span<const byte_t> rom) {
  std::array<byte_t, 0x14F> unborked_header{};
  if (c.header_checksum_ok)
    return false; // Lmao

  // We attempt to first unscramble the header found on the cartridge itself
  for (std::size_t addr{0}; addr < unborked_header.size(); ++addr) {
    addr_t unjumbled_addr = sachen_jumble(addr);
    unborked_header.at(addr) = rom[unjumbled_addr];
  }

  // Checks if the unjumbled bytes are a valid nintendo logo (this feels wrong)
  return compute_header_checksum(unborked_header, 0) == unborked_header[0x014D];
}

SpecialMbc detect_special_mbc(const cart &c) {
  const auto cart_type = Debug::hex8(c.header.cartridge_type, true);

  // Sachen MMC2 jumbles the cartridge header to avoid getting sued so it is
  // guaranteed to fail the header checksum basically every single time
  if (!c.header_checksum_ok) {
    switch (c.header.cartridge_type) {
    case 0x00:
    case 0x01:
    case 0x31:
    case 0x40:
    case 0xC2:
    case 0xFA:
    case 0xFF:
      if (maybe_sachen(c, c.rom_span())) {
        Logger::push(LogLevel::Info, "ROM", "Mapper override",
                     IroGB::format("{} fails header checksum but meets criteria"
                                   "with Sachen MMC2 unscramble; forcing Sachen.",
                                   cart_type));
        return Sachen_t;
      }
    }
  }

  switch (c.header.cartridge_type) {
  case 0x00: { // ROM ONLY, but some WT/M161 carts lie about this, we
               // investigate further
    if (c.rom_size() <= 0x8000)
      return NotSpecial_t; // If strictly <= 32KiB, it's probably safe
    if (c.header.title() == "WISDOM TREE" || maybe_wisdom_tree(c.rom_span())) {
      Logger::push(LogLevel::Info, "ROM", "Mapper override",
                   IroGB::format("{} header type {} looks inconsistent with "
                                 "ROM size {} and appears to be WT; "
                                 "forcing Wisdom Tree mapper.",
                                 c.header.title(), cart_type, c.rom_span().size()));
      return WisdomTree_t;
    }
    if (maybe_m161(c.rom_span())) {
      Logger::push(LogLevel::Info, "ROM", "Mapper override",
                   IroGB::format("{} header type {} looks inconsistent with "
                                 "ROM size {} and appears to be M161; "
                                 "forcing M161 mapper.",
                                 c.header.title(), cart_type, c.rom_span().size()));
      return M161_t;
    }
  }

  case 0x01:
  case 0x02:
  case 0x03: // MBC1M possibility
    if (maybe_mbc1m(c.rom_span())) {
      Logger::push(LogLevel::Info, "ROM", "Mapper override",
                   IroGB::format("{} header type {} looks inconsistent with "
                                 "ROM size {} and appears to be MBC1M; "
                                 "forcing MBC1M mapper.",
                                 c.header.title(), cart_type, c.rom_span().size()));
      return MBC1M_t;
    }

  case 0x0F:
  case 0x10:
  case 0x11:
  case 0x12:
  case 0x13: // Special MBC3 carts that have 64 KiB RAM or 128 KiB ROM
    if (c.declared_ram_bytes > 32 * 1024 || c.declared_rom_bytes > 128 * 16 * 1024)
      return MBC30_t;

  case 0x1B:
    if (c.header.destination_code == 0xE1 || c.header.title() == "EMSMENU" ||
        c.header.title() == "GB16M")
      return EMS_t;

  case 0xC0:
    if (c.header.destination_code == 0xD1)
      return WisdomTree_t;

  default:
    return NotSpecial_t;
  }
}

static void apply_checksum_values(cart &c, std::uint16_t g_checksum, byte_t h_checksum) {
  c.global_checksum_ok = (g_checksum == c.header.global_checksum);
  c.header_checksum_ok = (h_checksum == c.header.header_checksum);
  c.computed_global_checksum = g_checksum;
  c.computed_header_checksum = h_checksum;
}

void calc_checksums_and_override(cart &c) {
  c.header = parse_header(c.rom, 0); // This may not be valid

  // We have not computed the checksums yet so have to do that now
  const std::uint16_t g_checksum = compute_global_checksum(c.rom, 0);
  const byte_t h_checksum = compute_header_checksum(c.rom, 0);
  apply_checksum_values(c, g_checksum, h_checksum);

  // If the rom header was not valid, we consider the possibility of MMM01 here
  // as all steps moving forward rely on a valid header extraction, which you can
  // get out of MMM01 carts.
  if (!c.header_checksum_ok || !c.global_checksum_ok) {
    if (const auto r = maybe_mmm01(c.rom); r.has_value()) {
      Logger::push(LogLevel::Info, "ROM", "Mapper override",
                   IroGB::format("Header checksums failed but meets criteria "
                                 "with MMM01 unscramble; forcing MMM01."));
      const auto &[header, g_checksum, h_checksum] = r.value();
      c.header = header;

      // Specifically, it is likely the global checksum that failed, but there is
      // likely still a valid logo in the very first bank.
      apply_checksum_values(c, g_checksum, h_checksum);
      c.special_mbc = MMM01_t; // Skips remaining heuristics
    }
  }

  // Pull the ROM and RAM sizes, at this point we just work with what we have
  c.declared_rom_bytes = rom_bytes_from_code(c.header.rom_size_code);
  c.declared_ram_bytes = ram_bytes_from_code(c.header.ram_size_code);

  // We neeed to make sure these actually align or it could throw off our banking
  // modulus calculation to avoid out of bounds memory accesses.
  if (c.declared_rom_bytes != 0 && c.rom.size() < c.declared_rom_bytes)
    c.rom.resize(c.declared_rom_bytes, 0);

  // If we haven't already detected a weird mapper type, try again using what ever
  // we have for the current header. If its still invalid, so be it.
  if (c.special_mbc == NotSpecial_t)
    c.special_mbc = detect_special_mbc(c);

  // If they dont match, its tough luck. We have to proceed regardless because some
  // developers did not follow the cartridge header like they were supposed to.
  if (!c.header_checksum_ok || !c.global_checksum_ok) {
    Logger::push(LogLevel::Warning, "ROM", "ROM validation failed",
                 "ROM checksum failed. Please make sure the ROM is not corrupted.");
  }
}

cart load_cart_raw(std::vector<byte_t> rom_bytes) {
  cart c{.special_mbc = NotSpecial_t};

#ifndef NO_CORE_FILESYSTEM
  c.file_path.clear();
#endif // NO_CORE_FILESYSTEM
  c.rom = std::move(rom_bytes);

  // We will load the cart even if it fails; the user should know what they are doing
  calc_checksums_and_override(c);
  return c;
}

void Cartridge::write(const addr_t addr, const byte_t v) {
  (void)addr;
  mbc_->write(addr, v);
  if (has_battery() && !mbc_->ram().empty())
    save_dirty_ = true;
}

bool Cartridge::consume_sram_save() noexcept {
  if (save_dirty_) {
    save_dirty_ = false;
    return true;
  }

  // SRAM is not dirty, so ignore
  return false;
}

#ifndef NO_CORE_FILESYSTEM
static std::optional<std::vector<byte_t>> read_all_bytes(const std::filesystem::path &p) {
  std::ifstream f(p, std::ios::binary | std::ios::ate);
  if (!f) {
    Logger::push(LogLevel::Error, "ROM", "Failed to open ROM file",
                 "Failed to open the ROM file: " + p.string() +
                     ". Please make sure the file exists and is accessible.");
    return std::nullopt;
  }

  const std::streamsize size = f.tellg();
  if (size < 0) {
    Logger::push(LogLevel::Error, "ROM", "Failed read ROM size",
                 "Failed read the size of the ROM file : " + p.string() +
                     ". Please make sure the file exists and is accessible.");
    return std::nullopt;
  }

  std::vector<byte_t> buf(static_cast<std::size_t>(size));
  f.seekg(0, std::ios::beg);
  if (!f.read(reinterpret_cast<char *>(buf.data()), size)) {
    Logger::push(LogLevel::Error, "ROM", "Failed to load ROM content",
                 "Failed to read the content of the ROM file: " + p.string() +
                     ". Please make sure the file exists and is accessible.");
    return std::nullopt;
  }

  return buf;
}

cart load_cart_fs(const std::filesystem::path &rom_path) {
  cart c{.file_path = rom_path, .special_mbc = NotSpecial_t};
  if (const auto rom = read_all_bytes(rom_path); rom != std::nullopt)
    c.rom = rom.value();
  else {
    throw std::runtime_error{"Cannot read cartridge content"};
  }

  // We will load the cart even if it fails; the user should know what they are doing
  calc_checksums_and_override(c);
  return c;
}

bool Cartridge::load_save_file(const std::filesystem::path &save_path) {
  if (!has_battery() || save_path.empty())
    return false;

  auto ram_view = ram();
  if (ram_view.empty())
    return false;

  std::ifstream f(save_path, std::ios::binary | std::ios::ate);
  if (!f)
    return false;

  const std::streamsize size = f.tellg();
  if (size <= 0)
    return false;

  std::vector<byte_t> buf(static_cast<std::size_t>(size));
  f.seekg(0, std::ios::beg);
  if (!f.read(reinterpret_cast<char *>(buf.data()), size))
    return false;

  const std::size_t copy_bytes = std::min(ram_view.size(), buf.size());
  std::copy_n(buf.data(), copy_bytes, ram_view.begin());
  save_dirty_ = false;
  return true;
}

bool Cartridge::write_save_file(const std::filesystem::path &save_path) const {
  if (!has_battery() || save_path.empty())
    return false;

  const auto ram_view = ram();
  if (ram_view.empty())
    return false;

  const auto parent = save_path.parent_path();
  if (!parent.empty()) {
    std::error_code ec;
    std::filesystem::create_directories(parent, ec);
  }

  std::ofstream f(save_path, std::ios::binary | std::ios::trunc);
  if (!f)
    return false;

  f.write(reinterpret_cast<const char *>(ram_view.data()),
          static_cast<std::streamsize>(ram_view.size()));
  return static_cast<bool>(f);
}
#endif // NO_CORE_FILESYSTEM
