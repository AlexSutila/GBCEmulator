#include "cart/cart.hpp"

#include <algorithm>
#include <fstream>
#include <numeric>
#include <stdexcept>


static std::vector<byte_t> read_all_bytes(const fs::path& p) {
    std::ifstream f(p, std::ios::binary | std::ios::ate);
    if (!f) throw std::runtime_error("load_cart: failed to open ROM file: " + p.string());

    const std::streamsize size = f.tellg();
    if (size < 0) throw std::runtime_error("load_cart: size read failed: " + p.string());

    std::vector<byte_t> buf(static_cast<std::size_t>(size));
    f.seekg(0, std::ios::beg);
    if (!f.read(reinterpret_cast<char*>(buf.data()), size)) {
        throw std::runtime_error("load_cart: failed to read ROM content: " + p.string());
    }
    return buf;
}

static bool is_ascii_upper_alnum(byte_t b) {
    const unsigned c = b;
    return (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
}

static std::string ascii_ztrim(const byte_t* p, std::size_t n) {
    std::size_t len = 0;
    while (len < n && p[len] != 0x00) ++len;
    return std::string(reinterpret_cast<const char*>(p), len);
}

// Nintendo logo bytes (0104-0133).
static constexpr std::array<byte_t, 0x30> kNintendoLogo = {
    0xCE,0xED,0x66,0x66,0xCC,0x0D,0x00,0x0B,0x03,0x73,0x00,0x83,0x00,0x0C,0x00,0x0D,
    0x00,0x08,0x11,0x1F,0x88,0x89,0x00,0x0E,0xDC,0xCC,0x6E,0xE6,0xDD,0xDD,0xD9,0x99,
    0xBB,0xBB,0x67,0x63,0x6E,0x0E,0xEC,0xCC,0xDD,0xDC,0x99,0x9F,0xBB,0xB9,0x33,0x3E
};

// GB: checks all 0x30 bytes
// CGB+: checks first 0x18 bytes only
static bool check_logo(std::span<const byte_t> rom) {
    if (rom.size() < 0x0134) return false;
    return std::equal(kNintendoLogo.begin(), kNintendoLogo.end(), rom.begin() + 0x0104);
}

// Header checksum algorithm from boot ROM
static byte_t compute_header_checksum(std::span<const byte_t> rom) {
    byte_t checksum = 0;
    for (std::uint16_t addr = 0x0134; addr <= 0x014C; ++addr) {
        checksum = static_cast<byte_t>(checksum - rom[addr] - 1);
    }
    return checksum;
}

// Global checksum is a simple 16-bit sum excluding bytes 014E-014F
static std::uint16_t compute_global_checksum(std::span<const byte_t> rom) {
    std::uint32_t sum = 0;
    for (std::size_t i = 0; i < rom.size(); ++i) {
        if (i == 0x014E || i == 0x014F) continue;
        sum += rom[i];
    }
    return static_cast<std::uint16_t>(sum & 0xFFFF);
}

std::size_t rom_bytes_from_code(byte_t code) {
    // Usually 32 KiB * (1 << value) for 00-08; plus some “unofficial” 52-54
    if (code <= 0x08) {
        return (32ull * 1024ull) << code;
    }
    switch (code) {
        case 0x52: return 72ull * 16ull * 1024ull;  // 72 banks * 16 KiB
        case 0x53: return 80ull * 16ull * 1024ull;
        case 0x54: return 96ull * 16ull * 1024ull;
        default:   return 0; // unknown/unhandled
    }
}

std::size_t ram_bytes_from_code(byte_t code) {
    switch (code) {
        case 0x00: return 0;
        case 0x02: return 8ull  * 1024ull;
        case 0x03: return 32ull * 1024ull;
        case 0x04: return 128ull * 1024ull;
        case 0x05: return 64ull * 1024ull;
        default:   return 0; // treat unknown as 0
    }
}

std::string rom_header::manufacturer_code() const {
    // Manufacturer code uses bytes 013F-0142 on "newer" carts
    const byte_t* m = &title_area[0x013F - 0x0134]; // offset within title_area
    if (is_ascii_upper_alnum(m[0]) && is_ascii_upper_alnum(m[1]) &&
        is_ascii_upper_alnum(m[2]) && is_ascii_upper_alnum(m[3])) {
        return std::string(reinterpret_cast<const char*>(m), 4);
    }
    return {};
}

std::string rom_header::title() const {
    // 0134-0143 is "Title", but parts are repurposed on later carts
    const bool cgb = (cgb_flag() & 0x80) != 0;

    // Heuristic:
    // - Non-CGB: take all 16 bytes (0134-0143) as title area
    // - CGB: if manufacturer code looks valid, title is 11 bytes (0134-013E); else 15 bytes (0134-0142)
    const std::size_t mfg_off = 0x013F - 0x0134;
    const bool has_mfg = cgb && !manufacturer_code().empty();

    const std::size_t title_len =
        (!cgb) ? 16 :
        (has_mfg ? mfg_off : (0x0143 - 0x0134)); // 15 bytes up to 0142

    return ascii_ztrim(title_area.data(), title_len);
}

static rom_header parse_header(std::span<const byte_t> rom) {
    if (rom.size() < kMinRomSize) {
        throw std::runtime_error("parse_header: ROM too small (< 0x0150 bytes)");
    }

    rom_header h{};

    std::copy_n(rom.data() + 0x0100, 4,    h.entry_point.begin());
    std::copy_n(rom.data() + 0x0104, 0x30, h.nintendo_logo.begin());
    std::copy_n(rom.data() + 0x0134, 16,   h.title_area.begin());

    h.new_licensee_code[0] = rom[0x0144];
    h.new_licensee_code[1] = rom[0x0145];
    h.sgb_flag          = rom[0x0146];
    h.cartridge_type    = rom[0x0147];
    h.rom_size_code     = rom[0x0148];
    h.ram_size_code     = rom[0x0149];
    h.destination_code  = rom[0x014A];
    h.old_licensee_code = rom[0x014B];
    h.mask_rom_version  = rom[0x014C];
    h.header_checksum   = rom[0x014D];

    h.global_checksum = static_cast<std::uint16_t>(
        (static_cast<std::uint16_t>(rom[0x014E]) << 8) |
         static_cast<std::uint16_t>(rom[0x014F])
    );

    return h;
}

cart load_cart(const fs::path& rom_path) {
    cart c{};
    c.file_path = rom_path;
    c.rom = read_all_bytes(rom_path);

    if (c.rom.size() < kMinRomSize) {
        throw std::runtime_error("load_cart: ROM is too small to contain a valid header");
    }

    c.header = parse_header(c.rom);

    c.declared_rom_bytes = rom_bytes_from_code(c.header.rom_size_code);
    c.declared_ram_bytes = ram_bytes_from_code(c.header.ram_size_code);

    c.logo_ok = check_logo(c.rom);

    const byte_t computed_hchk = compute_header_checksum(c.rom);
    c.header_checksum_ok = (computed_hchk == c.header.header_checksum);

    const std::uint16_t computed_gchk = compute_global_checksum(c.rom);
    c.global_checksum_ok = (computed_gchk == c.header.global_checksum);

    // Optional: if declared size is known, ensure file is at least that big
    if (c.declared_rom_bytes != 0 && c.rom.size() < c.declared_rom_bytes) {
        throw std::runtime_error("load_cart: ROM file smaller than header-declared ROM size");
    }

    return c;
}