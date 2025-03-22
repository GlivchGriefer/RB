#include <furi.h>
#include <furi_hal_power.h>
#include <gui/gui.h>
#include <input/input.h>
#include <stdlib.h>
#include <gui/elements.h>

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
    bool is_b2_on;
    bool is_c3_on;
    uint8_t current_pin;
} PluginState;

static void render_callback(Canvas* const canvas, void* ctx) {
    furi_assert(ctx);
    PluginState* plugin_state = ctx;
    furi_mutex_acquire(plugin_state->mutex, FuriWaitForever);

    canvas_set_font(canvas, FontPrimary);
    elements_multiline_text_aligned(canvas, 64, 4, AlignCenter, AlignTop, "Emergency Lights");

    canvas_set_font(canvas, FontSecondary);
    canvas_draw_icon(canvas, 0, 17, &I_led_connections);

    const char* pin_name = plugin_state->current_pin == 0 ? "B2" : "C3";
    bool is_on = plugin_state->current_pin == 0 ? plugin_state->is_b2_on : plugin_state->is_c3_on;
    char text_buffer[128];
    snprintf(text_buffer, sizeof(text_buffer), "%s is %s", pin_name, is_on ? "ON" : "OFF");
    elements_multiline_text_aligned(canvas, 64, 44, AlignCenter, AlignTop, text_buffer);

    furi_mutex_release(plugin_state->mutex);
}

static void flash_toggle(PluginState* const plugin_state) {
    const GpioPin* pin = (plugin_state->current_pin == 0) ? &gpio_ext_pb2 : &gpio_ext_pc3;
    bool* pin_state = (plugin_state->current_pin == 0) ? &plugin_state->is_b2_on : &plugin_state->is_c3_on;
    
    furi_hal_gpio_init(pin, GpioModeOutputPushPull, GpioPullNo, GpioSpeedVeryHigh);
    *pin_state = !(*pin_state); // Toggle the state
    furi_hal_gpio_write(pin, *pin_state);
}

static void input_callback(InputEvent* input_event, void* ctx) {
    furi_assert(ctx);
    FuriMessageQueue* event_queue = ctx;

    PluginEvent event = {.type = EventTypeKey, .input = *input_event};
    furi_message_queue_put(event_queue, &event, FuriWaitForever);
}

int32_t rb_app() {
    FuriMessageQueue* event_queue = furi_message_queue_alloc(8, sizeof(PluginEvent));

    PluginState* plugin_state = malloc(sizeof(PluginState));
    plugin_state->mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    plugin_state->current_pin = 0; // Start with B2
    plugin_state->is_b2_on = false;
    plugin_state->is_c3_on = false;

    if(!plugin_state->mutex) {
        FURI_LOG_E("flashlight", "cannot create mutex\r\n");
        furi_message_queue_free(event_queue);
        free(plugin_state);
        return 255;
    }

    // Set system callbacks
    ViewPort* view_port = view_port_alloc();
    view_port_draw_callback_set(view_port, render_callback, plugin_state);
    view_port_input_callback_set(view_port, input_callback, event_queue);

    // Open GUI and register view_port
    Gui* gui = furi_record_open(RECORD_GUI);
    gui_add_view_port(gui, view_port, GuiLayerFullscreen);

    PluginEvent event;
    for(bool processing = true; processing;) {
        FuriStatus event_status = furi_message_queue_get(event_queue, &event, 100);
        furi_mutex_acquire(plugin_state->mutex, FuriWaitForever);

        if(event_status == FuriStatusOk) {
            if(event.type == EventTypeKey) {
                if(event.input.type == InputTypePress) {
                    switch(event.input.key) {
                    case InputKeyRight:
                    case InputKeyLeft:
                        // Switch pin without changing the state of the light
                        plugin_state->current_pin = (event.input.key == InputKeyRight) ?
                            (plugin_state->current_pin + 1) % 2 :
                            (plugin_state->current_pin + 2 - 1) % 2; // +2 to avoid negative modulo
                        break;
                    case InputKeyOk:
                        flash_toggle(plugin_state);
                        break;
                    case InputKeyBack:
                        processing = false;
                        break;
                    default:
                        break;
                    }
                }
            }
        }

        furi_mutex_release(plugin_state->mutex);
        view_port_update(view_port);
    }

    view_port_enabled_set(view_port, false);
    gui_remove_view_port(gui, view_port);
    furi_record_close(RECORD_GUI);
    view_port_free(view_port);
    furi_message_queue_free(event_queue);
    furi_mutex_free(plugin_state->mutex);

    return 0;
}
