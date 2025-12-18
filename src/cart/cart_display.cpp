#include "cart/cart_display.hpp"

#include <array>
#include <cctype>
#include <iomanip>
#include <ostream>
#include <sstream>
#include <string_view>

static std::string hex8(byte_t v) {
  std::ostringstream oss;
  oss << "0x" << std::hex << std::uppercase << std::setw(2) << std::setfill('0')
      << static_cast<int>(v);
  return oss.str();
}

static std::string hex16(std::uint16_t v) {
  std::ostringstream oss;
  oss << "0x" << std::hex << std::uppercase << std::setw(4) << std::setfill('0')
      << v;
  return oss.str();
}

static std::string bytes_hex(std::span<const byte_t> s) {
  std::ostringstream oss;
  oss << std::hex << std::uppercase << std::setfill('0');
  for (std::size_t i = 0; i < s.size(); ++i) {
    if (i)
      oss << ' ';
    oss << std::setw(2) << static_cast<int>(s[i]);
  }
  return oss.str();
}

static std::string two_char_code(byte_t a, byte_t b) {
  auto printable = [](byte_t x) {
    return std::isprint(static_cast<unsigned char>(x)) != 0;
  };
  if (printable(a) && printable(b)) {
    std::string s;
    s.push_back(static_cast<char>(a));
    s.push_back(static_cast<char>(b));
    return s;
  }
  // Fall back to hex bytes if the ROM is weird/corrupt
  return hex8(a) + std::string(" ") + hex8(b);
}

template <class K, class V, std::size_t N>
static std::string
lookup_or_unknown(const std::array<std::pair<K, V>, N> &table, const K &key) {
  for (auto &[k, v] : table) {
    if (k == key)
      return std::string(v);
  }
  return "Unknown";
}

// --- Tables  ---
// Cartridge type codes (0147)
static constexpr std::array<std::pair<byte_t, std::string_view>, 28> kCartType =
    {{
        {0x00, "ROM ONLY"},
        {0x01, "MBC1"},
        {0x02, "MBC1+RAM"},
        {0x03, "MBC1+RAM+BATTERY"},
        {0x05, "MBC2"},
        {0x06, "MBC2+BATTERY"},
        {0x08, "ROM+RAM"},
        {0x09, "ROM+RAM+BATTERY"},
        {0x0B, "MMM01"},
        {0x0C, "MMM01+RAM"},
        {0x0D, "MMM01+RAM+BATTERY"},
        {0x0F, "MBC3+TIMER+BATTERY"},
        {0x10, "MBC3+TIMER+RAM+BATTERY"},
        {0x11, "MBC3"},
        {0x12, "MBC3+RAM"},
        {0x13, "MBC3+RAM+BATTERY"},
        {0x19, "MBC5"},
        {0x1A, "MBC5+RAM"},
        {0x1B, "MBC5+RAM+BATTERY"},
        {0x1C, "MBC5+RUMBLE"},
        {0x1D, "MBC5+RUMBLE+RAM"},
        {0x1E, "MBC5+RUMBLE+RAM+BATTERY"},
        {0x20, "MBC6"},
        {0x22, "MBC7+SENSOR+RUMBLE+RAM+BATTERY"},
        {0xFC, "POCKET CAMERA"},
        {0xFD, "BANDAI TAMA5"},
        {0xFE, "HuC3"},
        {0xFF, "HuC1+RAM+BATTERY"},
    }};

// Destination code (014A)
std::string destination_name(byte_t code) {
  switch (code) {
  case 0x00:
    return "Japan";
  case 0x01:
    return "Overseas only";
  default:
    return "Unknown";
  }
}

// CGB flag (0143)
std::string cgb_flag_desc(byte_t f) {
  switch (f) {
  case 0x80:
    return "CGB supported (DMG compatible)";
  case 0xC0:
    return "CGB only";
  default:
    return "Not marked / older header";
  }
}

// SGB flag (0146)
std::string sgb_flag_desc(byte_t f) {
  if (f == 0x03)
    return "SGB functions supported";
  return "No SGB functions";
}

// Old licensee codes (014B)
static constexpr std::array<std::pair<byte_t, std::string_view>, 255>
    kOldLicensee = {{
        {0x00, "None"},
        {0x01, "Nintendo"},
        {0x08, "Capcom"},
        {0x09, "HOT-B"},
        {0x0A, "Jaleco"},
        {0x0B, "Coconuts Japan"},
        {0x0C, "Elite Systems"},
        {0x13, "EA (Electronic Arts)"},
        {0x18, "Hudson Soft"},
        {0x19, "ITC Entertainment"},
        {0x1A, "Yanoman"},
        {0x1D, "Japan Clary"},
        {0x1F, "Virgin Games Ltd."},
        {0x24, "PCM Complete"},
        {0x25, "San-X"},
        {0x28, "Kemco"},
        {0x29, "SETA Corporation"},
        {0x30, "Infogrames"},
        {0x31, "Nintendo"},
        {0x32, "Bandai"},
        {0x33, "Use New licensee code"},
        {0x34, "Konami"},
        {0x35, "HectorSoft"},
        {0x38, "Capcom"},
        {0x39, "Banpresto"},
        {0x3C, "Entertainment Interactive"},
        {0x3E, "Gremlin"},
        {0x41, "Ubi Soft"},
        {0x42, "Atlus"},
        {0x44, "Malibu Interactive"},
        {0x46, "Angel"},
        {0x47, "Spectrum HoloByte"},
        {0x49, "Irem"},
        {0x4A, "Virgin Games Ltd."},
        {0x4D, "Malibu Interactive"},
        {0x4F, "U.S. Gold"},
        {0x50, "Absolute"},
        {0x51, "Acclaim Entertainment"},
        {0x52, "Activision"},
        {0x53, "Sammy USA Corporation"},
        {0x54, "GameTek"},
        {0x55, "Park Place"},
        {0x56, "LJN"},
        {0x57, "Matchbox"},
        {0x59, "Milton Bradley Company"},
        {0x5A, "Mindscape"},
        {0x5B, "Romstar"},
        {0x5C, "Naxat Soft"},
        {0x5D, "Tradewest"},
        {0x60, "Titus Interactive"},
        {0x61, "Virgin Games Ltd."},
        {0x67, "Ocean Software"},
        {0x69, "EA (Electronic Arts)"},
        {0x6E, "Elite Systems"},
        {0x6F, "Electro Brain"},
        {0x70, "Infogrames"},
        {0x71, "Interplay Entertainment"},
        {0x72, "Broderbund"},
        {0x73, "Sculptured Software"},
        {0x75, "The Sales Curve Limited"},
        {0x78, "THQ"},
        {0x79, "Accolade"},
        {0x7A, "Triffix Entertainment"},
        {0x7C, "MicroProse"},
        {0x7F, "Kemco"},
        {0x80, "Misawa Entertainment"},
        {0x83, "LOZC G."},
        {0x86, "Tokuma Shoten"},
        {0x8B, "Bullet-Proof Software"},
        {0x8C, "Vic Tokai Corp."},
        {0x8E, "Ape Inc."},
        {0x8F, "I’Max"},
        {0x91, "Chunsoft Co."},
        {0x92, "Video System"},
        {0x93, "Tsubaraya Productions"},
        {0x95, "Varie"},
        {0x96, "Yonezawa / S’Pal"},
        {0x97, "Kemco"},
        {0x99, "Arc"},
        {0x9A, "Nihon Bussan"},
        {0x9B, "Tecmo"},
        {0x9C, "Imagineer"},
        {0x9D, "Banpresto"},
        {0x9F, "Nova"},
        {0xA1, "Hori Electric"},
        {0xA2, "Bandai"},
        {0xA4, "Konami"},
        {0xA6, "Kawada"},
        {0xA7, "Takara"},
        {0xA9, "Technos Japan"},
        {0xAA, "Broderbund"},
        {0xAC, "Toei Animation"},
        {0xAD, "Toho"},
        {0xAF, "Namco"},
        {0xB0, "Acclaim Entertainment"},
        {0xB1, "ASCII Corporation or Nexsoft"},
        {0xB2, "Bandai"},
        {0xB4, "Square"},
        {0xB6, "HAL Laboratory"},
        {0xB7, "SNK"},
        {0xB9, "Pony Canyon"},
        {0xBA, "Culture Brain"},
        {0xBB, "Sunsoft"},
        {0xBD, "Sony Imagesoft"},
        {0xBF, "Sammy Corporation"},
        {0xC0, "Taito"},
        {0xC2, "Kemco"},
        {0xC3, "Square"},
        {0xC4, "Tokuma Shoten"},
        {0xC5, "Data East"},
        {0xC6, "Tonkin House"},
        {0xC8, "Koei"},
        {0xC9, "UFL"},
        {0xCA, "Ultra Games"},
        {0xCB, "VAP, Inc."},
        {0xCC, "Use Corporation"},
        {0xCD, "Meldac"},
        {0xCE, "Pony Canyon"},
        {0xCF, "Angel"},
        {0xD0, "Taito"},
        {0xD1, "SOFEL (Software Engineering Lab)"},
        {0xD2, "Quest"},
        {0xD3, "Sigma Enterprises"},
        {0xD4, "ASK Kodansha Co."},
        {0xD6, "Naxat Soft"},
        {0xD7, "Copya System"},
        {0xD9, "Banpresto"},
        {0xDA, "Tomy"},
        {0xDB, "LJN"},
        {0xDD, "Nippon Computer Systems"},
        {0xDE, "Human Ent."},
        {0xDF, "Altron"},
        {0xE0, "Jaleco"},
        {0xE1, "Towa Chiki"},
        {0xE2, "Yutaka"},
        {0xE3, "Varie"},
        {0xE5, "Epoch"},
        {0xE7, "Athena"},
        {0xE8, "Asmik Ace Entertainment"},
        {0xE9, "Natsume"},
        {0xEA, "King Records"},
        {0xEB, "Atlus"},
        {0xEC, "Epic / Sony Records"},
        {0xEE, "IGS"},
        {0xF0, "A Wave"},
        {0xF3, "Extreme Entertainment"},
        {0xFF, "LJN"},
    }};

// New licensee code samples (0144–0145)
static constexpr std::array<std::pair<std::string_view, std::string_view>, 255>
    kNewLicensee = {{
        {"00", "None"},
        {"01", "Nintendo R&D1"},
        {"08", "Capcom"},
        {"13", "EA (Electronic Arts)"},
        {"18", "Hudson Soft"},
        {"19", "B-AI"},
        {"20", "KSS"},
        {"22", "Planning Office WADA"},
        {"24", "PCM Complete"},
        {"25", "San-X"},
        {"28", "Kemco"},
        {"29", "SETA Corporation"},
        {"30", "Viacom"},
        {"31", "Nintendo"},
        {"32", "Bandai"},
        {"33", "Ocean Software / Acclaim Entertainment"},
        {"34", "Konami"},
        {"35", "HectorSoft"},
        {"37", "Taito"},
        {"38", "Hudson Soft"},
        {"39", "Banpresto"},
        {"41", "Ubi Soft"},
        {"42", "Atlus"},
        {"44", "Malibu Interactive"},
        {"46", "Angel"},
        {"47", "Bullet-Proof Software"},
        {"49", "Irem"},
        {"50", "Absolute"},
        {"51", "Acclaim Entertainment"},
        {"52", "Activision"},
        {"53", "Sammy USA Corporation"},
        {"54", "Konami"},
        {"55", "Hi Tech Expressions"},
        {"56", "LJN"},
        {"57", "Matchbox"},
        {"58", "Mattel"},
        {"59", "Milton Bradley Company"},
        {"60", "Titus Interactive"},
        {"61", "Virgin Games Ltd."},
        {"64", "Lucasfilm Games"},
        {"67", "Ocean Software"},
        {"69", "EA (Electronic Arts)"},
        {"70", "Infogrames"},
        {"71", "Interplay Entertainment"},
        {"72", "Broderbund"},
        {"73", "Sculptured Software"},
        {"75", "The Sales Curve Limited"},
        {"78", "THQ"},
        {"79", "Accolade"},
        {"80", "Misawa Entertainment"},
        {"83", "LOZC G."},
        {"86", "Tokuma Shoten"},
        {"87", "Tsukuda Original"},
        {"91", "Chunsoft Co."},
        {"92", "Video System"},
        {"93", "Ocean Software / Acclaim Entertainment"},
        {"95", "Varie"},
        {"96", "Yonezawa / S’Pal"},
        {"97", "Kaneko"},
        {"99", "Pack-In-Video"},
        {"9H", "Bottom Up"},
        {"A4", "Konami (Yu-Gi-Oh!)"},
        {"BL", "MTO"},
        {"DK", "Kodansha"},
    }};

std::string cartridge_type_name(byte_t code) {
  return lookup_or_unknown(kCartType, code);
}

std::string old_licensee_name(byte_t old_code) {
  return lookup_or_unknown(kOldLicensee, old_code);
}

std::string new_licensee_name(const std::string &two_chars) {
  // table keyed by two-char strings
  for (auto &[k, v] : kNewLicensee) {
    if (two_chars == k)
      return std::string(v);
  }
  return "Unknown";
}

// ROM size tag (0148)
std::string rom_size_pretty(byte_t code) {
  if (code <= 0x08) {
    const std::uint64_t bytes = (32ull * 1024ull) << code;
    const std::uint64_t banks = bytes / (16ull * 1024ull);
    std::ostringstream oss;
    oss << (bytes / 1024ull) << " KiB (" << banks << " ROM banks)";
    return oss.str();
  }
  switch (code) {
  case 0x52:
    return "1.1 MiB (72 ROM banks)";
  case 0x53:
    return "1.2 MiB (80 ROM banks)";
  case 0x54:
    return "1.5 MiB (96 ROM banks)";
  default:
    return "Unknown";
  }
}

// RAM size tag (0149)
std::string ram_size_pretty(byte_t code) {
  switch (code) {
  case 0x00:
    return "0 (No RAM)";
  case 0x01:
    return "Unused tag";
  case 0x02:
    return "8 KiB (1 bank)";
  case 0x03:
    return "32 KiB (4 banks of 8 KiB)";
  case 0x04:
    return "128 KiB (16 banks of 8 KiB)";
  case 0x05:
    return "64 KiB (8 banks of 8 KiB)";
  default:
    return "Unknown";
  }
}

// Checksums (duplicated here so the report can show expected vs computed)
static byte_t compute_header_checksum(std::span<const byte_t> rom) {
  byte_t checksum = 0;
  for (std::uint16_t addr = 0x0134; addr <= 0x014C; ++addr) {
    checksum = static_cast<byte_t>(checksum - rom[addr] - 1);
  }
  return checksum;
}

static std::uint16_t compute_global_checksum(std::span<const byte_t> rom) {
  std::uint32_t sum = 0;
  for (std::size_t i = 0; i < rom.size(); ++i) {
    if (i == 0x014E || i == 0x014F)
      continue;
    sum += rom[i];
  }
  return static_cast<std::uint16_t>(sum & 0xFFFF);
}

std::string describe_cart(const cart &c) {
  const auto &h = c.header;

  const std::string new_code =
      two_char_code(h.new_licensee_code[0], h.new_licensee_code[1]);

  const byte_t hdr_chk = compute_header_checksum(c.rom);
  const std::uint16_t glob_chk = compute_global_checksum(c.rom);

  std::ostringstream os;

  os << "=== Game Boy Cartridge Report ===\n";
  os << "File: " << c.file_path.string() << "\n";
  os << "ROM bytes: " << c.rom.size() << "\n\n";

  os << "[Header]\n";
  os << "Title: " << h.title() << "\n";
  {
    auto m = h.manufacturer_code();
    os << "Manufacturer code: " << (m.empty() ? "(none/unknown)" : m) << "\n";
  }
  os << "CGB flag (0143): " << hex8(h.cgb_flag()) << " -> "
     << cgb_flag_desc(h.cgb_flag()) << "\n";
  os << "SGB flag (0146): " << hex8(h.sgb_flag) << " -> "
     << sgb_flag_desc(h.sgb_flag) << "\n";

  os << "Cartridge type (0147): " << hex8(h.cartridge_type) << " -> "
     << cartridge_type_name(h.cartridge_type) << "\n";

  os << "ROM size tag (0148): " << hex8(h.rom_size_code) << " -> "
     << rom_size_pretty(h.rom_size_code) << "\n";

  os << "RAM size tag (0149): " << hex8(h.ram_size_code) << " -> "
     << ram_size_pretty(h.ram_size_code) << "\n";

  os << "Destination (014A): " << hex8(h.destination_code) << " -> "
     << destination_name(h.destination_code) << "\n";

  os << "Old licensee (014B): " << hex8(h.old_licensee_code) << " -> "
     << old_licensee_name(h.old_licensee_code) << "\n";

  os << "New licensee (0144-0145): '" << new_code << "' -> "
     << new_licensee_name(new_code) << "\n";

  os << "Mask ROM version (014C): " << hex8(h.mask_rom_version) << "\n\n";

  os << "[Boot/Integrity]\n";
  os << "Nintendo logo check (full): " << (c.logo_ok ? "OK" : "FAIL") << "\n";
  os << "Entry point (0100-0103): " << bytes_hex(h.entry_point) << "\n";

  os << "Header checksum (014D): expected " << hex8(h.header_checksum)
     << ", computed " << hex8(hdr_chk) << " -> "
     << ((hdr_chk == h.header_checksum) ? "OK" : "FAIL") << "\n";

  os << "Global checksum (014E-014F): expected " << hex16(h.global_checksum)
     << ", computed " << hex16(glob_chk) << " -> "
     << ((glob_chk == h.global_checksum) ? "OK" : "FAIL") << "\n";

  return os.str();
}

void print_cart(std::ostream &os, const cart &c) { os << describe_cart(c); }
