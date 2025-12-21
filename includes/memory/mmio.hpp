#ifndef __MMIO_H
#define __MMIO_H

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
  MMIO_INT_FLAGS = 0xFF0F,
  MMIO_LCD_CONTROL = 0xFF40,
  MMIO_LCD_STATUS = 0xFF41,
  MMIO_LCD_Y_COOR = 0xFF44,
  MMIO_LCD_Y_COMPARE = 0xFF45,
  MMIO_BOOT_ROM_CTRL = 0xFF50,
  MMIO_INT_ENABLE = 0xFFFF,
};

/*
 * General purpose MMIO Register and abstract class for more complicated IO
 * registers that actually interact with other hardware components.
 */
class MMIORegister {
public:
  virtual void write(const byte_t value);
  virtual byte_t read(); // Not const, reads could alter internal state
  MMIORegister(const byte_t init_state) : state(init_state) {}
  MMIORegister() : state(0) {}

private:
  byte_t state{}; // Internal state
};

#endif // __MMIO_H
