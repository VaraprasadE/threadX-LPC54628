/*
 * GUIX display driver for LPC54628 LCD controller (16bpp 565RGB).
 *
 * This module initializes the LPC LCD controller in 16bpp mode and provides
 * the buffer-toggle callback that GUIX uses to push rendered canvas
 * content to the LCD framebuffer located in external SDRAM.
 */

#include <string.h>
#include "board.h"
#include "app.h"
#include "fsl_lcdc.h"
#include "gx_api.h"
#include "gx_display.h"
#include "lpc_guix_display_driver.h"

/*******************************************************************************
 * Definitions
 ******************************************************************************/
#define BYTES_PER_PIXEL    2U
#define FRAME_BUF_STRIDE   (IMG_WIDTH * BYTES_PER_PIXEL)  /* 960 bytes per row */
#define FRAME_BUF_SIZE     (IMG_WIDTH * IMG_HEIGHT * BYTES_PER_PIXEL) /* 261120 bytes */

/*
 * SDRAM memory layout:
 *   0xA0000000 + 0x000000 : LCD framebuffer (261,120 bytes)
 *   0xA0000000 + 0x040000 : GUIX canvas memory (261,120 bytes)
 *   Total: ~512 KB out of 8 MB SDRAM
 */
#define LCD_FB_ADDR        (SDRAM_BASE_ADDR)
#define GUIX_CANVAS_ADDR   (SDRAM_BASE_ADDR + 0x00040000U)

/*******************************************************************************
 * Variables
 ******************************************************************************/

/* Pointers to framebuffer and canvas in SDRAM. */
static uint16_t * const s_frameBuf   = (uint16_t *)LCD_FB_ADDR;
static ULONG   * const s_canvasMem  = (ULONG *)GUIX_CANVAS_ADDR;

/* Canvas size in ULONGs for GUIX: 480*272*2 / 4 = 65280 */
#define CANVAS_MEM_ULONGS  (FRAME_BUF_SIZE / sizeof(ULONG))

/*******************************************************************************
 * Code
 ******************************************************************************/

/**
 * @brief Copy dirty region from GUIX canvas to LCD framebuffer.
 *
 * Called by GUIX after rendering completes. Copies only the dirty
 * rectangle from the canvas memory to the LCD framebuffer.
 */
static void lpc_buffer_toggle(struct GX_CANVAS_STRUCT *canvas,
                               GX_RECTANGLE *dirty_area)
{
    GX_UBYTE *canvas_mem;
    INT       canvas_stride;
    INT       y;
    INT       x_start_bytes;
    INT       copy_width_bytes;

    if (canvas == GX_NULL || dirty_area == GX_NULL)
    {
        return;
    }

    /* Get the canvas memory pointer. */
    canvas_mem = (GX_UBYTE *)canvas->gx_canvas_memory;
    if (canvas_mem == GX_NULL)
    {
        return;
    }

    /* For 16bpp, stride in bytes = width * 2. */
    canvas_stride = (INT)(canvas->gx_canvas_x_resolution * BYTES_PER_PIXEL);

    /* Compute byte boundaries of the dirty rectangle. */
    x_start_bytes   = dirty_area->gx_rectangle_left * (INT)BYTES_PER_PIXEL;
    copy_width_bytes = (dirty_area->gx_rectangle_right - dirty_area->gx_rectangle_left + 1) * (INT)BYTES_PER_PIXEL;

    /* Copy each row of the dirty region. */
    for (y = dirty_area->gx_rectangle_top; y <= dirty_area->gx_rectangle_bottom; y++)
    {
        memcpy((uint8_t *)s_frameBuf + y * FRAME_BUF_STRIDE + x_start_bytes,
               canvas_mem + y * canvas_stride + x_start_bytes,
               (size_t)copy_width_bytes);
    }
}

/**
 * @brief Initialize the LPC LCD controller hardware.
 *
 * Configures the LCDC for 16bpp TFT mode with the framebuffer
 * in external SDRAM.
 */
static void lpc_lcd_hw_init(void)
{
    lcdc_config_t lcdConfig;

    /* Clear framebuffer to black. */
    memset(s_frameBuf, 0, FRAME_BUF_SIZE);

    LCDC_GetDefaultConfig(&lcdConfig);

    lcdConfig.panelClock_Hz  = LCD_PANEL_CLK;
    lcdConfig.ppl            = LCD_PPL;
    lcdConfig.hsw            = LCD_HSW;
    lcdConfig.hfp            = LCD_HFP;
    lcdConfig.hbp            = LCD_HBP;
    lcdConfig.lpp            = LCD_LPP;
    lcdConfig.vsw            = LCD_VSW;
    lcdConfig.vfp            = LCD_VFP;
    lcdConfig.vbp            = LCD_VBP;
    lcdConfig.polarityFlags  = LCD_POL_FLAGS;
    lcdConfig.upperPanelAddr = (uint32_t)s_frameBuf;
    lcdConfig.bpp            = kLCDC_16BPP565;
    lcdConfig.display        = kLCDC_DisplayTFT;
    lcdConfig.swapRedBlue    = true;
    lcdConfig.dataFormat     = kLCDC_WinCeMode;

    LCDC_Init(APP_LCD, &lcdConfig, LCD_INPUT_CLK_FREQ);

    /* No palette needed for 16bpp. */

    LCDC_Start(APP_LCD);
    LCDC_PowerUp(APP_LCD);
}

/**
 * @brief Return pointer to GUIX canvas memory in SDRAM.
 */
ULONG *lpc_guix_get_canvas_memory(void)
{
    /* Clear canvas memory. */
    memset(s_canvasMem, 0, FRAME_BUF_SIZE);
    return s_canvasMem;
}

/**
 * @brief Return canvas memory size in ULONGs.
 */
ULONG lpc_guix_get_canvas_size(void)
{
    return CANVAS_MEM_ULONGS;
}

/**
 * @brief GUIX display driver setup for LPC54628 (16bpp 565RGB).
 *
 * This is the driver callback passed to gx_studio_display_configure().
 * It initializes the LCD hardware, then installs the standard GUIX
 * 565RGB (16bpp) display driver with our buffer-toggle function.
 */
UINT lpc_graphics_driver_setup(GX_DISPLAY *display)
{
    /* Initialize LCD controller hardware. */
    lpc_lcd_hw_init();

    /* Install the built-in GUIX 565RGB driver and set our toggle. */
    _gx_display_driver_565rgb_setup(display, GX_NULL, lpc_buffer_toggle);

    return GX_SUCCESS;
}
