/*
 * GUIX display driver for LPC54628 LCD controller (16bpp 565RGB).
 */
#ifndef _LPC_GUIX_DISPLAY_DRIVER_H_
#define _LPC_GUIX_DISPLAY_DRIVER_H_

#include "gx_api.h"

/*******************************************************************************
 * Prototypes
 ******************************************************************************/

/**
 * @brief GUIX display driver setup function for the LPC54628 LCD controller.
 *
 * This function is passed to gx_studio_display_configure() as the driver
 * setup callback. It initializes the LCD hardware and hooks the 565RGB
 * display driver with a custom buffer-toggle function.
 *
 * @param display  Pointer to the GUIX display control block.
 * @return GX_SUCCESS on success.
 */
UINT lpc_graphics_driver_setup(GX_DISPLAY *display);

/**
 * @brief Get pointer to GUIX canvas memory in SDRAM.
 * @return Pointer to canvas memory (ULONG aligned, in SDRAM).
 */
ULONG *lpc_guix_get_canvas_memory(void);

/**
 * @brief Get canvas memory size in ULONGs.
 * @return Canvas memory size.
 */
ULONG lpc_guix_get_canvas_size(void);

#endif /* _LPC_GUIX_DISPLAY_DRIVER_H_ */
