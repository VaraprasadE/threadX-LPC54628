#include <stdio.h>
#include <string.h>
#include "tx_api.h"
#include "ui_memory.h"
#include "ui_resources.h"
#include "ui_specifications.h"

/* Extern generated display table */
extern GX_STUDIO_DISPLAY_INFO ui_display_table[];
extern ULONG app_get_shared_counter(void);

#define UI_COUNTER_TIMER_ID 1

static char s_counter_text[32];

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

static UINT ui_counter_timer_event_process(GX_WIDGET *widget, GX_EVENT *event_ptr)
{
    if (event_ptr->gx_event_type == GX_EVENT_TIMER &&
        event_ptr->gx_event_payload.gx_event_timer_id == UI_COUNTER_TIMER_ID)
    {
        ui_update_counter_prompt(app_get_shared_counter());
    }

    return gx_window_event_process((GX_WINDOW *)widget, event_ptr);
}

VOID ui_init_counter_prompt(GX_WINDOW_ROOT *root)
{
    gx_widget_event_process_set(root, ui_counter_timer_event_process);
    gx_system_timer_start(root,
                          UI_COUNTER_TIMER_ID,
                          TX_TIMER_TICKS_PER_SECOND,
                          TX_TIMER_TICKS_PER_SECOND);
    ui_update_counter_prompt(app_get_shared_counter());
}
