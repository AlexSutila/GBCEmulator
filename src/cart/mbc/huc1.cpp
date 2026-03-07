#include "cart/cart.hpp"
#include "cart/mbc.hpp"
#include "cart/mbc_creator.hpp"

// ---------------------------
// HuC1 (ROM + RAM + IR)
// ---------------------------
// Pan Docs: https://gbdev.io/pandocs/HuC1.html
//
// Memory:
// Fixed ROM bank 00 (16 KiB)            (0000-3FFF)
// Switchable ROM bank (16 KiB)          (4000-7FFF)
// Switchable RAM bank (8 KiB banks)     (A000-BFFF)  OR IR register
//
// Registers (write only):
// IR select (0E=IR mode, else RAM mode) (0000-1FFF)
// ROM bank select (>=6 bits)            (2000-3FFF)
// RAM bank select (>=2 bits)            (4000-5FFF)
// (6000-7FFF unused)
//
// A000-BFFF:
// - RAM mode: behaves like cart RAM
// - IR mode: IR register (write: bit0 TX on/off; read: C1 if light else C0)

class HuC1 final : public Mbc {
public:
  HuC1(std::span<const byte_t> const rom, std::size_t const ram_bytes,
       bool const battery)
      : rom_(rom), ram_(ram_bytes), battery_(battery) {}

  byte_t read(addr_t const addr) override {
    if (addr <= 0x3FFF)
      return rom_at(rom_, 0, addr);
    if (addr <= 0x7FFF)
      return rom_at(rom_, rom_bank_, addr - 0x4000);

    if (addr >= 0xA000 && addr <= 0xBFFF) {
      if (ir_mode_) {
        // No actual IR environment: default "no light"
        return static_cast<byte_t>(0xC0 | (ir_light_ ? 0x01 : 0x00));
      }
      if (ram_.empty())
        return open_bus();
      return ram_at(ram_bank_, addr - 0xA000);
    }

    return open_bus();
  }

  void write(addr_t const addr, byte_t const val) override {
    if (addr <= 0x1FFF) {
      // 0x0E -> IR mode, else RAM mode
      ir_mode_ = ((val & 0x0F) == 0x0E);
      return;
    }
    if (addr <= 0x3FFF) {
      // "At least 6 bits"; accept 7 and clamp to ROM size
      rom_bank_ = static_cast<byte_t>(val & 0x7F);
      return;
    }
    if (addr <= 0x5FFF) {
      // "At least 2 bits"
      ram_bank_ = static_cast<byte_t>(val & 0x03);
      return;
    }
    if (addr <= 0x7FFF) {
      // Unused region on HuC1; ignore
      return;
    }

    if (addr >= 0xA000 && addr <= 0xBFFF) {
      if (ir_mode_) {
        ir_tx_on_ = (val & 0x01) != 0;
        return;
      }
      if (ram_.empty())
        return;
      ram_write(ram_bank_, addr - 0xA000, val);
    }
  }

  [[nodiscard]] bool has_battery() const noexcept override { return battery_; }
  [[nodiscard]] std::span<const byte_t> ram() const noexcept override { return ram_; }
  std::span<byte_t> ram() noexcept override { return ram_; }
  [[nodiscard]] const char *savestate_tag() const noexcept override {
    return "HUC1";
  }

private:
  std::span<const byte_t> rom_;
  std::vector<byte_t> ram_;
  bool battery_{};

  byte_t rom_bank_{1};
  byte_t ram_bank_{0};

  bool ir_mode_{false};
  bool ir_tx_on_{false};
  bool ir_light_{false}; // TODO: hook to a simulated IR environment?

  [[nodiscard]] byte_t ram_at(std::size_t const bank, std::size_t const off) const {
    if (ram_.empty())
      return open_bus();
    const std::size_t banks = std::max<std::size_t>(1, ram_.size() / kRamBankSize);
    const std::size_t b = clamp_bank(bank, banks);
    const std::size_t idx = (b * kRamBankSize + off) % ram_.size();
    return ram_[idx];
  }

  void ram_write(std::size_t const bank, std::size_t const off, byte_t const v) {
    if (ram_.empty())
      return;
    const std::size_t banks = std::max<std::size_t>(1, ram_.size() / kRamBankSize);
    const std::size_t b = clamp_bank(bank, banks);
    const std::size_t idx = (b * kRamBankSize + off) % ram_.size();
    ram_[idx] = v;
  }
};

std::unique_ptr<Mbc> make_huc1(const cart &c) {
  return std::make_unique<HuC1>(c.rom_span(), c.declared_ram_bytes,
                                type_has_battery(c.header.cartridge_type));
}
