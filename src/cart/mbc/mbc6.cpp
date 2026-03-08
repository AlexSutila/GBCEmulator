#include "cart/cart.hpp"
#include "cart/mbc.hpp"
#include "cart/mbc_creator.hpp"

#include <algorithm>
#include <array>
#include <bitset>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

// ---------------------------
// MBC6 (ROM/Flash + RAM + Flash commands)
// ---------------------------
// Pan Docs: https://gbdev.io/pandocs/MBC6.html
//
// Memory:
// Fixed ROM bank 00 (16 KiB)             (0000-3FFF)
// ROM/Flash bank A (8 KiB banks 00-7F)   (4000-5FFF)
// ROM/Flash bank B (8 KiB banks 00-7F)   (6000-7FFF)
// RAM bank A (4 KiB banks 00-07)         (A000-AFFF)
// RAM bank B (4 KiB banks 00-07)         (B000-BFFF)
//
// Registers (write only):
// RAM enable (0A=enable, 00=disable)     (0000-3FFF)
// RAM bank A number                      (0400-07FF)
// RAM bank B number                      (0800-0BFF)
// Flash enable (bit0)                    (0C00-0FFF)
// Flash write enable                     (1000)
// (/WP) (bit0) (affects sector0 + hidden region)
// ROM/Flash bank A number (00-7F)        (2000-27FF)
// ROM/Flash select A (00=ROM, 08=Flash)  (2800-2FFF)
// ROM/Flash bank B number (00-7F)        (3000-37FF)
// ROM/Flash select B (00=ROM, 08=Flash)  (3800-3FFF)

class Mbc6 final : public Mbc {
public:
  Mbc6(const std::span<const byte_t> rom,
       const std::size_t ram_bytes,
       const bool battery)
      : rom_(rom),
        ram_(ram_bytes),
        persist_(battery),
        flash_(kFlashSize, 0xFF) {
    hidden_.fill(0xFF);
  }

  byte_t read(const addr_t addr) override {
    // 0000-3FFF fixed ROM
    if (addr <= 0x3FFF) {
      return rom_fixed(addr);
    }

    // 4000-5FFF window A
    if (addr >= 0x4000 && addr <= 0x5FFF) {
      const auto off = static_cast<std::size_t>(addr - 0x4000);
      return read_window(Window::A, off);
    }

    // 6000-7FFF window B
    if (addr >= 0x6000 && addr <= 0x7FFF) {
      const auto off = static_cast<std::size_t>(addr - 0x6000);
      return read_window(Window::B, off);
    }

    // A000-AFFF RAM bank A
    if (addr >= 0xA000 && addr <= 0xAFFF) {
      if (!ram_enabled_ || ram_.empty()) return open_bus();
      return ram_at_4k(ram_bank_a_, static_cast<std::size_t>(addr - 0xA000));
    }

    // B000-BFFF RAM bank B
    if (addr >= 0xB000 && addr <= 0xBFFF) {
      if (!ram_enabled_ || ram_.empty()) return open_bus();
      return ram_at_4k(ram_bank_b_, static_cast<std::size_t>(addr - 0xB000));
    }

    return open_bus();
  }

  void write(const addr_t addr, const byte_t val) override {
    if (addr <= 0x3FFF) {
      write_regs(addr, val);
      return;
    }

    // Flash writes happen through window A/B when flash is mapped + enabled.
    if (addr >= 0x4000 && addr <= 0x5FFF) {
      const auto off = static_cast<std::size_t>(addr - 0x4000);
      write_window(Window::A, addr, off, val);
      return;
    }

    if (addr >= 0x6000 && addr <= 0x7FFF) {
      const auto off = static_cast<std::size_t>(addr - 0x6000);
      write_window(Window::B, addr, off, val);
      return;
    }

    // External RAM writes
    if (addr >= 0xA000 && addr <= 0xAFFF) {
      if (!ram_enabled_ || ram_.empty()) return;
      ram_write_4k(ram_bank_a_, static_cast<std::size_t>(addr - 0xA000), val);
      return;
    }
    if (addr >= 0xB000 && addr <= 0xBFFF) {
      if (!ram_enabled_ || ram_.empty()) return;
      ram_write_4k(ram_bank_b_, static_cast<std::size_t>(addr - 0xB000), val);
      return;
    }
  }

  // MBC6 has non-volatile flash, which is technically persistent and can act as battery-backed flash
  // More research needed
  [[nodiscard]] bool has_battery() const noexcept override { return persist_; }

  [[nodiscard]] std::span<const byte_t> ram() const noexcept override { return ram_; }
  std::span<byte_t> ram() noexcept override { return ram_; }

private:
  static constexpr std::size_t kRomBank8K  = 0x2000;
  static constexpr std::size_t kRamBank4K  = 0x1000;
  static constexpr std::size_t kFlashSize  = 0x100000; // 1 MiB
  static constexpr std::size_t kFlashBanks = kFlashSize / kRomBank8K; // 128 banks of 8 KiB
  static constexpr std::size_t kSectorSize = 0x20000;  // 128 KiB
  static constexpr std::size_t kHiddenSize = 256;
  static constexpr std::size_t kProgChunk  = 0x80;  // 128 bytes
  static constexpr std::size_t kProgMaskBytes = kProgChunk / 8;

  enum class Window : std::uint8_t { A, B };

  // Flash command modes
  enum class FlashMode : std::uint8_t {
    ReadArray,
    IdMode,
    Program,
    HiddenRead,
    HiddenProgram,
    Status // return status bytes until F0
  };

  enum class Seq : std::uint8_t {
    Idle,
    GotAA,
    Got55,

    // After AA 55 80
    Erase_80,
    Erase_AA,
    Erase_55,

    // After AA 55 60
    Cmd60_AA,
    Cmd60_55,

    // After AA 55 77
    Hidden77_AA,
    Hidden77_55,
    Hidden77_Final
    };

    struct ProgramLatch {
      bool active{false};
      bool filled{false};          // all 128 bytes received
      std::size_t base{0};         // chip address aligned to 128 bytes
      std::array<byte_t, kProgChunk> buf{};
      std::bitset<kProgChunk> written{};

      ProgramLatch() { reset(); }

      void reset() {
        active = false;
        filled = false;
        base = 0;
        buf.fill(0xFF);
        written.reset();
      }
  };

  std::span<const byte_t> rom_;
  std::vector<byte_t> ram_;

  // Treat flash as persistent storage
  bool persist_{true};

  // MBC6 registers/state
  bool  ram_enabled_{false};
  byte_t ram_bank_a_{0};
  byte_t ram_bank_b_{0};

  bool  flash_ce_{false};   // flash enable (bit0)
  bool  flash_wp_{false};   // flash write enable (/WP) (bit0), protects sector0+hidden when 0
  bool  a_flash_{false};    // window A selects flash (true) or ROM (false)
  bool  b_flash_{false};    // window B selects flash (true) or ROM (false)
  byte_t a_bank_{0};        // 00-7F (8 KiB banks)
  byte_t b_bank_{0};        // 00-7F

  // Flash storage and mode
  std::vector<byte_t> flash_;
  std::array<byte_t, kHiddenSize> hidden_{};

  FlashMode flash_mode_{FlashMode::ReadArray};
  Seq seq_{Seq::Idle};

  ProgramLatch prog_{};        // main flash 128-byte program buffer
  ProgramLatch hidden_prog_{}; // hidden region 128-byte program buffer

  bool sector0_protected_{false};

  [[nodiscard]] static byte_t flash_mode_to_raw_(const FlashMode mode) {
    return static_cast<byte_t>(mode);
  }

  [[nodiscard]] static FlashMode flash_mode_from_raw_(const byte_t raw) {
    switch (raw) {
    case static_cast<byte_t>(FlashMode::IdMode):
      return FlashMode::IdMode;
    case static_cast<byte_t>(FlashMode::Program):
      return FlashMode::Program;
    case static_cast<byte_t>(FlashMode::HiddenRead):
      return FlashMode::HiddenRead;
    case static_cast<byte_t>(FlashMode::HiddenProgram):
      return FlashMode::HiddenProgram;
    case static_cast<byte_t>(FlashMode::Status):
      return FlashMode::Status;
    default:
      return FlashMode::ReadArray;
    }
  }

  [[nodiscard]] static byte_t seq_to_raw_(const Seq seq) {
    return static_cast<byte_t>(seq);
  }

  [[nodiscard]] static Seq seq_from_raw_(const byte_t raw) {
    switch (raw) {
    case static_cast<byte_t>(Seq::GotAA):
      return Seq::GotAA;
    case static_cast<byte_t>(Seq::Got55):
      return Seq::Got55;
    case static_cast<byte_t>(Seq::Erase_80):
      return Seq::Erase_80;
    case static_cast<byte_t>(Seq::Erase_AA):
      return Seq::Erase_AA;
    case static_cast<byte_t>(Seq::Erase_55):
      return Seq::Erase_55;
    case static_cast<byte_t>(Seq::Cmd60_AA):
      return Seq::Cmd60_AA;
    case static_cast<byte_t>(Seq::Cmd60_55):
      return Seq::Cmd60_55;
    case static_cast<byte_t>(Seq::Hidden77_AA):
      return Seq::Hidden77_AA;
    case static_cast<byte_t>(Seq::Hidden77_55):
      return Seq::Hidden77_55;
    case static_cast<byte_t>(Seq::Hidden77_Final):
      return Seq::Hidden77_Final;
    default:
      return Seq::Idle;
    }
  }

  [[nodiscard]] static std::array<byte_t, kProgMaskBytes>
  pack_written_(const std::bitset<kProgChunk> &bits) {
    std::array<byte_t, kProgMaskBytes> packed{};
    packed.fill(0);
    for (std::size_t i = 0; i < kProgChunk; ++i) {
      if (!bits.test(i))
        continue;
      packed[i >> 3] = static_cast<byte_t>(packed[i >> 3] | (1u << (i & 7)));
    }
    return packed;
  }

  static void unpack_written_(const std::array<byte_t, kProgMaskBytes> &packed,
                              std::bitset<kProgChunk> &bits) {
    bits.reset();
    for (std::size_t i = 0; i < kProgChunk; ++i) {
      const bool on = (packed[i >> 3] & (1u << (i & 7))) != 0;
      if (on)
        bits.set(i);
    }
  }

  // ---------- ROM helpers ----------
  [[nodiscard]] byte_t rom_fixed(const std::size_t off) const {
    return (off < rom_.size()) ? rom_[off] : open_bus();
  }

  [[nodiscard]] std::size_t rom_8k_bank_count() const noexcept {
    return std::max<std::size_t>(1, (rom_.size() + (kRomBank8K - 1)) / kRomBank8K);
  }

  [[nodiscard]] byte_t rom_at_8k(const std::size_t bank, const std::size_t off) const {
    const std::size_t banks = rom_8k_bank_count();
    const std::size_t b = clamp_bank(bank, banks);
    const std::size_t idx = b * kRomBank8K + off;
    return (idx < rom_.size()) ? rom_[idx] : open_bus();
  }

  // ---------- helpers: RAM 4 KiB banks ----------
  [[nodiscard]] byte_t ram_at_4k(const std::size_t bank, const std::size_t off) const {
    if (ram_.empty()) return open_bus();
    const std::size_t banks = std::max<std::size_t>(1, ram_.size() / kRamBank4K);
    const std::size_t b = clamp_bank(bank, banks);
    const std::size_t idx = (b * kRamBank4K + off) % ram_.size();
    return ram_[idx];
  }

  void ram_write_4k(const std::size_t bank, const std::size_t off, const byte_t v) {
    if (ram_.empty()) return;
    const std::size_t banks = std::max<std::size_t>(1, ram_.size() / kRamBank4K);
    const std::size_t b = clamp_bank(bank, banks);
    const std::size_t idx = (b * kRamBank4K + off) % ram_.size();
    ram_[idx] = v;
  }

  // ---------- register writes ----------
  void write_regs(const addr_t addr, const byte_t val) {
    if (addr <= 0x03FF) {
      ram_enabled_ = ((val & 0x0F) == 0x0A);
      return;
    }
    if (addr <= 0x07FF) {
      ram_bank_a_ = static_cast<byte_t>(val & 0x07);
      return;
    }
    if (addr <= 0x0BFF) {
      ram_bank_b_ = static_cast<byte_t>(val & 0x07);
      return;
    }
    if (addr <= 0x0FFF) {
      flash_ce_ = (val & 0x01) != 0;
      // If flash is disabled, exiting any flash mode is reasonable
      if (!flash_ce_) {
        flash_mode_ = FlashMode::ReadArray;
        seq_ = Seq::Idle;
        discard_program_buffers();
      }
      return;
    }
    if (addr <= 0x13FF) {
      flash_wp_ = (val & 0x01) != 0;
      return;
    }
    if (addr <= 0x27FF) {
      a_bank_ = static_cast<byte_t>(val & 0x7F);
      return;
    }
    if (addr <= 0x2FFF) {
      a_flash_ = (val == 0x08) || ((val & 0x08) != 0);
      return;
    }
    if (addr <= 0x37FF) {
      b_bank_ = static_cast<byte_t>(val & 0x7F);
      return;
    }
    if (addr <= 0x3FFF) {
      b_flash_ = (val == 0x08) || ((val & 0x08) != 0);
      return;
    }
  }

  // ---------- window reads/writes ----------
  [[nodiscard]] byte_t read_window(const Window w, const std::size_t off) const {
    const bool use_flash = (w == Window::A) ? a_flash_ : b_flash_;
    const byte_t bank    = (w == Window::A) ? a_bank_  : b_bank_;

    if (!use_flash) {
      return rom_at_8k(bank, off);
    }

    if (!flash_ce_) return open_bus();

    const std::size_t chip_addr = (static_cast<std::size_t>(bank) % kFlashBanks) * kRomBank8K + off;
    return flash_read(chip_addr, off);
  }

  void write_window(const Window w, const addr_t abs_addr, const std::size_t off, const byte_t val) {
    const bool use_flash = (w == Window::A) ? a_flash_ : b_flash_;
    const byte_t bank    = (w == Window::A) ? a_bank_  : b_bank_;

    if (!use_flash) {
      // ROM mapped, ignore writes in 4000-7FFF region.
      return;
    }
    if (!flash_ce_) {
      // /CE disabled -> chip inaccessible
      return;
    }

    const std::size_t chip_addr = (static_cast<std::size_t>(bank) % kFlashBanks) * kRomBank8K + off;
    flash_write(w, abs_addr, chip_addr, off, val);
  }

  // ---------- flash core ----------
  [[nodiscard]] byte_t status_byte() const noexcept {
    // Pan Docs: bit7 done, bit4 timeout, bit1 sector0 protected
    // We model instant completion, no timeout
    byte_t s = 0x80;
    if (sector0_protected_) s |= 0x02;
    return s;
  }

  [[nodiscard]] byte_t flash_read(const std::size_t chip_addr, const std::size_t off_in_window) const {
    switch (flash_mode_) {
    case FlashMode::ReadArray:
      return (chip_addr < flash_.size()) ? flash_[chip_addr] : 0xFF;
    case FlashMode::IdMode:
      // JEDEC ID: (C2,81) at $XXX0,$XXX1
      if (off_in_window == 0x0000) return 0xC2;
      if (off_in_window == 0x0001) return 0x81;
      return 0xFF;
    case FlashMode::HiddenRead:
      return hidden_[off_in_window & 0xFF];
    case FlashMode::Program:
    case FlashMode::HiddenProgram:
    case FlashMode::Status:
      return status_byte();
    }
    return 0xFF;
  }

  // Command address helpers per Pan Docs table:
  // Bank A command addresses: Y=5, X=4 => 0x5555 and 0x4AAA
  // Bank B command addresses: Y=7, X=6 => 0x7555 and 0x6AAA
  [[nodiscard]] static bool is_cmd_aa_addr(const Window w, const addr_t a) noexcept {
    return (w == Window::A) ? (a == 0x5555) : (a == 0x7555);
  }
  [[nodiscard]] static bool is_cmd_55_addr(const Window w, const addr_t a) noexcept {
    return (w == Window::A) ? (a == 0x4AAA) : (a == 0x6AAA);
  }

  [[nodiscard]] bool can_modify_sector0() const noexcept {
    // Flash Write Enable controls /WP; when 0, sector0 and hidden region can't be erased/programmed
    // Additionally, protect/unprotect command can protect sector0
    return flash_wp_ && !sector0_protected_;
  }
  [[nodiscard]] bool can_modify_hidden() const noexcept {
    return flash_wp_;
  }

    void discard_program_buffers() {
    prog_.reset();
    hidden_prog_.reset();
  }

  void commit_program_block(const ProgramLatch& p) {
    for (std::size_t i = 0; i < kProgChunk; ++i) {
      const std::size_t a = p.base + i;
      if (a >= flash_.size()) break;

      const std::size_t sector = a / kSectorSize;
      if (sector == 0 && !can_modify_sector0()) continue;

      // Flash programming typically can only clear bits (1->0)
      flash_[a] = static_cast<byte_t>(flash_[a] & p.buf[i]);
    }
  }

  void commit_hidden_block(const ProgramLatch& p) {
    if (!can_modify_hidden()) return;
    const std::size_t base = p.base & 0xFF;
    for (std::size_t i = 0; i < kProgChunk; ++i) {
      const std::size_t idx = base + i;
      if (idx >= kHiddenSize) break;
      hidden_[idx] = static_cast<byte_t>(hidden_[idx] & p.buf[i]);
    }
  }

  void program_write_main(const std::size_t chip_addr, const byte_t val) {
    const std::size_t base = chip_addr & ~(kProgChunk - 1);

    if (!prog_.active || prog_.base != base) {
      prog_.reset();
      prog_.active = true;
      prog_.base = base;
    }

    const std::size_t off = chip_addr - prog_.base;
    if (off >= kProgChunk) return;

    // Commit happens on second write to the final address after all 128 bytes were written.
    if (prog_.filled && off == (kProgChunk - 1)) {
      commit_program_block(prog_);
      flash_mode_ = FlashMode::Status;
      prog_.reset();
      return;
    }

    prog_.buf[off] = val;
    prog_.written.set(off);
    if (prog_.written.all()) prog_.filled = true;
  }

  void program_write_hidden(const std::size_t hidden_idx, const byte_t val) {
    const std::size_t base = hidden_idx & ~(kProgChunk - 1);

    if (!hidden_prog_.active || hidden_prog_.base != base) {
      hidden_prog_.reset();
      hidden_prog_.active = true;
      hidden_prog_.base = base;
    }

    const std::size_t off = hidden_idx - hidden_prog_.base;
    if (off >= kProgChunk) return;

    if (hidden_prog_.filled && off == (kProgChunk - 1)) {
      commit_hidden_block(hidden_prog_);
      flash_mode_ = FlashMode::Status;
      hidden_prog_.reset();
      return;
    }

    hidden_prog_.buf[off] = val;
    hidden_prog_.written.set(off);
    if (hidden_prog_.written.all()) hidden_prog_.filled = true;
  }

  void flash_write(const Window w, const addr_t abs_addr, const std::size_t chip_addr,
                   const std::size_t off_in_window, const byte_t val) {
    // F0 exits any mode
    if (val == 0xF0) {
      flash_mode_ = FlashMode::ReadArray;
      seq_ = Seq::Idle;
      discard_program_buffers();
      return;
    }

    // If we're outputting status, ignore everything except F0
    if (flash_mode_ == FlashMode::Status) {
      return;
    }

    // Program mode: capture 128 bytes (aligned) into a buffer, then commit on a 2nd write to the final address
    if (flash_mode_ == FlashMode::Program) {
      program_write_main(chip_addr, val);
      return;
    }

    // Hidden region program mode: same 128-byte buffer + commit behavior, but applied to the 256-byte hidden region
    if (flash_mode_ == FlashMode::HiddenProgram) {
      program_write_hidden(off_in_window & 0xFF, val);
      return;
    }

    // HiddenRead/IdMode: generally require F0 to exit; allow starting sequences anyway by resetting state on AA
    // Now handle command sequences (unlock patterns)
    const bool aa_addr = is_cmd_aa_addr(w, abs_addr);
    const bool s55_addr = is_cmd_55_addr(w, abs_addr);

    switch (seq_) {
    case Seq::Idle:
      if (aa_addr && val == 0xAA) seq_ = Seq::GotAA;
      return;

    case Seq::GotAA:
      if (s55_addr && val == 0x55) seq_ = Seq::Got55;
      else seq_ = Seq::Idle;
      return;

    case Seq::Got55:
      if (!aa_addr) { seq_ = Seq::Idle; return; }

      if (val == 0x04) {
        // Erase hidden region* (requires flash_wp_)
        if (can_modify_hidden()) hidden_.fill(0xFF);
        flash_mode_ = FlashMode::Status;
        seq_ = Seq::Idle;
        return;
      }
      if (val == 0xE0) {
        // Program mode for hidden region* (requires flash_wp_)
        if (can_modify_hidden()) {
          flash_mode_ = FlashMode::HiddenProgram;
          hidden_prog_.reset();
          hidden_prog_.active = true;
        } else {
          flash_mode_ = FlashMode::Status;
        }
        seq_ = Seq::Idle;
        return;
      }
      if (val == 0x20) {
        // Protect sector 0*
        if (can_modify_hidden()) sector0_protected_ = true;
        flash_mode_ = FlashMode::Status;
        seq_ = Seq::Idle;
        return;
      }
      if (val == 0x40) {
        // Unprotect sector 0*
        if (can_modify_hidden()) sector0_protected_ = false;
        flash_mode_ = FlashMode::Status;
        seq_ = Seq::Idle;
        return;
      }

      // Third byte is a command at the AA address
      if (val == 0x90) {
        flash_mode_ = FlashMode::IdMode;
        seq_ = Seq::Idle;
        return;
      }
      if (val == 0xA0) {
        flash_mode_ = FlashMode::Program;
        prog_.reset();
        prog_.active = true;
        seq_ = Seq::Idle;
        return;
      }
      if (val == 0x80) {
        seq_ = Seq::Erase_80;
        return;
      }
      if (val == 0x60) {
        seq_ = Seq::Cmd60_AA;
        return;
      }
      if (val == 0x77) {
        seq_ = Seq::Hidden77_AA;
        return;
      }

      seq_ = Seq::Idle;
      return;

    case Seq::Erase_80:
      // Expect AA to cmd addr
      if (aa_addr && val == 0xAA) { seq_ = Seq::Erase_AA; return; }
      seq_ = Seq::Idle;
      return;

    case Seq::Erase_AA:
      // Expect 55 to second addr
      if (s55_addr && val == 0x55) { seq_ = Seq::Erase_55; return; }
      seq_ = Seq::Idle;
      return;

    case Seq::Erase_55: {
      // Final: 0x10 at cmd addr => chip erase
      // Final: 0x30 at ANY addr within target sector => sector erase
      if (val == 0x10 && aa_addr) {
        // Chip erase: erase sectors 1..7 always, sector0 only if allowed
        for (std::size_t s = 0; s < 8; ++s) {
          if (s == 0 && !can_modify_sector0()) continue;
          const std::size_t base = s * kSectorSize;
          const std::size_t end  = std::min(base + kSectorSize, flash_.size());
          std::fill(flash_.begin() + static_cast<std::ptrdiff_t>(base),
                    flash_.begin() + static_cast<std::ptrdiff_t>(end),
                    static_cast<byte_t>(0xFF));
        }
        flash_mode_ = FlashMode::Status;
        seq_ = Seq::Idle;
        return;
      }

      if (val == 0x30) {
        if (const std::size_t sector = chip_addr / kSectorSize; sector < 8) {
          if (sector != 0 || can_modify_sector0()) {
            const std::size_t base = sector * kSectorSize;
            const std::size_t end  = std::min(base + kSectorSize, flash_.size());
            std::fill(flash_.begin() + static_cast<std::ptrdiff_t>(base),
                      flash_.begin() + static_cast<std::ptrdiff_t>(end),
                      static_cast<byte_t>(0xFF));
          }
        }
        flash_mode_ = FlashMode::Status;
        seq_ = Seq::Idle;
        return;
      }

      seq_ = Seq::Idle;
      return;
    }

    case Seq::Cmd60_AA:
      if (aa_addr && val == 0xAA) { seq_ = Seq::Cmd60_55; return; }
      seq_ = Seq::Idle;
      return;

    case Seq::Cmd60_55:
      if (!s55_addr || val != 0x55) { seq_ = Seq::Idle; return; }

      // Next write at cmd addr chooses function; we implement by waiting for the next byte at cmd addr
      // by reusing Got55 state logic: set to Got55 but require aa_addr next
      seq_ = Seq::Got55;
      // And we "pretend" the previous steps already happened, so treat next as a cmd at aa_addr
      // To do that, we keep seq_=Got55 and require aa_addr in that state
      // The caller will immediately return; next write processes
      return;

    case Seq::Hidden77_AA:
      if (aa_addr && val == 0xAA) { seq_ = Seq::Hidden77_55; return; }
      seq_ = Seq::Idle;
      return;

    case Seq::Hidden77_55:
      if (!s55_addr || val != 0x55) { seq_ = Seq::Idle; return; }

      // Next write at cmd addr should be 0x77 to enter HiddenRead
      seq_ = Seq::Hidden77_Final;
      return;

    case Seq::Hidden77_Final:
      if (aa_addr && val == 0x77) {
        flash_mode_ = FlashMode::HiddenRead;
        seq_ = Seq::Idle;
        return;
      }
      seq_ = Seq::Idle;
      return;
    }
  }
  // We overload the AA/55/command decoding to also support the “* commands” listed
  // under the AA 55 60 ... category (hidden erase/program, protect/unprotect)
  // The simplest way: interpret those commands when we see them at the "cmd addr" in Got55,
  // which already requires AA addr
  //
  // To enable that, we check those command bytes in the Got55 handler above
  // But Got55 currently handles 0x90,0xA0,0x80,0x60,0x77 only
  // We extend it by patching in a helper called from write_window once per write,
  // OR implement it directly by adding cases. To keep this file compact, we implement
  // them by exploiting the existing flow: after AA 55 60 AA 55, we land back in Got55,
  // so the "command byte" is processed here too

  // Helpers for persisting MBC6's non-volatile flash/hidden storage
  [[nodiscard]] std::span<const byte_t> flash() const noexcept { return flash_; }
  std::span<byte_t> flash() noexcept { return flash_; }

  [[nodiscard]] std::span<const byte_t> hidden() const noexcept { return {hidden_.data(), hidden_.size()}; }
  std::span<byte_t> hidden() noexcept { return {hidden_.data(), hidden_.size()}; }
};


std::unique_ptr<Mbc> make_mbc6(const cart& c) {
  // MBC6 has non-volatile flash; treat as persistent even if battery flag isn't set
  return std::make_unique<Mbc6>(c.rom_span(), c.declared_ram_bytes, true);
}
