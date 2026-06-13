#ifndef UI_MEMORY_H_
#define UI_MEMORY_H_

#include "gx_api.h"

#ifdef __cplusplus
extern "C" {
#endif

VOID ui_set_home_canvas_memory(GX_COLOR *memory);
VOID ui_init_counter_prompt(GX_WINDOW_ROOT *root);
VOID ui_update_counter_prompt(ULONG counter);

#ifdef __cplusplus
}
#endif

#endif /* UI_MEMORY_H_ */
