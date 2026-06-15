#include "ra8889_spi_driver.h"
#include "board.h"
#include "fsl_lpspi.h"
#include <stdbool.h>
#include <stddef.h>

#define RA8889_CMD_HEADER_COMMAND_WRITE 0x00u
#define RA8889_CMD_HEADER_DATA_WRITE    0x80u
#define RA8889_CMD_HEADER_STATUS_READ   0x40u
#define RA8889_CMD_HEADER_DATA_READ     0xC0u

#define RA8889_REG_MEMORY_DATA_PORT     0x04u
#define RA8889_REG_ACTIVE_WINDOW_LEFT   0x56u
#define RA8889_REG_ACTIVE_WINDOW_TOP    0x57u
#define RA8889_REG_ACTIVE_WINDOW_RIGHT  0x58u
#define RA8889_REG_ACTIVE_WINDOW_BOTTOM 0x59u
#define RA8889_REG_WRITE_X_LSB          0x5Fu
#define RA8889_REG_WRITE_X_MSB          0x60u
#define RA8889_REG_WRITE_Y_LSB          0x61u
#define RA8889_REG_WRITE_Y_MSB          0x62u

static void ra8889_spi_transaction(const uint8_t *tx_buf, uint8_t *rx_buf, uint16_t len)
{
    lpspi_transfer_t transfer = {
        .txData = (uint8_t *)tx_buf,
        .rxData = rx_buf,
        .dataSize = len,
        .configFlags = kLPSPI_MasterPcs0,
    };

    LPSPI_MasterTransferBlocking(BOARD_GRAPHICS_LPSPI_BASEADDR, &transfer);
}

void ra8889_spi_write_command(uint8_t reg_addr)
{
    uint8_t tx[2] = {RA8889_CMD_HEADER_COMMAND_WRITE, reg_addr};
    ra8889_spi_transaction(tx, NULL, 2);
}

void ra8889_spi_write_data(uint8_t data)
{
    uint8_t tx[2] = {RA8889_CMD_HEADER_DATA_WRITE, data};
    ra8889_spi_transaction(tx, NULL, 2);
}

uint8_t ra8889_spi_read_status(void)
{
    uint8_t tx[2] = {RA8889_CMD_HEADER_STATUS_READ, 0x00u};
    uint8_t rx[2] = {0};
    ra8889_spi_transaction(tx, rx, 2);
    return rx[1];
}

uint8_t ra8889_spi_read_data(void)
{
    uint8_t tx[2] = {RA8889_CMD_HEADER_DATA_READ, 0x00u};
    uint8_t rx[2] = {0};
    ra8889_spi_transaction(tx, rx, 2);
    return rx[1];
}

void ra8889_write_register(uint8_t reg, uint8_t data)
{
    ra8889_spi_write_command(reg);
    ra8889_spi_write_data(data);
}

static void ra8889_wait_write_fifo_not_full(void)
{
    const uint32_t timeout_limit = 100000u;
    uint32_t timeout = 0u;

    while (timeout < timeout_limit)
    {
        uint8_t status = ra8889_spi_read_status();
        if ((status & 0x80u) == 0u)
        {
            return;
        }
        timeout++;
    }
}

void ra8889_set_active_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
    ra8889_write_register(RA8889_REG_ACTIVE_WINDOW_LEFT,  (uint8_t)(x0 & 0xFFu));
    ra8889_write_register(RA8889_REG_ACTIVE_WINDOW_TOP,   (uint8_t)(y0 & 0xFFu));
    ra8889_write_register(RA8889_REG_ACTIVE_WINDOW_RIGHT, (uint8_t)(x1 & 0xFFu));
    ra8889_write_register(RA8889_REG_ACTIVE_WINDOW_BOTTOM,(uint8_t)(y1 & 0xFFu));
}

void ra8889_set_write_position(uint16_t x, uint16_t y)
{
    ra8889_write_register(RA8889_REG_WRITE_X_LSB, (uint8_t)(x & 0xFFu));
    ra8889_write_register(RA8889_REG_WRITE_X_MSB, (uint8_t)((x >> 8u) & 0xFFu));
    ra8889_write_register(RA8889_REG_WRITE_Y_LSB, (uint8_t)(y & 0xFFu));
    ra8889_write_register(RA8889_REG_WRITE_Y_MSB, (uint8_t)((y >> 8u) & 0xFFu));
}

void board_graphics_driver_buffer_toggle(GX_CANVAS *canvas, GX_RECTANGLE *dirty_area)
{
    if ((canvas == GX_NULL) || (dirty_area == GX_NULL))
    {
        return;
    }

    const uint16_t x0 = (uint16_t)dirty_area->gx_rectangle_left;
    const uint16_t y0 = (uint16_t)dirty_area->gx_rectangle_top;
    const uint16_t x1 = (uint16_t)dirty_area->gx_rectangle_right;
    const uint16_t y1 = (uint16_t)dirty_area->gx_rectangle_bottom;

    const uint16_t width  = (uint16_t)(x1 - x0 + 1u);
    const uint16_t height = (uint16_t)(y1 - y0 + 1u);
    const uint32_t row_stride = (uint32_t)canvas->gx_canvas_x_resolution;

    ra8889_set_active_window(x0, y0, x1, y1);
    ra8889_set_write_position(x0, y0);
    ra8889_spi_write_command(RA8889_REG_MEMORY_DATA_PORT);

    const uint16_t *pixel_row = (const uint16_t *)canvas->gx_canvas_memory;
    pixel_row += (uint32_t)y0 * row_stride + x0;

    for (uint16_t row = 0u; row < height; row++)
    {
        for (uint16_t col = 0u; col < width; col++)
        {
            uint16_t pixel = pixel_row[col];
            uint8_t high_byte = (uint8_t)((pixel >> 8u) & 0xFFu);
            uint8_t low_byte  = (uint8_t)(pixel & 0xFFu);

            ra8889_wait_write_fifo_not_full();
            ra8889_spi_write_data(high_byte);

            ra8889_wait_write_fifo_not_full();
            ra8889_spi_write_data(low_byte);
        }

        pixel_row += row_stride;
    }
}
