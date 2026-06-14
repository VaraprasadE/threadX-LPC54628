#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "fsl_device_registers.h"
#include "tx_api.h"
#include "board.h"
#include "fsl_debug_console.h"
#include "fsl_i2c.h"
#include "fsl_iocon.h"
#include "pin_mux.h"
#include "ui_memory.h"
#include "ui_resources.h"
#include "ui_specifications.h"

/* Extern generated display table */
extern GX_STUDIO_DISPLAY_INFO ui_display_table[];
extern ULONG app_get_shared_counter(void);

#define UI_COUNTER_TIMER_ID 1
#define UI_TOUCH_TIMER_ID   2

#define UI_TOUCH_I2C_ADDRESS         0x38U
#define UI_TOUCH_MODE_SUBADDRESS     0x00U
#define UI_TOUCH_DATA_SUBADDRESS     0x01U
#define UI_TOUCH_DATA_LENGTH         0x20U
#define UI_TOUCH_RESET_ASSERT_TICKS  2U
#define UI_TOUCH_RESET_RELEASE_TICKS 6U
#define UI_TOUCH_POLL_TICKS          ((TX_TIMER_TICKS_PER_SECOND >= 50U) ? (TX_TIMER_TICKS_PER_SECOND / 50U) : 1U)

#define UI_TOUCH_I2C_DRIVE_HIGH      0x0400U
#define UI_TOUCH_I2C_FILTER_ENABLE   0x0800U

#define TOUCH_POINT_GET_EVENT(touch_point) ((UINT)((touch_point).xh >> 6))
#define TOUCH_POINT_GET_X(touch_point)     ((GX_VALUE)((((UINT)(touch_point).xh & 0x0FU) << 8) | (touch_point).xl))
#define TOUCH_POINT_GET_Y(touch_point)     ((GX_VALUE)((((UINT)(touch_point).yh & 0x0FU) << 8) | (touch_point).yl))

typedef struct ui_touch_point_struct
{
    uint8_t xh;
    uint8_t xl;
    uint8_t yh;
    uint8_t yl;
    uint8_t reserved[2];
} ui_touch_point_t;

typedef struct ui_touch_data_struct
{
    uint8_t gesture_id;
    uint8_t touch_count;
    ui_touch_point_t touch[5];
} ui_touch_data_t;

static char s_counter_text[32];
static char s_empty_text[] = "";
static char s_start_text[] = "START";
static char s_stop_text[] = "STOP";

static bool s_counter_visible;
static bool s_touch_ready;
static bool s_touch_pressed;
static GX_POINT s_last_touch_point;

static void ui_touch_pinmux_init(void);
static status_t ui_touch_hw_init(void);
static status_t ui_touch_controller_init(void);
static void ui_set_counter_running(bool running);
static void ui_send_pen_event(USHORT event_type, GX_VALUE x, GX_VALUE y);
static void ui_poll_touch_controller(void);

VOID ui_set_home_canvas_memory(GX_COLOR *memory)
{
    ui_display_table[HOME].canvas_memory = memory;
}

VOID ui_update_counter_prompt(ULONG counter)
{
    GX_STRING string;

    snprintf(s_counter_text, sizeof(s_counter_text), "%u", (unsigned int)counter);
    string.gx_string_ptr = s_counter_text;
    string.gx_string_length = (UINT)strlen(s_counter_text);
    gx_prompt_text_set_ext(&homeHindow.homeHindow_counterPrompt, &string);
}

static void ui_set_prompt_text(char *text)
{
    GX_STRING string;

    string.gx_string_ptr = text;
    string.gx_string_length = (UINT)strlen(text);
    gx_prompt_text_set_ext(&homeHindow.homeHindow_counterPrompt, &string);
}

static void ui_set_button_text(char *text)
{
    GX_STRING string;

    string.gx_string_ptr = text;
    string.gx_string_length = (UINT)strlen(text);
    gx_text_button_text_set_ext(&homeHindow.homeHindow_startButton, &string);
}

static void ui_touch_pinmux_init(void)
{
    const uint32_t touch_i2c_pin_config = IOCON_PIO_FUNC1 |
                                          IOCON_PIO_INV_DI |
                                          IOCON_PIO_DIGITAL_EN |
                                          IOCON_PIO_INPFILT_OFF |
                                          UI_TOUCH_I2C_DRIVE_HIGH |
                                          UI_TOUCH_I2C_FILTER_ENABLE;
    const uint32_t touch_gpio_pin_config = IOCON_FUNC0 |
                                           IOCON_PIO_MODE_INACT |
                                           IOCON_PIO_DIGITAL_EN |
                                           IOCON_PIO_INPFILT_OFF |
                                           IOCON_PIO_OPENDRAIN_DI;

    CLOCK_EnableClock(kCLOCK_Iocon);

    IOCON_PinMuxSet(IOCON, 3U, 23U, touch_i2c_pin_config);
    IOCON_PinMuxSet(IOCON, 3U, 24U, touch_i2c_pin_config);
    IOCON_PinMuxSet(IOCON, 2U, 27U, touch_gpio_pin_config);
    IOCON_PinMuxSet(IOCON, 4U, 0U, touch_gpio_pin_config);
}

static status_t ui_touch_hw_init(void)
{
    gpio_pin_config_t reset_pin_config = {kGPIO_DigitalOutput, 1U};
    gpio_pin_config_t int_pin_config = {kGPIO_DigitalInput, 0U};
    i2c_master_config_t i2c_config;

    ui_touch_pinmux_init();

    CLOCK_AttachClk(kFRO12M_to_FLEXCOMM2);
    I2C_MasterGetDefaultConfig(&i2c_config);
    I2C_MasterInit(BOARD_TOUCH_I2C_BASEADDR, &i2c_config, CLOCK_GetFlexCommClkFreq(2U));

    GPIO_PinInit(BOARD_TOUCH_RST_GPIO, BOARD_TOUCH_RST_PORT, BOARD_TOUCH_RST_PIN, &reset_pin_config);
    GPIO_PinInit(BOARD_TOUCH_INT_GPIO, BOARD_TOUCH_INT_PORT, BOARD_TOUCH_INT_PIN, &int_pin_config);

    GPIO_PinWrite(BOARD_TOUCH_RST_GPIO, BOARD_TOUCH_RST_PORT, BOARD_TOUCH_RST_PIN, 0U);
    tx_thread_sleep(UI_TOUCH_RESET_ASSERT_TICKS);
    GPIO_PinWrite(BOARD_TOUCH_RST_GPIO, BOARD_TOUCH_RST_PORT, BOARD_TOUCH_RST_PIN, 1U);
    tx_thread_sleep(UI_TOUCH_RESET_RELEASE_TICKS);

    return kStatus_Success;
}

static status_t ui_touch_controller_init(void)
{
    i2c_master_transfer_t transfer;
    uint8_t mode = 0U;

    memset(&transfer, 0, sizeof(transfer));
    transfer.slaveAddress = UI_TOUCH_I2C_ADDRESS;
    transfer.direction = kI2C_Write;
    transfer.subaddress = UI_TOUCH_MODE_SUBADDRESS;
    transfer.subaddressSize = 1U;
    transfer.data = &mode;
    transfer.dataSize = 1U;
    transfer.flags = kI2C_TransferDefaultFlag;

    return I2C_MasterTransferBlocking(BOARD_TOUCH_I2C_BASEADDR, &transfer);
}

static void ui_set_counter_running(bool running)
{
    if (running)
    {
        ui_update_counter_prompt(app_get_shared_counter());
        gx_widget_show(&homeHindow.homeHindow_counterPrompt);
        gx_system_timer_start(&homeHindow,
                              UI_COUNTER_TIMER_ID,
                              TX_TIMER_TICKS_PER_SECOND,
                              TX_TIMER_TICKS_PER_SECOND);
        ui_set_button_text(s_stop_text);
    }
    else
    {
        gx_system_timer_stop(&homeHindow, UI_COUNTER_TIMER_ID);
        ui_set_prompt_text(s_empty_text);
        gx_widget_hide(&homeHindow.homeHindow_counterPrompt);
        ui_set_button_text(s_start_text);
    }

    s_counter_visible = running;
}

static void ui_send_pen_event(USHORT event_type, GX_VALUE x, GX_VALUE y)
{
    GX_EVENT event;

    memset(&event, 0, sizeof(event));
    event.gx_event_type = event_type;
    event.gx_event_payload.gx_event_pointdata.gx_point_x = x;
    event.gx_event_payload.gx_event_pointdata.gx_point_y = y;
    gx_system_event_send(&event);
}

static void ui_poll_touch_controller(void)
{
    i2c_master_transfer_t transfer;
    uint8_t raw_data[UI_TOUCH_DATA_LENGTH] = {0};
    ui_touch_data_t *touch_data = (ui_touch_data_t *)(void *)raw_data;
    GX_VALUE x;
    GX_VALUE y;
    UINT touch_event;

    if (!s_touch_ready)
    {
        return;
    }

    memset(&transfer, 0, sizeof(transfer));
    transfer.slaveAddress = UI_TOUCH_I2C_ADDRESS;
    transfer.direction = kI2C_Read;
    transfer.subaddress = UI_TOUCH_DATA_SUBADDRESS;
    transfer.subaddressSize = 1U;
    transfer.data = raw_data;
    transfer.dataSize = sizeof(raw_data);
    transfer.flags = kI2C_TransferDefaultFlag;

    if (I2C_MasterTransferBlocking(BOARD_TOUCH_I2C_BASEADDR, &transfer) != kStatus_Success)
    {
        return;
    }

    if ((touch_data->touch_count & 0x0FU) == 0U)
    {
        if (s_touch_pressed)
        {
            ui_send_pen_event(GX_EVENT_PEN_UP, s_last_touch_point.gx_point_x, s_last_touch_point.gx_point_y);
            s_touch_pressed = false;
        }
        return;
    }

    touch_event = TOUCH_POINT_GET_EVENT(touch_data->touch[0]);
    x = TOUCH_POINT_GET_Y(touch_data->touch[0]);
    y = TOUCH_POINT_GET_X(touch_data->touch[0]);

    if (!s_touch_pressed || (touch_event == 0U))
    {
        ui_send_pen_event(GX_EVENT_PEN_DOWN, x, y);
        s_touch_pressed = true;
    }
    else if ((x != s_last_touch_point.gx_point_x) || (y != s_last_touch_point.gx_point_y) || (touch_event == 2U))
    {
        ui_send_pen_event(GX_EVENT_PEN_DRAG, x, y);
    }

    s_last_touch_point.gx_point_x = x;
    s_last_touch_point.gx_point_y = y;
}

static UINT ui_counter_timer_event_process(GX_WIDGET *widget, GX_EVENT *event_ptr)
{
    switch (event_ptr->gx_event_type)
    {
        case GX_EVENT_TIMER:
            if (event_ptr->gx_event_payload.gx_event_timer_id == UI_COUNTER_TIMER_ID)
            {
                if (s_counter_visible)
                {
                    ui_update_counter_prompt(app_get_shared_counter());
                }
            }
            else if (event_ptr->gx_event_payload.gx_event_timer_id == UI_TOUCH_TIMER_ID)
            {
                ui_poll_touch_controller();
            }
            break;

        case GX_SIGNAL(ID_START_BTN, GX_EVENT_CLICKED):
            ui_set_counter_running(!s_counter_visible);
            return GX_SUCCESS;

        default:
            break;
    }

    return gx_window_event_process((GX_WINDOW *)widget, event_ptr);
}

VOID ui_init_counter_prompt(GX_WINDOW_ROOT *root)
{
    TX_PARAMETER_NOT_USED(root);

    gx_widget_show(&homeHindow.homeHindow_startButton);
    gx_widget_event_process_set(&homeHindow, ui_counter_timer_event_process);
    ui_set_counter_running(false);

    if ((ui_touch_hw_init() == kStatus_Success) &&
        (ui_touch_controller_init() == kStatus_Success))
    {
        s_touch_ready = true;
        gx_system_timer_start(&homeHindow,
                              UI_TOUCH_TIMER_ID,
                              UI_TOUCH_POLL_TICKS,
                              UI_TOUCH_POLL_TICKS);
    }
    else
    {
        PRINTF("Touch controller initialization failed\r\n");
    }
}
