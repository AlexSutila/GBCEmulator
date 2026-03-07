#include "cart/cart.hpp"
#include "cart/mbc.hpp"
#include "cart/mbc_creator.hpp"

// ---------------------------
// EMS (Flash cart / Multi-ROM selector)
// ---------------------------
//
// This mapper is used by certain EMS flash carts / multi-ROM menus.
// A game is typically started by the menu using this sequence:
//
//   Write 0xA5 to 0x1000
//   Write game's first bank number to 0x2000
//   Write any value to 0x7000            (commit / latch game base)
//   Write 0x98 to 0x1000
//   Write 0x01 to 0x2000                 (so 32K games work)
//   Jump to 0x0100
//
// Memory (in "game mode"):
// ROM bank "base" mapped at 0000-3FFF
// ROM bank "base + bank_sel" mapped at 4000-7FFF
//
// Notes:
// - This implementation models just enough behavior for menus to boot games.
// - No cart RAM is modeled here (EMS carts are typically flash-only for ROM).

class Ems final : public Mbc {
public:
  explicit Ems(std::span<const byte_t> const rom) : rom_(rom) {
    // Default to a menu-like mapping: bank 0 + bank 1.
    sync_banks_();
  }

  byte_t read(addr_t const addr) override {
    if (addr <= 0x3FFF)
      return rom_at(rom_, rom0_bank_, addr);
    if (addr <= 0x7FFF)
      return rom_at(rom_, rom1_bank_, addr - 0x4000);

    return open_bus();
  }

  void write(addr_t const addr, byte_t const val) override {
    // "Key" writes
    // Docs refer to 0x1000 specifically; accept the whole 1000-1FFF page.
    if (addr >= 0x1000 && addr <= 0x1FFF) {
      if (val == 0xA5) {
        mode_ = Mode::SelectGameBase;
        return;
      }
      if (val == 0x98) {
        mode_ = Mode::SelectBank;
        return;
      }
      mode_ = Mode::None;
      return;
    }

    // Bank register writes
    if (addr >= 0x2000 && addr <= 0x3FFF) {
      switch (mode_) {
      case Mode::SelectGameBase:
        pending_base_ = val;
        return;

      case Mode::SelectBank:
        bank_sel_ = val;
        sync_banks_();
        return;

      case Mode::None:
      default:
        // If a menu writes without "keying" first, ignore by default.
        return;
      }
    }

    // Commit/latch write
    // Docs refer to 0x7000 specifically; accept the whole 7000-7FFF page.
    if (addr >= 0x7000 && addr <= 0x7FFF) {
      if (mode_ == Mode::SelectGameBase) {
        // Latch game base and enter game mode.
        in_game_ = true;
        base_bank_ = pending_base_;
        bank_sel_ = 0;
        sync_banks_();
      }
      return;
    }

    // No RAM / flash programming modeled here.
  }

  [[nodiscard]] bool has_battery() const noexcept override { return false; }
  [[nodiscard]] std::span<const byte_t> ram() const noexcept override { return {}; }
  std::span<byte_t> ram() noexcept override { return {}; }
  [[nodiscard]] const char *savestate_tag() const noexcept override {
    return "EMS ";
  }

private:
  enum class Mode : byte_t {
    None = 0,
    SelectGameBase,
    SelectBank,
  };

  [[nodiscard]] static Mode mode_from_raw_(const byte_t raw) {
    switch (raw) {
    case static_cast<byte_t>(Mode::SelectGameBase):
      return Mode::SelectGameBase;
    case static_cast<byte_t>(Mode::SelectBank):
      return Mode::SelectBank;
    default:
      return Mode::None;
    }
  }

  void sync_banks_() {
    if (in_game_) {
      rom0_bank_ = static_cast<std::size_t>(base_bank_);
      rom1_bank_ = static_cast<std::size_t>(base_bank_) +
                   static_cast<std::size_t>(bank_sel_);
      return;
    }
    rom0_bank_ = 0;
    rom1_bank_ = bank_sel_ ? bank_sel_ : 1;
  }

  std::span<const byte_t> rom_;

  bool in_game_{false};
  Mode mode_{Mode::None};

  byte_t pending_base_{0};
  byte_t base_bank_{0};
  byte_t bank_sel_{1};

  std::size_t rom0_bank_{0};
  std::size_t rom1_bank_{1};
};

std::unique_ptr<Mbc> make_ems(const cart &c) {
  return std::make_unique<Ems>(c.rom_span());
}
