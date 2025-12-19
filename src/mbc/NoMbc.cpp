#include "mbc.hpp"


// ---------------------------
// No MBC (32 KiB ROM only) + optional 8 KiB RAM
// ---------------------------
// ROM maps directly to 0000-7FFF; optional 8 KiB RAM at A000-BFFF via discrete logic
class NoMbc final : public Mbc {
public:
    NoMbc(const std::span<const byte_t> rom, const std::size_t ram_bytes, const bool battery)
        : rom_(rom), ram_(ram_bytes), battery_(battery) {}

    byte_t read(const addr_t addr) override {
        if (addr <= 0x7FFF) {
            if (addr < rom_.size()) return rom_[addr];
            return open_bus();
        }
        if (addr >= 0xA000 && addr <= 0xBFFF) {
            if (ram_.empty()) return open_bus();
            return ram_[(addr - 0xA000) % ram_.size()];
        }
        return open_bus();
    }

    void write(const addr_t addr, const byte_t val) override {
        if (addr >= 0xA000 && addr <= 0xBFFF && !ram_.empty()) {
            ram_[(addr - 0xA000) % ram_.size()] = val;
        }
        // writes to ROM area do nothing (no controller)
    }

    [[nodiscard]] bool has_battery() const noexcept override { return battery_; }
    [[nodiscard]] std::span<const byte_t> ram() const noexcept override { return ram_; }
    std::span<byte_t>       ram() noexcept override { return ram_; }

private:
    std::span<const byte_t> rom_;
    std::vector<byte_t>     ram_;
    bool battery_{};
};