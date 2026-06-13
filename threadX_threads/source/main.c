/*
 * Copyright (c) 2013 - 2015, Freescale Semiconductor, Inc.
 * Copyright 2016-2017, 2024 NXP
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "fsl_device_registers.h"
#include "fsl_debug_console.h"
#include "board.h"
#include "app.h"
#include "tx_api.h"

/* GUIX includes */
#include "gx_api.h"
#include "ui_specifications.h"
#include "ui_resources.h"
#include "ui_memory.h"
#include "lpc_guix_display_driver.h"

/*******************************************************************************
 * Definitions
 ******************************************************************************/

#define THREAD_STACK_SIZE      1024U
#define GUIX_THREAD_STACK_SIZE 4096U

/*******************************************************************************
 * Prototypes
 ******************************************************************************/

/*******************************************************************************
 * Variables
 ******************************************************************************/

static TX_THREAD thread_one;
static TX_THREAD thread_two;
static TX_THREAD guix_thread;
static TX_MUTEX counter_mutex;

static ULONG thread_one_stack[THREAD_STACK_SIZE / sizeof(ULONG)];
static ULONG thread_two_stack[THREAD_STACK_SIZE / sizeof(ULONG)];
static ULONG guix_thread_stack[GUIX_THREAD_STACK_SIZE / sizeof(ULONG)];

volatile ULONG shared_counter = 0U;

/* GUIX canvas memory pointer — points to SDRAM, used by ui_specifications.c */
ULONG *HOME_canvas_memory;

/*******************************************************************************
 * Prototypes
 ******************************************************************************/
extern VOID _tx_timer_interrupt(VOID);
/**
 * @brief Configure low-level CPU and timer resources for ThreadX.
 *
 * This function is called by ThreadX before the kernel starts. It sets
 * the SysTick timer and interrupt priorities so the RTOS tick and context
 * switching work correctly on Cortex-M.
 */
VOID _tx_initialize_low_level(VOID)
{
    uint32_t lowest_priority = (1UL << __NVIC_PRIO_BITS) - 1UL;
    uint32_t systick_priority = (lowest_priority > 0U) ? (lowest_priority - 1U) : lowest_priority;

    if (SystemCoreClock == 0U)
    {
        SystemCoreClockUpdate();
    }

    SysTick_Config(SystemCoreClock / TX_TIMER_TICKS_PER_SECOND);
    NVIC_SetPriority(PendSV_IRQn, lowest_priority);
    NVIC_SetPriority(SysTick_IRQn, systick_priority);
}

/**
 * @brief ThreadX SysTick interrupt handler.
 *
 * Forwards the Cortex-M SysTick interrupt to ThreadX so the RTOS tick
 * count and thread time-slicing are maintained.
 */
void SysTick_Handler(void)
{
    _tx_timer_interrupt();
}


static void thread_entry(ULONG thread_input);
static void guix_thread_entry(ULONG thread_input);

VOID tx_application_define(VOID *first_unused_memory);
ULONG app_get_shared_counter(void);

/*******************************************************************************
 * Code
 ******************************************************************************/

static void thread_entry(ULONG thread_input)
{
    for (;;)
    {
        tx_mutex_get(&counter_mutex, TX_WAIT_FOREVER);
        shared_counter++;
        PRINTF("Thread %u: shared_counter = %u\r\n", (unsigned int)thread_input, (unsigned int)shared_counter);
        tx_mutex_put(&counter_mutex);

        tx_thread_sleep(TX_TIMER_TICKS_PER_SECOND / 2U);
    }
}

ULONG app_get_shared_counter(void)
{
    ULONG value;

    tx_mutex_get(&counter_mutex, TX_WAIT_FOREVER);
    value = shared_counter;
    tx_mutex_put(&counter_mutex);

    return value;
}

/**
 * @brief GUIX UI thread entry.
 *
 * Initializes the GUIX system, configures the display with the LPC LCD
 * driver, creates the Studio-generated UI, and starts the GUIX event loop.
 * gx_system_start() does not return — GUIX runs its own event loop
 * internally within this thread context.
 */
static void guix_thread_entry(ULONG thread_input)
{
    GX_WINDOW_ROOT *root;

    TX_PARAMETER_NOT_USED(thread_input);

    /* Point GUIX canvas memory to SDRAM. */
    HOME_canvas_memory = lpc_guix_get_canvas_memory();
    ui_set_home_canvas_memory((GX_COLOR *)HOME_canvas_memory);

    /* Initialize GUIX. */
    gx_system_initialize();

    /* Configure the display: install our LPC LCD driver. */
    gx_studio_display_configure(HOME,
                                lpc_graphics_driver_setup,
                                LANGUAGE_ENGLISH,
                                HOME_THEME_CLASSIC,
                                &root);

    /* Create the Studio-generated homeHindow screen. */
    gx_studio_named_widget_create("homeHindow", (GX_WIDGET *)root, GX_NULL);

    /* Show the root window (makes the canvas visible). */
    gx_widget_show(root);

    /* Start the counter prompt updater.
       GUIX timer events run in the GUI thread, so the prompt is updated safely. */
    ui_init_counter_prompt(root);

    /* Start GUIX — enters the event loop (does not return). */
    gx_system_start();
}

VOID tx_application_define(VOID *first_unused_memory)
{
    TX_PARAMETER_NOT_USED(first_unused_memory);

    tx_mutex_create(&counter_mutex, "counter_mutex", TX_INHERIT);

    tx_thread_create(&thread_one,
                     "thread_one",
                     thread_entry,
                     1,
                     thread_one_stack,
                     sizeof(thread_one_stack),
                     4,
                     4,
                     TX_NO_TIME_SLICE,
                     TX_AUTO_START);

    tx_thread_create(&thread_two,
                     "thread_two",
                     thread_entry,
                     2,
                     thread_two_stack,
                     sizeof(thread_two_stack),
                     5,
                     5,
                     TX_NO_TIME_SLICE,
                     TX_AUTO_START);

    tx_thread_create(&guix_thread,
                     "guix_thread",
                     guix_thread_entry,
                     0,
                     guix_thread_stack,
                     sizeof(guix_thread_stack),
                     6,
                     6,
                     TX_NO_TIME_SLICE,
                     TX_AUTO_START);
}

/*!
 * @brief Main function
 */
int main(void)
{
    /* Init board hardware. */
    BOARD_InitHardware();

    PRINTF("Starting ThreadX demo\r\n");

    tx_kernel_enter();

    return 0;
}

