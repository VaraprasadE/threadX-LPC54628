#include <stdbool.h>
#include <stdint.h>
#include "board.h"
#include "fsl_device_registers.h"
#include "fsl_clock.h"
#include "fsl_debug_console.h"
#include "fsl_lpspi.h"
#include "gx_api.h"
#include "gx_display.h"

static bool board_graphics_driver_spi_initialized = false;
static uint32_t board_graphics_driver_flush_count = 0u;
static uint32_t board_graphics_driver_pixel_count = 0u;
static uint32_t board_graphics_driver_byte_count = 0u;

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

static status_t board_graphics_driver_spi_transfer(const uint8_t *txData, uint8_t *rxData, size_t len)
{
    lpspi_transfer_t transfer;
    transfer.txData = txData;
    transfer.rxData = rxData;
    transfer.dataSize = len;
    transfer.configFlags = kLPSPI_MasterPcs0;

    return LPSPI_MasterTransferBlocking(BOARD_GRAPHICS_LPSPI_BASEADDR, &transfer);
}

static status_t board_graphics_driver_spi_init(void)
{
    if (board_graphics_driver_spi_initialized)
    {
        return kStatus_Success;
    }

    lpspi_master_config_t masterConfig;
    LPSPI_MasterGetDefaultConfig(&masterConfig);
    masterConfig.baudRate = BOARD_GRAPHICS_LPSPI_BAUDRATE;
    masterConfig.bitsPerFrame = 8U;
    masterConfig.cpol = kLPSPI_ClockPolarityActiveHigh;
    masterConfig.cpha = kLPSPI_ClockPhaseSecondEdge;
    masterConfig.direction = kLPSPI_MsbFirst;
    masterConfig.whichPcs = kLPSPI_Pcs0;
    masterConfig.pcsActiveHighOrLow = kLPSPI_PcsActiveLow;
    masterConfig.pinCfg = kLPSPI_SdiInSdoOut;
    masterConfig.pcsFunc = kLPSPI_PcsAsCs;
    masterConfig.dataOutConfig = kLpspiDataOutRetained;
    masterConfig.enableInputDelay = false;

    LPSPI_MasterInit(BOARD_GRAPHICS_LPSPI_BASEADDR, &masterConfig, BOARD_GRAPHICS_LPSPI_CLK_FREQ);
    board_graphics_driver_spi_initialized = true;
    PRINTF("RA8889 SPI initialized in Mode 3, 8-bit transfers\r\n");

    return kStatus_Success;
}

static status_t ra8889_spi_write_command(uint8_t reg_addr)
{
    uint8_t tx[2] = {RA8889_CMD_HEADER_COMMAND_WRITE, reg_addr};
    return board_graphics_driver_spi_transfer(tx, NULL, 2);
}

static status_t ra8889_spi_write_data(uint8_t data)
{
    uint8_t tx[2] = {RA8889_CMD_HEADER_DATA_WRITE, data};
    return board_graphics_driver_spi_transfer(tx, NULL, 2);
}

static status_t ra8889_spi_read_status(uint8_t *status)
{
    uint8_t tx[2] = {RA8889_CMD_HEADER_STATUS_READ, 0x00u};
    uint8_t rx[2] = {0};
    status_t result = board_graphics_driver_spi_transfer(tx, rx, 2);
    if (result == kStatus_Success)
    {
        *status = rx[1];
    }
    return result;
}

static status_t ra8889_wait_write_fifo_not_full(void)
{
    const uint32_t timeout_limit = 100000u;
    uint32_t timeout = 0u;
    uint8_t status = 0u;

    while (timeout < timeout_limit)
    {
        status_t result = ra8889_spi_read_status(&status);
        if (result != kStatus_Success)
        {
            return result;
        }

        if ((status & 0x80u) == 0u)
        {
            return kStatus_Success;
        }

        timeout++;
    }

    return kStatus_Timeout;
}

static status_t ra8889_write_register(uint8_t reg, uint8_t data)
{
    status_t status = ra8889_spi_write_command(reg);
    if (status != kStatus_Success)
    {
        return status;
    }

    return ra8889_spi_write_data(data);
}

static status_t ra8889_set_active_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
    status_t status;

    status = ra8889_write_register(RA8889_REG_ACTIVE_WINDOW_LEFT,  (uint8_t)(x0 & 0xFFu));
    if (status != kStatus_Success) return status;

    status = ra8889_write_register(RA8889_REG_ACTIVE_WINDOW_TOP,   (uint8_t)(y0 & 0xFFu));
    if (status != kStatus_Success) return status;

    status = ra8889_write_register(RA8889_REG_ACTIVE_WINDOW_RIGHT, (uint8_t)(x1 & 0xFFu));
    if (status != kStatus_Success) return status;

    return ra8889_write_register(RA8889_REG_ACTIVE_WINDOW_BOTTOM, (uint8_t)(y1 & 0xFFu));
}

static status_t ra8889_set_write_position(uint16_t x, uint16_t y)
{
    status_t status;

    status = ra8889_write_register(RA8889_REG_WRITE_X_LSB, (uint8_t)(x & 0xFFu));
    if (status != kStatus_Success) return status;

    status = ra8889_write_register(RA8889_REG_WRITE_X_MSB, (uint8_t)((x >> 8u) & 0xFFu));
    if (status != kStatus_Success) return status;

    status = ra8889_write_register(RA8889_REG_WRITE_Y_LSB, (uint8_t)(y & 0xFFu));
    if (status != kStatus_Success) return status;

    return ra8889_write_register(RA8889_REG_WRITE_Y_MSB, (uint8_t)((y >> 8u) & 0xFFu));
}

static status_t ra8889_write_pixel(uint16_t pixel)
{
    status_t status = ra8889_wait_write_fifo_not_full();
    if (status != kStatus_Success)
    {
        return status;
    }

    uint8_t high_byte = (uint8_t)((pixel >> 8u) & 0xFFu);
    status = ra8889_spi_write_data(high_byte);
    if (status != kStatus_Success)
    {
        return status;
    }

    status = ra8889_wait_write_fifo_not_full();
    if (status != kStatus_Success)
    {
        return status;
    }

    uint8_t low_byte = (uint8_t)(pixel & 0xFFu);
    return ra8889_spi_write_data(low_byte);
}

static void board_graphics_driver_buffer_toggle(GX_CANVAS *canvas, GX_RECTANGLE *dirty_area)
{
    if ((canvas == GX_NULL) || (dirty_area == GX_NULL))
    {
        return;
    }

    if (!board_graphics_driver_spi_initialized)
    {
        if (board_graphics_driver_spi_init() != kStatus_Success)
        {
            return;
        }
    }

    const uint16_t *canvas_pixels = (const uint16_t *)canvas->gx_canvas_memory;
    const uint32_t stride_pixels = (uint32_t)canvas->gx_canvas_x_resolution;
    const uint16_t x0 = (uint16_t)dirty_area->gx_rectangle_left;
    const uint16_t y0 = (uint16_t)dirty_area->gx_rectangle_top;
    const uint16_t x1 = (uint16_t)dirty_area->gx_rectangle_right;
    const uint16_t y1 = (uint16_t)dirty_area->gx_rectangle_bottom;
    const uint16_t width  = (uint16_t)(x1 - x0 + 1u);
    const uint16_t height = (uint16_t)(y1 - y0 + 1u);
    const uint32_t pixel_count = (uint32_t)width * (uint32_t)height;

    board_graphics_driver_flush_count++;
    board_graphics_driver_pixel_count += pixel_count;
    board_graphics_driver_byte_count += pixel_count * 2u;

    PRINTF("RA8889 flush: area=(%u,%u)-(%u,%u) size=%ux%u pixels=%u bytes=%u\r\n",
           (unsigned)x0, (unsigned)y0, (unsigned)x1, (unsigned)y1,
           (unsigned)width, (unsigned)height,
           (unsigned)pixel_count,
           (unsigned)(pixel_count * 2u));

    if (ra8889_set_active_window(x0, y0, x1, y1) != kStatus_Success)
    {
        PRINTF("RA8889 set active window failed\r\n");
        return;
    }

    if (ra8889_set_write_position(x0, y0) != kStatus_Success)
    {
        PRINTF("RA8889 set write position failed\r\n");
        return;
    }

    if (ra8889_spi_write_command(RA8889_REG_MEMORY_DATA_PORT) != kStatus_Success)
    {
        PRINTF("RA8889 select memory data port failed\r\n");
        return;
    }

    const uint16_t *row_ptr = canvas_pixels + ((uint32_t)y0 * stride_pixels) + x0;

    for (uint16_t row = 0u; row < height; row++)
    {
        for (uint16_t col = 0u; col < width; col++)
        {
            if (ra8889_write_pixel(row_ptr[col]) != kStatus_Success)
            {
                PRINTF("RA8889 pixel write failed at row=%u col=%u\r\n", (unsigned)row, (unsigned)col);
                return;
            }
        }
        row_ptr += stride_pixels;
    }
}

bool board_graphics_driver_is_spi_initialized(void)
{
    return board_graphics_driver_spi_initialized;
}

uint32_t board_graphics_driver_get_flush_count(void)
{
    return board_graphics_driver_flush_count;
}

uint32_t board_graphics_driver_get_pixel_count(void)
{
    return board_graphics_driver_pixel_count;
}

uint32_t board_graphics_driver_get_byte_count(void)
{
    return board_graphics_driver_byte_count;
}

/*
 * Minimal graphics driver setup for GUIX with RA8889 SPI transport.
 */
UINT board_graphics_driver_setup_565rgb(GX_DISPLAY *display)
{
    if (board_graphics_driver_spi_init() != kStatus_Success)
    {
        return GX_SYSTEM_ERROR;
    }

    _gx_display_driver_565rgb_setup(display, GX_NULL, board_graphics_driver_buffer_toggle);
    return GX_SUCCESS;
}
