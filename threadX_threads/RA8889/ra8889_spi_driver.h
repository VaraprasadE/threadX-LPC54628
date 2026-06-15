#ifndef RA8889_SPI_DRIVER_H
#define RA8889_SPI_DRIVER_H

#include <stdbool.h>
#include <stdint.h>
#include <gx_api.h>

void ra8889_spi_write_command(uint8_t reg_addr);
void ra8889_spi_write_data(uint8_t data);
uint8_t ra8889_spi_read_status(void);
uint8_t ra8889_spi_read_data(void);
void ra8889_write_register(uint8_t reg, uint8_t data);
void ra8889_set_active_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1);
void ra8889_set_write_position(uint16_t x, uint16_t y);
bool board_graphics_driver_is_spi_initialized(void);
uint32_t board_graphics_driver_get_flush_count(void);
uint32_t board_graphics_driver_get_pixel_count(void);
uint32_t board_graphics_driver_get_byte_count(void);
void board_graphics_driver_buffer_toggle(GX_CANVAS *canvas, GX_RECTANGLE *dirty_area);

#endif // RA8889_SPI_DRIVER_H
