#ifndef GBC_MMIO_HPP
#define GBC_MMIO_HPP

#include "emu_types.hpp"

/*
 * Game Boy I/O Register Map (FF00–FF7F)
 *
 *  Start   End     First Appeared   Purpose
 *  --------------------------------------------------------------------
 *  FF00            DMG              Joypad input
 *  FF01    FF02    DMG              Serial transfer
 *  FF04    FF07    DMG              Timer and divider
 *  FF0F            DMG              Interrupt flags
 *  FF10    FF26    DMG              Audio registers
 *  FF30    FF3F    DMG              Wave pattern RAM
 *  FF40    FF4B    DMG              LCD control, status, position,
 *                                  scrolling, and palettes
 *  FF46            DMG              OAM DMA transfer
 *  FF4C    FF4D    CGB              KEY0 and KEY1
 *  FF4F            CGB              VRAM bank select
 *  FF50            DMG              Boot ROM mapping control
 *  FF51    FF55    CGB              VRAM DMA
 *  FF56            CGB              Infrared (IR) port
 *  FF68    FF6B    CGB              BG / OBJ palettes
 *  FF6C            CGB              Object priority mode
 *  FF70            CGB              WRAM bank select
 */
enum class IORegisterMapping : addr_t {
  MMIO_JOYPAD = 0xFF00,
  MMIO_SERIAL_DATA = 0xFF01,
  MMIO_SERIAL_CTRL = 0xFF02,
  MMIO_TIMER_DIV = 0xFF04,
  MMIO_TIMER_TIMA = 0xFF05,
  MMIO_TIMER_TMA = 0xFF06,
  MMIO_TIMER_TAC = 0xFF07,
  MMIO_INT_FLAGS = 0xFF0F,
  MMIO_AUDIO_BASE = 0xFF10,
  MMIO_WAVE_RAM_BASE = 0xFF30,
  MMIO_LCD_CTRL = 0xFF40,
  MMIO_LCD_STAT = 0xFF41,
  MMIO_LCD_SCY = 0xFF42,
  MMIO_LCD_SCX = 0xFF43,
  MMIO_LCD_Y_COOR = 0xFF44,
  MMIO_LCD_Y_COMP = 0xFF45,
  MMIO_OAM_DMA = 0xFF46,
  MMIO_LCD_BGP = 0xFF47,
  MMIO_LCD_OBP0 = 0xFF48,
  MMIO_LCD_OBP1 = 0xFF49,
  MMIO_LCD_WY = 0xFF4A,
  MMIO_LCD_WX = 0xFF4B,
  MMIO_SPD_KEY0 = 0xFF4C,
  MMIO_SPD_KEY1 = 0xFF4D,
  MMIO_VRAM_BANK = 0xFF4F,
  MMIO_BOOT_ROM_CTRL = 0xFF50,
  MMIO_VDMA1 = 0xFF51,
  MMIO_VDMA2 = 0xFF52,
  MMIO_VDMA3 = 0xFF53,
  MMIO_VDMA4 = 0xFF54,
  MMIO_VDMA5 = 0xFF55,
  MMIO_LCD_BGPI = 0xFF68,
  MMIO_LCD_BGPD = 0xFF69,
  MMIO_LCD_OBPI = 0xFF6A,
  MMIO_LCD_OBPD = 0xFF6B,
  MMIO_LCD_OPRI = 0xFF6C,
  MMIO_WRAM_BANK = 0xFF70,
  MMIO_INT_ENABLE = 0xFFFF,
};

/*
 * General purpose MMIO Register and abstract class for more complicated IO
 * registers that actually interact with other hardware components.
 */
class MMIORegister {
public:
  explicit MMIORegister(const byte_t init_state) : state_(init_state) {}
  MMIORegister() : state_(0) {}

  template <typename T> void parse_savestate(T &t);
  virtual ~MMIORegister() = default;

  /* Note that read() is meant for address bus which may alter internal state
   * peak() can be used by other components to read state. */
  virtual void write(byte_t value);
  [[nodiscard]] virtual byte_t peek() const; // Non-state altering read
  virtual byte_t read();                     // Not const, reads could alter internal state

  /* Overriding this is entirely optional. The intention is, return true if this
   * should behave as an unused 'open bus - return 0xFF' in CGB mode type
   * register. Such support may be useful for extending this code backwards to
   * re-implement a true DMG GameBoy emulator.
   *
   * Although it may be useless for this emulator, which strictly emulates a
   * GameBoy color, we leave the option here regardless. */
  virtual constexpr bool cgb() { return false; }

protected:
  byte_t state_{}; // Internal register state
};

#endif // GBC_MMIO_HPP
