import "mbc.hpp";


// ---------------------------
// No MBC (32 KiB ROM only) + optional 8 KiB RAM
// ---------------------------
// ROM maps directly to 0000-7FFF; optional 8 KiB RAM at A000-BFFF via discrete logic
class MbcNone final : public Mbc {
public:
    MbcNone(std::span<const byte_t> rom, std::size_t ram_bytes, bool battery)
        : rom_(rom), ram_(ram_bytes), battery_(battery) {}

    byte_t read(std::uint16_t addr) override {
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

    void write(std::uint16_t addr, byte_t val) override {
        if (addr >= 0xA000 && addr <= 0xBFFF && !ram_.empty()) {
            ram_[(addr - 0xA000) % ram_.size()] = val;
        }
        // writes to ROM area do nothing (no controller)
    }

    bool has_battery() const noexcept override { return battery_; }
    std::span<const byte_t> ram() const noexcept override { return ram_; }
    std::span<byte_t>       ram() noexcept override { return ram_; }

private:
    std::span<const byte_t> rom_;
    std::vector<byte_t>     ram_;
    bool battery_{};
};