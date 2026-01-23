#include "gbc.hpp"
#include "cart/cart.hpp"
#include "cpu/lr35902.hpp"
#include "memory/boot.hpp"
#include "memory/bus.hpp"
#include "memory/dma.hpp"
#include "memory/mmio/dmg.hpp"
#include "memory/mmio/mmio.hpp"
#include "ppu/palette.hpp"
#include "ppu/ppu.hpp"
#include "timer/timer.hpp"
#include <memory>

GameBoyColor::GameBoyColor(Frontend &frontend) : fe_(frontend) {
  using mmio = IORegisterMapping;
  system_init(); // Generic hardware connection initialization

  const LR35902::ProcessorState cpu_regs = cgb_boot_regs();
  /* We cannot simply start executing without a BIOS for numerous reasons. So,
   * if we want to skip the BIOS, we need the system to 'pretend' like it really
   * did run through the BIOS. */
  cpu->load_state(cpu_regs); // Fake CPU register values
  hwio_init();               // Fake the post-bios MMIO register values

  // Color ram has to be initialized for both sprites and background
  cram_init(mmio::MMIO_LCD_BGPI, mmio::MMIO_LCD_BGPD);
  cram_init(mmio::MMIO_LCD_OBPI, mmio::MMIO_LCD_OBPD);
}

void GameBoyColor::system_init() {
  /* General system operation info */
  sys_ = {
      .elapsed_clocks = 0,
      .cgb_mode = true,
      .halted = false,
      .speed_switch_armed = false,
      .double_speed = false,
  };

  /* Component initializaiton */
  bus = std::make_unique<AddressBus>(sys_);
  cpu = std::make_unique<LR35902>(bus.get(), sys_);
  apu = std::make_unique<APU>(*bus, fe_);
  ppu = std::make_unique<PixelProcessingUnit>(bus.get(), fe_, sys_);
  timer = std::make_unique<TimerUnit>(bus.get(), sys_);

  /* Joypad initialization */
  auto *joypad_reg = dynamic_cast<Joypad::JOYP *>(
      bus->get_mmio(IORegisterMapping::MMIO_JOYPAD));
  auto *if_reg = dynamic_cast<InterruptBits *>(
      bus->get_mmio(IORegisterMapping::MMIO_INT_FLAGS));
  if (!joypad_reg || !if_reg)
    throw std::logic_error("Failed to configure joypad MMIO");

  /* To avoid running into problems with other registers it depends on in time,
   * we have to invoke this method to configure the dependencies it needs after
   * we can garuntee they have been instantiated. */
  joypad_reg->set_interrupt_reg(if_reg);
}

void GameBoyColor::hwio_init() {
  using mmio = IORegisterMapping;

  // Load hardware MMIO registers with post-BIOS initial conditions
  bus->write_byte(static_cast<addr_t>(mmio::MMIO_JOYPAD), 0xC7);
  // TODO: Serial registers FF01 and FF02
  bus->write_byte(static_cast<addr_t>(mmio::MMIO_TIMER_TIMA), 0x00);
  bus->write_byte(static_cast<addr_t>(mmio::MMIO_TIMER_TMA), 0x00);
  bus->write_byte(static_cast<addr_t>(mmio::MMIO_TIMER_TAC), 0xF8);
  bus->write_byte(static_cast<addr_t>(mmio::MMIO_INT_FLAGS), 0xE1);
  // TODO: Audio registers
  bus->write_byte(static_cast<addr_t>(mmio::MMIO_LCD_CONTROL), 0x91);
  bus->write_byte(static_cast<addr_t>(mmio::MMIO_LCD_SCY), 0x00);
  bus->write_byte(static_cast<addr_t>(mmio::MMIO_LCD_SCX), 0x00);
  bus->write_byte(static_cast<addr_t>(mmio::MMIO_LCD_Y_COMP), 0x00);
  bus->write_byte(static_cast<addr_t>(mmio::MMIO_LCD_Y_COMP), 0x00);
  // DMA (0xFF46) left blank intentionally - dont want to trigger it
  bus->write_byte(static_cast<addr_t>(mmio::MMIO_LCD_WY), 0x00);
  bus->write_byte(static_cast<addr_t>(mmio::MMIO_LCD_WX), 0x00);
  // KEY0 (0xFF4C) left blank intentionally - primed during cart insertion
  // KEY1 (0xFF4D) left blank intentionally - not touched in real cgb bios
  bus->write_byte(static_cast<addr_t>(mmio::MMIO_VRAM_BANK), 0xFE);
  bus->write_byte(static_cast<addr_t>(mmio::MMIO_VDMA1), 0xFF);
  bus->write_byte(static_cast<addr_t>(mmio::MMIO_VDMA2), 0xFF);
  bus->write_byte(static_cast<addr_t>(mmio::MMIO_VDMA3), 0xFF);
  bus->write_byte(static_cast<addr_t>(mmio::MMIO_VDMA4), 0xFF);
  // VDMA5 (0xFF55) left blank intentionally - dont want to trigger it
  bus->write_byte(static_cast<addr_t>(mmio::MMIO_VDMA4), 0xFF);
  bus->write_byte(static_cast<addr_t>(mmio::MMIO_WRAM_BANK), 0xF8);
  bus->write_byte(static_cast<addr_t>(mmio::MMIO_INT_ENABLE), 0x00);

  // Finally, perform a synthetic write to unmap the boot ROM and terminate
  // the execution of our fake basic input-output system.
  bus->write_byte(static_cast<addr_t>(mmio::MMIO_VRAM_BANK), 0xFE);
}

void GameBoyColor::cram_init(IORegisterMapping index, IORegisterMapping data) {
  constexpr auto nr_palettes = 8;
  constexpr auto nr_colors = 4;

  /* Convert index and data enumerations into 16-bit addresses */
  const addr_t cram_index = static_cast<addr_t>(index);
  const addr_t cram_data = static_cast<addr_t>(data);
  bus->write_byte(cram_index, 0x80); // Write high bit for auto increment

  /* This is where CRAM is actually populated, by writing the actual values in
   * that would normally be written in over the address bus. */
  for (auto pal{0}; pal < nr_palettes; pal++) {
    for (byte_t color{0}; color < nr_colors; color++) {
      const std::uint32_t argb8888 = get_mono_color(color & 0x3);
      const std::uint16_t rgb555 = argb8888_to_rgb555(argb8888);
      bus->write_byte(cram_data, static_cast<byte_t>(rgb555 & 0xFF));
      bus->write_byte(cram_data, static_cast<byte_t>((rgb555 >> 8) & 0xFF));
    }
  }
}

void GameBoyColor::insert_cartridge(cart c) {
  const byte_t &cgb_flag = c.header.cgb_flag();
  if (!bus)
    throw std::logic_error("Bus not initialized");
  bus->insert_cartridge(c);

  /* IMPORTANT: on the real hardware, this flag is set via KEY0 during the BIOS
   * based on the cartridge header. If we are skipping the BIOS, this init step
   * will never happen. Hence we must do it ourselves when we skip the BIOS.
   * --------------------------------------------------------------------------
   * Another side note, KEY0 uses a reference to this boolean to formulate the
   * actual value of the register read over the address bus. Hence, by changing
   * this we are also updating the value of KEY0 respectively. */
  sys_.cgb_mode = cgb_enabled(cgb_flag);
}

void GameBoyColor::init_test_bed() {
  if (!bus)
    throw std::logic_error("Bus not initialized");

  /* Init convenience RAM-only cartridge for testing */
  bus->init_test_bed();
}

void GameBoyColor::step_dma(bool fast_cycle) {
  bus->get_oam_dma().step(); // Runs 2X in double speed

  /* As described elsewhere, HDMA and GDMA have an initialization phase that
   * does run fast in double speed mode, but the transfers themselves don't */
  if (fast_cycle)
    bus->get_vdma().step_fast_cycle();
  else
    bus->get_vdma().step();
}
bool GameBoyColor::vdma_enabled() const { return bus->get_vdma().enabled(); }

void GameBoyColor::step_processor() {
  const auto &vdma = bus->get_vdma();
  if (!vdma.enabled()) // CPU is halted until VDMA is complete
    cpu->step();
}

void GameBoyColor::step() {
  step_processor();
  step_dma(false);
  ppu->step();
  timer->step();
  apu->step();

  // System clocks are maintained in unit `t-cycles`
  ++sys_.elapsed_clocks;

  // If we are in double speed mode, step affected components again
  if (sys_.double_speed) {
    step_processor();
    step_dma(true);
    timer->step();
  }
}
