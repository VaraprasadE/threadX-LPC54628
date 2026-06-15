# RA8889 SPI Display Driver Overview for ThreadX + GUIX

## 1. Purpose

This document explains how a ThreadX + GUIX application can drive an external RA8889 display controller over SPI.
The RA8889 is not controlled by a simple opcode list. It uses an indirect, register-based architecture, so every MCU interaction is built from a few basic host cycles.

## 2. ThreadX + GUIX High-Level Flow

- `ThreadX` provides the RTOS threads and scheduling.
- `GUIX` provides the graphical framework and generates the canvas contents.
- The GUIX display driver must take GUIX dirty-area updates and forward the pixel data to the RA8889 over SPI.
- The MCU application typically:
  1. initializes board clocks, pins, and peripherals,
  2. starts ThreadX,
  3. initializes GUIX with `gx_system_initialize()`,
  4. configures a display using a board-specific driver setup function,
  5. creates screens/widgets,
  6. starts GUIX event processing with `gx_system_start()`.

## 3. RA8889 Communication Model

The RA8889 is controlled by writing register addresses and register data indirectly. There are no stand-alone display command opcodes in the traditional sense.
Instead, every host transaction is one of four fundamental cycles determined by two bits:

- `A0` (Command/Data Select)
- `WR#` (Write/Read)

Together they define the operation:

| Cycle | A0 | WR# | Purpose |
|------|----|-----|---------|
| Command Write | 0 | 0 | Set the target register address |
| Data Write    | 1 | 0 | Write data to the selected register or memory port |
| Status Read   | 0 | 1 | Read the status register (STSR) |
| Data Read     | 1 | 1 | Read data from the selected register or memory port |

> On SPI/IIC, the `A0` bit is embedded in the serial header. On parallel interfaces, it is driven by the `XA0` pin.

## 4. RA8889 SPI Interface Specifics

For SPI, the low-level transactions are packetized into two distinct bytes. The first byte is a command header containing only:
- the `A0` bit in an MSB position,
- the `WR#` bit in the next MSB position,
- and six padding/dummy bits.

The second byte carries the actual payload:
- the register number (`REG_NO`) for a Command Write cycle,
- or the register/data byte (`REG_DAT` / `MEM_DAT`) for a Data Write cycle.

So the SPI sequence is not a single combined header+address byte. It is always:
1. header byte with `A0/WR#`,
2. payload byte with register address or data.

When using 3-wire or 4-wire SPI, the protocol still follows the four fundamental host cycles, but the SPI packet is always split into a one-byte control header followed by a one-byte payload.

## 5. Driver Procedures

A robust RA8889 driver usually implements three core procedures:

### 5.1 Register Write Procedure

1. Perform a **Command Write** cycle with the 8-bit register address.
2. Perform a **Data Write** cycle with the 8-bit register value.

This is the basic method for configuring chip registers.

### 5.2 Register Read Procedure

1. Perform a **Command Write** cycle with the 8-bit register address.
2. Perform a **Data Read** cycle to retrieve the register value.

This is used to verify configuration or read status values.

### 5.3 Memory Write Procedure

Sending image data to the RA8889 is a burst write sequence:

1. Define the active drawing window using the active window registers.
2. Set the write position with the X/Y coordinate registers.
3. Perform a **Command Write** cycle to point to `REG[04h]` (Memory Data Port).
4. Perform repeated **Data Write** cycles to send pixel data.

Because the RA8889 auto-increments its internal memory pointer after writing to `REG[04h]`, the driver does not need to resend the address for every pixel.

> For SPI mode, each `Data Write` cycle transfers only a single 8-bit value. Even when the display is running in 16-bit RGB565 mode internally, the SPI host must split each 16-bit pixel into two sequential 8-bit writes to `REG[04h]`.

## 6. Key RA8889 Registers

The most important registers for display operation are:

- `STSR` (Status Register)
  - Read via Status Read Cycle.
  - Check busy state, FIFO full state, and interrupt flags.
- `REG[04h]` (Memory Data Port)
  - Write pixel data here.
- `REG[01h]` to `REG[0Fh]` (Chip Configuration)
  - Configure PLL, host interface width, display mode, and clock settings.
- `REG[10h]` to `REG[1Fh]` (LCD Display Control)
  - Set screen color depth, turn the LCD on/off, and choose the main/PIP window format.
- `REG[56h]` to `REG[5Eh]` (Active Window)
  - Set the target window on the screen.
- `REG[5Fh]` to `REG[62h]` (Write Position)
  - Set the current X/Y write address before sending pixel data.

## 7. Suggested RA8889 SPI Driver Structure

A clean driver can be layered like this:

1. Low-level SPI transfer helpers
   - `ra8889_spi_write_command(uint8_t address)`
   - `ra8889_spi_write_data(uint8_t value)`
   - `ra8889_spi_read_status(void)`
   - `ra8889_spi_read_data(void)`
2. Register access helpers
   - `ra8889_write_register(uint8_t addr, uint8_t data)`
   - `ra8889_read_register(uint8_t addr)`
3. Display setup helpers
   - `ra8889_init_display()`
   - `ra8889_set_window(x0, y0, x1, y1)`
   - `ra8889_set_write_position(x, y)`
4. Memory write helper
   - `ra8889_write_pixels(const uint16_t *pixels, size_t count)`
     - note: in SPI mode, this helper must split each 16-bit RGB565 pixel into two 8-bit Data Write cycles to `REG[04h]`.

## 8. GUIX Integration

In GUIX, the display driver registers a buffer-toggle callback. This callback is responsible for sending dirty pixels to the physical display.

A typical integration path is:

1. Call `_gx_display_driver_565rgb_setup(display, aux_data, buffer_toggle_callback)` in your board graphics driver setup function.
2. In `buffer_toggle_callback(GX_CANVAS *canvas, GX_RECTANGLE *dirty_area)`, do:
   - compute the dirty rectangle bounds,
   - translate those bounds into RA8889 active window and write position registers,
   - call the RA8889 memory write procedure for each row or block of pixels.
3. Ensure the dirty area pixel format matches the RA8889 configured color depth.

## 9. Example GUIX Driver Flow

A simplified flow inside the board driver would be:

1. `gx_system_initialize()`
2. `gx_studio_display_configure(HOME, board_graphics_driver_setup_565rgb, ...)`
3. `board_graphics_driver_setup_565rgb()`:
   - initialize SPI
   - initialize RA8889 registers
   - call `_gx_display_driver_565rgb_setup(display, NULL, board_graphics_driver_buffer_toggle)`
4. `board_graphics_driver_buffer_toggle()`:
   - read dirty rectangle from GUIX canvas memory
   - configure RA8889 active window and write position
   - write pixel data via the RA8889 memory write procedure

## 10. Important Notes

- The RA8889 uses an indirect register model, so the driver must track the current selected register and send the correct command/data cycles.
- Always check `STSR` before burst writes to avoid overrunning the FIFO or writing while the chip is busy.
- If the interface is SPI, the `A0` bit is encoded in the serial host header, not by a separate pin.
- The RA8889 may require special initialization of PLL and clock registers before the display can accept pixel data.
- In Serial host mode (SPI/IIC), the chip forces the host data bus width to 8-bit access only. That means SPI memory writes to `REG[04h]` must always be performed as 8-bit transfers, even for 16-bit RGB565 pixels.

## 11. Conclusion

To drive the RA8889 from ThreadX/GUIX over SPI, the driver must:
- implement the four basic host cycles,
- provide register read/write and burst memory-write procedures,
- integrate GUIX's dirty-area callback with the RA8889 window and data port registers,
- and use the chip's status register to wait safely for readiness.

This architecture allows GUIX to render to memory in the MCU and then transmit only the changed pixels to the RA8889 display controller.
