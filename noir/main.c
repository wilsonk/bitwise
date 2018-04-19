#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <ctype.h>
#include <assert.h>

#include "noir.c"

#include "SDL.h"

int noir_key_to_sdl_scancode[NUM_KEYS] = {
    [KEY_RETURN] = SDL_SCANCODE_RETURN,
    [KEY_SPACE] = SDL_SCANCODE_SPACE,
    [KEY_BACKSPACE] = SDL_SCANCODE_BACKSPACE,
    [KEY_TAB] = SDL_SCANCODE_TAB,
    [KEY_ESCAPE] = SDL_SCANCODE_ESCAPE,
    [KEY_LEFT] = SDL_SCANCODE_LEFT,
    [KEY_RIGHT] = SDL_SCANCODE_RIGHT,
    [KEY_UP] = SDL_SCANCODE_UP,
    [KEY_DOWN] = SDL_SCANCODE_DOWN,
    [KEY_LSHIFT] = SDL_SCANCODE_LSHIFT,
    [KEY_RSHIFT] = SDL_SCANCODE_RSHIFT,
    [KEY_LCTRL] = SDL_SCANCODE_LCTRL,
    [KEY_RCTRL] = SDL_SCANCODE_RCTRL,
    [KEY_LALT] = SDL_SCANCODE_LALT,
    [KEY_RALT] = SDL_SCANCODE_RALT,
};

int sdl_scancode_to_noir_key[SDL_NUM_SCANCODES];

static void sdl_error(const char *name) {
    const char *error = SDL_GetError();
    if (*error) {
        sprintf_s(app.error_buf, sizeof(app.error_buf), "%s: %s", name, error);
        app.error = app.error_buf;
    }
}

static bool init_display(void) {
    float dpi;
    if (SDL_GetDisplayDPI(0, &dpi, NULL, NULL) != 0) {
        sdl_error("SDL_GetDisplayDPI");
        return false;
    }
    app.display.dpi = dpi;

    SDL_DisplayMode mode;
    if (SDL_GetCurrentDisplayMode(0, &mode) != 0) {
        sdl_error("SDL_GetCurrentDisplayMode");
        return false;
    }
    app.display.size.x = mode.w;
    app.display.size.y = mode.h;
    app.display.rate = mode.refresh_rate;
    return true;
}

static void init_keys(void) {
    for (int c = 0; c < 256; c++) {
        if (isprint(c)) {
            char str[] = {c, 0};
            SDL_Scancode scancode = SDL_GetScancodeFromName(str);
            if (scancode != SDL_SCANCODE_UNKNOWN) {
                noir_key_to_sdl_scancode[(unsigned char)c] = scancode;
            }
        }
    }
    for (int key = 0; key < NUM_KEYS; key++) {
        int scancode = noir_key_to_sdl_scancode[key];
        if (scancode) {
            sdl_scancode_to_noir_key[scancode] = key;
        }
    }
}

static bool init_window(void) {
    if (!app.window.title) {
        app.window.title = default_window_title;
    }
    int x = app.window.pos.x == DEFAULT_WINDOW_POS  ? SDL_WINDOWPOS_CENTERED : app.window.pos.x;
    int y = app.window.pos.y == DEFAULT_WINDOW_POS  ? SDL_WINDOWPOS_CENTERED : app.window.pos.y;
    int width = app.window.size.x == 0 ? default_window_size.x : app.window.size.x;
    int height = app.window.size.y == 0 ? default_window_size.y : app.window.size.y;
    SDL_WindowFlags flags = 0;
    if (app.window.resizable) {
        flags |= SDL_WINDOW_RESIZABLE;
    }
    if (app.window.hidden) {
        flags |= SDL_WINDOW_HIDDEN;
    }
    SDL_Window *sdl_window = SDL_CreateWindow(app.window.title, x, y, width, height, flags);
    if (!sdl_window) {
        app.error = "Window creation failed";
        return false;
    }
    app.window.sdl = sdl_window;
    app.window.synced_pos = app.window.pos;
    strcpy_s(app.window.synced_title, sizeof(app.window.synced_title), app.window.title);
    extern void update_window();
    update_window();
    return true;
}

static void init_time(void) {
    app.time.ticks_per_sec = SDL_GetPerformanceFrequency();
    app.time.sdl_start_ticks = SDL_GetPerformanceCounter();
}

static bool check_init(void) {
    if (!app.init) {
        app.error = "Not initialized";
        return false;
    }
    return true;
}

static void update_mouse(void) {
    if (app.mouse.capture != app.mouse.synced_capture) {
        if (SDL_CaptureMouse(app.mouse.capture) < 0) {
            sdl_error("SDL_CaptureMouse");
        }
    }
    app.mouse.synced_capture = app.mouse.capture;

    if (app.mouse.pos.x != app.mouse.synced_pos.x || app.mouse.pos.y != app.mouse.synced_pos.y) {
        SDL_WarpMouseInWindow(NULL, app.mouse.pos.x, app.mouse.pos.y);
    }
    uint32 state = SDL_GetMouseState(&app.mouse.pos.x, &app.mouse.pos.y);
    app.mouse.delta_pos = (int2){app.mouse.pos.x - app.mouse.synced_pos.x, app.mouse.pos.y - app.mouse.synced_pos.y};
    app.mouse.moved = app.mouse.delta_pos.x || app.mouse.delta_pos.y;
    app.mouse.synced_pos = app.mouse.pos;

    if (app.mouse.global_pos.x != app.mouse.synced_global_pos.x || app.mouse.global_pos.y != app.mouse.synced_global_pos.y) {
        SDL_WarpMouseGlobal(app.mouse.global_pos.x, app.mouse.global_pos.y);
    }
    SDL_GetGlobalMouseState(&app.mouse.global_pos.x, &app.mouse.global_pos.y);
    app.mouse.global_delta_pos = (int2){app.mouse.global_pos.x - app.mouse.synced_global_pos.x, app.mouse.global_pos.y - app.mouse.synced_global_pos.y};
    app.mouse.global_moved = app.mouse.global_delta_pos.x || app.mouse.global_delta_pos.y;
    app.mouse.synced_global_pos = app.mouse.global_pos;
}

static void update_events(void) {
    for (int key = 0; key < NUM_KEYS; key++) {
        reset_digital_button_events(&app.keys[key]);
    }

    reset_digital_button_events(&app.mouse.left_button);
    reset_digital_button_events(&app.mouse.middle_button);
    reset_digital_button_events(&app.mouse.right_button);

    char *text_ptr = app.text;
    char *text_end = app.text + sizeof(app.text) - 1;

    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        switch (event.type) {
        case SDL_MOUSEBUTTONDOWN:
        case SDL_MOUSEBUTTONUP:
            if (event.button.button == SDL_BUTTON_LEFT) {
                update_digital_button(&app.mouse.left_button, event.button.state == SDL_PRESSED);
            } else if (event.button.button == SDL_BUTTON_MIDDLE) {
                update_digital_button(&app.mouse.middle_button, event.button.state == SDL_PRESSED);
            } else if (event.button.button == SDL_BUTTON_RIGHT) {
                update_digital_button(&app.mouse.right_button, event.button.state == SDL_PRESSED);
            }
            break;
        case SDL_KEYDOWN:
        case SDL_KEYUP:
            if (!event.key.repeat) {
                int key = sdl_scancode_to_noir_key[event.key.keysym.scancode];
                if (key) {
                    update_digital_button(&app.keys[key], event.key.state == SDL_PRESSED);
                    update_combination_keys();
                }
            }
            break;
        case SDL_TEXTINPUT: {
            const char *str = event.text.text;
            while (*str) {
                if (text_ptr == text_end) {
                    app.error = "Text buffer overflow";
                    break;
                }
                *text_ptr++ = *str++;
            }
            break;
        }
        case SDL_QUIT:
            app.quit = true;
            break;
        }
    }

    *text_ptr = 0;
}

static void update_time(void) {
    uint64 ticks = SDL_GetPerformanceCounter() - app.time.sdl_start_ticks;
    app.time.ticks = ticks;

    app.time.nsecs = (app.time.ticks * 1000 * 1000 * 1000) / app.time.ticks_per_sec;
    app.time.usecs = (app.time.ticks * 1000 * 1000) / app.time.ticks_per_sec;
    app.time.msecs = (app.time.ticks * 1000) / app.time.ticks_per_sec;
    app.time.secs = (double)app.time.ticks / (double)app.time.ticks_per_sec;

    app.time.delta_ticks = (int)(ticks - app.time.ticks);
    app.time.delta_nsecs = (int)((app.time.delta_ticks * 1000 * 1000 * 1000) / app.time.ticks_per_sec);
    app.time.delta_usecs = (int)((app.time.delta_ticks * 1000 * 1000) / app.time.ticks_per_sec);
    app.time.delta_msecs = (int)((app.time.delta_ticks * 1000) / app.time.ticks_per_sec);
    app.time.delta_secs = (float)app.time.delta_ticks / (float)app.time.ticks_per_sec;
}

static void update_window(void) {
    if (app.window.title != app.window.synced_title && strcmp(app.window.title, app.window.synced_title) != 0) {
        SDL_SetWindowTitle(app.window.sdl, app.window.title);
        strcpy_s(app.window.synced_title, sizeof(app.window.synced_title), app.window.title);
        app.window.title = app.window.synced_title;
    }

    if (!int2_eq(app.window.pos, app.window.synced_pos)) {
        SDL_SetWindowPosition(app.window.sdl, app.window.pos.x, app.window.pos.y);
    }
    SDL_GetWindowPosition(app.window.sdl, &app.window.pos.x, &app.window.pos.y);
    app.window.moved = app.num_updates == 0 || !int2_eq(app.window.pos, app.window.synced_pos);
    app.window.synced_pos = app.window.pos;

    if (!int2_eq(app.window.size, app.window.synced_size)) {
        SDL_SetWindowSize(app.window.sdl, app.window.size.x, app.window.size.y);
    }
    SDL_GetWindowSize(app.window.sdl, &app.window.size.x, &app.window.size.y);
    app.window.resized = app.num_updates == 0 || !int2_eq(app.window.size, app.window.synced_size);
    app.window.synced_size = app.window.size;

    if (app.window.resizable != app.window.synced_resizable) {
        SDL_SetWindowResizable(app.window.sdl, app.window.resizable);
    }
    app.window.synced_resizable = app.window.resizable;

    if (app.window.hidden != app.window.synced_hidden) {
        if (app.window.hidden) {
            SDL_HideWindow(app.window.sdl);
        } else {
            SDL_ShowWindow(app.window.sdl);
        }
    }
    app.window.synced_hidden = app.window.hidden;
}

void update_clipboard(void) {
    // TODO: Concerned about performance implications for large clipboard data.
    if (app.clipboard != app.synced_clipboard) {
        SDL_free(app.synced_clipboard);
        app.synced_clipboard = SDL_strdup(app.clipboard);
        app.clipboard = app.synced_clipboard;
        SDL_SetClipboardText(app.clipboard);
    } else {
        if (SDL_HasClipboardText()) {
            char *new_clipboard = SDL_GetClipboardText();
            if (!app.synced_clipboard || strcmp(new_clipboard, app.synced_clipboard) != 0) {
                SDL_free(app.synced_clipboard);
                app.synced_clipboard = new_clipboard;
                app.clipboard = app.synced_clipboard;
            } else {
                SDL_free(new_clipboard);
            }
        }
    }
}

bool init(void) {
    if (app.init) {
        return true;
    }
    if (SDL_Init(SDL_INIT_EVERYTHING) < 0) {
        sdl_error("SDL_Init");
        return false;
    }
    if (!init_display()) {
        return false;
    }
    if (!init_window()) {
        return false;
    }
    init_keys();
    init_time();
    app.init = true;
    return true;
}

bool update(void) {
    if (!check_init()) {
        return false;
    }
    if (!app.error) {
        SDL_ClearError();
    }
    SDL_PumpEvents();
    update_events();
    update_window();
    update_time();
    update_mouse();
    update_clipboard();
    app.num_updates++;
    return !app.quit;
}
