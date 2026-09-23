#ifndef MMT8_GUI_H
#define MMT8_GUI_H

#include <SDL2/SDL.h>
#include "emu8051.h"

/* Initialize SDL window and renderer. Returns 0 on success. */
int gui_init(void);

/* Process an SDL event (mouse clicks → key_matrix updates) */
void gui_handle_event(SDL_Event *ev);

/* Render the GUI: LCD, LEDs, buttons */
void gui_render(struct em8051 *cpu);

/* Clean up SDL resources */
void gui_shutdown(void);

/* Save the current frame as a BMP file. Returns 0 on success. */
int  gui_screenshot(const char *path);

#endif /* MMT8_GUI_H */
