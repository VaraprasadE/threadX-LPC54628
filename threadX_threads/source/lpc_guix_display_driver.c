/*
 * GUIX display driver for LPC54628 LCD controller (monochrome 1bpp).
 *
 * This module initializes the LPC LCD controller and provides the
 * buffer-toggle callback that GUIX uses to push rendered canvas
 * content to the LCD framebuffer.
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
#define APP_PIXEL_PER_BYTE 8
#define FRAME_BUF_STRIDE   (IMG_WIDTH / APP_PIXEL_PER_BYTE)  /* 60 bytes */

/*******************************************************************************
 * Variables
 ******************************************************************************/

/* LCD framebuffer — 1 bpp, 480x272 = 16320 bytes, aligned to 8 bytes. */
#if (defined(__CC_ARM) || defined(__ARMCC_VERSION) || defined(__GNUC__))
__attribute__((aligned(8)))
#elif defined(__ICCARM__)
#pragma data_alignment = 8
#else
#error Toolchain not supported.
#endif
static uint8_t s_frameBuf[IMG_HEIGHT][FRAME_BUF_STRIDE];

/*
 * Palette for 1bpp mode.
 * Entry 0 = white (background), Entry 1 = black (foreground).
 * Each 32-bit word holds two 16-bit palette entries.
 *   Bits[14:10] = B[4:0], Bits[9:5] = G[4:0], Bits[4:0] = R[4:0]
 *   0x7FFF = white, 0x0000 = black  =>  packed: 0x00007FFF
 */
static const uint32_t s_palette[] = {0x00007FFF};

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
    INT       x_byte_start;
    INT       x_byte_end;
    INT       copy_width;

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

    /* For 1bpp, stride in bytes = width / 8. */
    canvas_stride = canvas->gx_canvas_x_resolution / APP_PIXEL_PER_BYTE;

    /* Compute byte boundaries of the dirty rectangle. */
    x_byte_start = dirty_area->gx_rectangle_left / APP_PIXEL_PER_BYTE;
    x_byte_end   = dirty_area->gx_rectangle_right / APP_PIXEL_PER_BYTE;
    copy_width   = x_byte_end - x_byte_start + 1;

    /* Copy each row of the dirty region. */
    for (y = dirty_area->gx_rectangle_top; y <= dirty_area->gx_rectangle_bottom; y++)
    {
        memcpy(&s_frameBuf[y][x_byte_start],
               &canvas_mem[y * canvas_stride + x_byte_start],
               (size_t)copy_width);
    }
}

/**
 * @brief Initialize the LPC LCD controller hardware.
 *
 * Configures the LCDC for 1bpp TFT mode, sets the palette,
 * and starts the LCD output.
 */
static void lpc_lcd_hw_init(void)
{
    lcdc_config_t lcdConfig;

    /* Clear framebuffer to all-zeros (palette index 0 = white). */
    memset(s_frameBuf, 0, sizeof(s_frameBuf));

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
    lcdConfig.bpp            = kLCDC_1BPP;
    lcdConfig.display        = kLCDC_DisplayTFT;
    lcdConfig.swapRedBlue    = false;

    LCDC_Init(APP_LCD, &lcdConfig, LCD_INPUT_CLK_FREQ);
    LCDC_SetPalette(APP_LCD, s_palette, ARRAY_SIZE(s_palette));

    LCDC_Start(APP_LCD);
    LCDC_PowerUp(APP_LCD);
}

/**
 * @brief GUIX display driver setup for LPC54628.
 *
 * This is the driver callback passed to gx_studio_display_configure().
 * It initializes the LCD hardware, then installs the standard GUIX
 * monochrome (1bpp) display driver with our buffer-toggle function.
 */
UINT lpc_graphics_driver_setup(GX_DISPLAY *display)
{
    /* Initialize LCD controller hardware. */
    lpc_lcd_hw_init();

    /* Install the built-in GUIX monochrome driver and set our toggle. */
    _gx_display_driver_monochrome_setup(display, GX_NULL, lpc_buffer_toggle);

    return GX_SUCCESS;
}
