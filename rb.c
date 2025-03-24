#include <furi.h>
#include <furi_hal.h>
#include <gui/gui.h>
#include <input/input.h>
#include <stdlib.h>
#include <gui/elements.h>
#include <furi_hal_gpio.h>

#include "rb_icons.h"

typedef enum {
    EventTypeTick,
    EventTypeKey,
} EventType;

typedef struct {
    EventType type;
    InputEvent input;
} PluginEvent;

typedef struct {
    FuriMutex* mutex;
    bool active;
    uint8_t pattern_index;
    uint32_t last_tick;
    uint8_t step;
    bool frame_toggle;
    uint32_t frame_tick;
} PluginState;

#define PATTERN_COUNT 3

// Helper macros for pin access
#define RED_PIN (&gpio_ext_pb2)
#define BLUE_PIN (&gpio_ext_pc3)

// Forward declaration for patterns
void pattern_red_blue(PluginState* state);
void pattern_red_blue_alt(PluginState* state);
void pattern_purple_flash(PluginState* state);

// Array of pattern function pointers
void (*patterns[PATTERN_COUNT])(PluginState* state) = {
    pattern_red_blue,
    pattern_red_blue_alt,
    pattern_purple_flash,
};

const char* pattern_names[PATTERN_COUNT] = {
    "Red/Blue",
    "Alt Blink",
    "Purple Flash",
};

// Pattern 0: Basic Red/Blue alternating 250ms
void pattern_red_blue(PluginState* state) {
    const uint32_t interval = 250;

    if (furi_get_tick() - state->last_tick >= interval) {
        state->last_tick = furi_get_tick();
        state->step = !state->step;

        furi_hal_gpio_write(RED_PIN, state->step);
        furi_hal_gpio_write(BLUE_PIN, !state->step);
    }
}

// Pattern 1: Alternating blinking (Red, Off, Blue, Off)
void pattern_red_blue_alt(PluginState* state) {
    const uint32_t interval = 250;

    if (furi_get_tick() - state->last_tick >= interval) {
        state->last_tick = furi_get_tick();
        state->step = (state->step + 1) % 4;

        furi_hal_gpio_write(RED_PIN, state->step == 0);
        furi_hal_gpio_write(BLUE_PIN, state->step == 2);
    }
}

// Pattern 2: Both on (purple) flash every 500ms
void pattern_purple_flash(PluginState* state) {
    const uint32_t interval = 500;

    if (furi_get_tick() - state->last_tick >= interval) {
        state->last_tick = furi_get_tick();
        state->step = !state->step;

        furi_hal_gpio_write(RED_PIN, state->step);
        furi_hal_gpio_write(BLUE_PIN, state->step);
    }
}

static void turn_off_all() {
    furi_hal_gpio_init(RED_PIN, GpioModeOutputPushPull, GpioPullNo, GpioSpeedVeryHigh);
    furi_hal_gpio_init(BLUE_PIN, GpioModeOutputPushPull, GpioPullNo, GpioSpeedVeryHigh);

    furi_hal_gpio_write(RED_PIN, false);
    furi_hal_gpio_write(BLUE_PIN, false);
}


static void render_callback(Canvas* const canvas, void* ctx) {
    PluginState* state = ctx;
    furi_mutex_acquire(state->mutex, FuriWaitForever);

    canvas_draw_icon(canvas, 0, 22, &I_cop);
    
    canvas_set_font(canvas, FontPrimary);
    char pattern[64];
    snprintf(pattern, sizeof(pattern), "%s", pattern_names[state->pattern_index]);
    elements_multiline_text_aligned(canvas, 64, 4, AlignCenter, AlignTop, pattern);

    uint32_t now = furi_get_tick();
    if(state->active) {
        if(now - state->frame_tick >= 300) {  // 300ms per frame = ~3.3 FPS
            state->frame_toggle = !state->frame_toggle;
            state->frame_tick = now;
        }

        if(state->frame_toggle) {
            canvas_draw_icon(canvas, 0, 17, &I_led_connections_on);
        } else {
            canvas_draw_icon(canvas, 0, 17, &I_led_connections);
        }
    } else {
        canvas_draw_icon(canvas, 0, 17, &I_led_connections);
    }

    furi_mutex_release(state->mutex);
}

static void input_callback(InputEvent* input_event, void* ctx) {
    FuriMessageQueue* queue = ctx;
    PluginEvent event = {.type = EventTypeKey, .input = *input_event};
    furi_message_queue_put(queue, &event, FuriWaitForever);
}

int32_t rb_app() {
    FuriMessageQueue* queue = furi_message_queue_alloc(8, sizeof(PluginEvent));
    PluginState* state = malloc(sizeof(PluginState));
    state->mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    state->active = false;
    state->pattern_index = 0;
    state->last_tick = furi_get_tick();
    state->step = 0;
    state->frame_tick = furi_get_tick();
    state->frame_toggle = false;


    ViewPort* view_port = view_port_alloc();
    view_port_draw_callback_set(view_port, render_callback, state);
    view_port_input_callback_set(view_port, input_callback, queue);

    Gui* gui = furi_record_open(RECORD_GUI);
    gui_add_view_port(gui, view_port, GuiLayerFullscreen);

    PluginEvent event;
    bool running = true;

    while(running) {
        // Poll input or tick
        if(furi_message_queue_get(queue, &event, 100) == FuriStatusOk) {
            furi_mutex_acquire(state->mutex, FuriWaitForever);
            if(event.type == EventTypeKey && event.input.type == InputTypePress) {
                switch(event.input.key) {
                    case InputKeyOk:
                        state->active = !state->active;
                        state->step = 0;
                        turn_off_all();
                        state->last_tick = furi_get_tick();
                        break;
                    case InputKeyRight:
                        state->pattern_index = (state->pattern_index + 1) % PATTERN_COUNT;
                        state->step = 0;
                        turn_off_all();
                        break;
                    case InputKeyLeft:
                        state->pattern_index = (state->pattern_index + PATTERN_COUNT - 1) % PATTERN_COUNT;
                        state->step = 0;
                        turn_off_all();
                        break;
                    case InputKeyBack:
                        running = false;
                        break;
                    default:
                        break;
                }
            }
            furi_mutex_release(state->mutex);
        }

        // Run pattern if active
        furi_mutex_acquire(state->mutex, FuriWaitForever);
        if(state->active) {
            patterns[state->pattern_index](state);
        } else {
            turn_off_all();
        }
        
        furi_mutex_release(state->mutex);

        view_port_update(view_port);
    }

    turn_off_all();
    view_port_enabled_set(view_port, false);
    gui_remove_view_port(gui, view_port);
    furi_record_close(RECORD_GUI);
    view_port_free(view_port);
    furi_message_queue_free(queue);
    furi_mutex_free(state->mutex);
    free(state);

    return 0;
}
