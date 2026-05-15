#include <furi.h>
#include <gui/gui.h>
#include <gui/view_dispatcher.h>
#include <gui/modules/submenu.h>
#include <gui/view.h>
#include <storage/storage.h>
#include <furi_hal_usb_hid.h>
#include <string.h>
#include <stdlib.h>

#define PASSWORDS_PATH    "/ext/apps/Tools/passwords.txt"
#define MAX_ENTRIES       64
#define MAX_FIELD_LEN     64
#define LINE_BUF_LEN      (MAX_FIELD_LEN * 3 + 3)
#define MAX_FILE_SIZE     (MAX_ENTRIES * (size_t)LINE_BUF_LEN)
#define HID_KEY_DELAY_MS  15
#define HID_ENUM_DELAY_MS 1000
#define PIN_LENGTH        4

/* PIN box geometry (128x64 screen) */
#define PIN_BOX_W   20
#define PIN_BOX_H   24
#define PIN_BOX_GAP 4
#define PIN_BOX_Y   18
/* total width = PIN_LENGTH*BOX_W + (PIN_LENGTH-1)*GAP = 4*20+3*4 = 92 */
#define PIN_BOX_X0  ((128 - (PIN_LENGTH * PIN_BOX_W + (PIN_LENGTH - 1) * PIN_BOX_GAP)) / 2)

typedef struct {
    char name[MAX_FIELD_LEN];
    char user[MAX_FIELD_LEN];
    char pass[MAX_FIELD_LEN];
} PassEntry;

typedef enum {
    ViewPin,
    ViewMenu,
    ViewDetail,
} AppView;

typedef struct {
    PassEntry*      entries;
    size_t          count;
    size_t          selected_idx;
    char            pin[PIN_LENGTH + 1];
    Gui*            gui;
    ViewDispatcher* view_dispatcher;
    View*           pin_view;
    Submenu*        submenu;
    View*           detail_view;
} App;

typedef struct {
    uint8_t digits[PIN_LENGTH];
    uint8_t cursor;
} PinModel;

typedef struct {
    App* app;
} DetailModel;

/* forward declaration needed by pin_input_cb */
static void menu_item_cb(void* context, uint32_t index);

/* ------------------------------------------------------------------ */
/* HID typing                                                          */
/* ------------------------------------------------------------------ */

static uint16_t char_to_hid(char c) {
    if(c >= 'a' && c <= 'z') return 0x04 + (uint8_t)(c - 'a');
    if(c >= 'A' && c <= 'Z') return KEY_MOD_LEFT_SHIFT | (0x04 + (uint8_t)(c - 'A'));
    if(c >= '1' && c <= '9') return 0x1E + (uint8_t)(c - '1');
    switch(c) {
    case '0':  return 0x27;
    case ' ':  return 0x2C;
    case '!':  return KEY_MOD_LEFT_SHIFT | 0x1E;
    case '@':  return KEY_MOD_LEFT_SHIFT | 0x1F;
    case '#':  return KEY_MOD_LEFT_SHIFT | 0x20;
    case '$':  return KEY_MOD_LEFT_SHIFT | 0x21;
    case '%':  return KEY_MOD_LEFT_SHIFT | 0x22;
    case '^':  return KEY_MOD_LEFT_SHIFT | 0x23;
    case '&':  return KEY_MOD_LEFT_SHIFT | 0x24;
    case '*':  return KEY_MOD_LEFT_SHIFT | 0x25;
    case '(':  return KEY_MOD_LEFT_SHIFT | 0x26;
    case ')':  return KEY_MOD_LEFT_SHIFT | 0x27;
    case '-':  return 0x2D;
    case '_':  return KEY_MOD_LEFT_SHIFT | 0x2D;
    case '=':  return 0x2E;
    case '+':  return KEY_MOD_LEFT_SHIFT | 0x2E;
    case '[':  return 0x2F;
    case '{':  return KEY_MOD_LEFT_SHIFT | 0x2F;
    case ']':  return 0x30;
    case '}':  return KEY_MOD_LEFT_SHIFT | 0x30;
    case '\\': return 0x31;
    case '|':  return KEY_MOD_LEFT_SHIFT | 0x31;
    case ';':  return 0x33;
    case ':':  return KEY_MOD_LEFT_SHIFT | 0x33;
    case '\'': return 0x34;
    case '"':  return KEY_MOD_LEFT_SHIFT | 0x34;
    case '`':  return 0x35;
    case '~':  return KEY_MOD_LEFT_SHIFT | 0x35;
    case ',':  return 0x36;
    case '<':  return KEY_MOD_LEFT_SHIFT | 0x36;
    case '.':  return 0x37;
    case '>':  return KEY_MOD_LEFT_SHIFT | 0x37;
    case '/':  return 0x38;
    case '?':  return KEY_MOD_LEFT_SHIFT | 0x38;
    default:   return 0;
    }
}

static void hid_press_key(uint16_t btn) {
    if(!btn) return;
    furi_hal_hid_kb_press(btn);
    furi_delay_ms(HID_KEY_DELAY_MS);
    furi_hal_hid_kb_release(btn);
    furi_delay_ms(HID_KEY_DELAY_MS);
}

static void hid_type_string(const char* str) {
    for(; *str; str++) hid_press_key(char_to_hid(*str));
}

static void hid_type_entry(const PassEntry* e) {
    const FuriHalUsbInterface* prev = furi_hal_usb_get_config();
    furi_hal_usb_set_config(&usb_hid, NULL);
    furi_delay_ms(HID_ENUM_DELAY_MS);

    hid_type_string(e->user);
    hid_press_key(HID_KEYBOARD_TAB);
    hid_type_string(e->pass);
    furi_hal_hid_kb_release_all();

    furi_delay_ms(100);
    furi_hal_usb_set_config(prev, NULL);
}

/* ------------------------------------------------------------------ */
/* XOR encryption (symmetric: same function encrypts and decrypts)    */
/* ------------------------------------------------------------------ */

static void xor_crypt(uint8_t* data, size_t len, const char* pin) {
    size_t pin_len = strlen(pin);
    if(!pin_len) return;
    for(size_t i = 0; i < len; i++) {
        data[i] ^= (uint8_t)pin[i % pin_len];
    }
}

/* ------------------------------------------------------------------ */
/* File loading                                                        */
/* ------------------------------------------------------------------ */

static void parse_line(const char* line, PassEntry* e) {
    char buf[LINE_BUF_LEN];
    strncpy(buf, line, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    char* p = buf;
    char* tok = strchr(p, '|');
    if(!tok) return;
    *tok = '\0';
    strncpy(e->name, p, MAX_FIELD_LEN - 1);
    p = tok + 1;

    tok = strchr(p, '|');
    if(!tok) return;
    *tok = '\0';
    strncpy(e->user, p, MAX_FIELD_LEN - 1);
    p = tok + 1;

    size_t len = strlen(p);
    while(len && (p[len - 1] == '\n' || p[len - 1] == '\r')) p[--len] = '\0';
    strncpy(e->pass, p, MAX_FIELD_LEN - 1);
}

static size_t load_entries(PassEntry* entries, size_t max, const char* pin) {
    Storage* storage = furi_record_open(RECORD_STORAGE);
    File* file = storage_file_alloc(storage);
    size_t count = 0;

    if(storage_file_open(file, PASSWORDS_PATH, FSAM_READ, FSOM_OPEN_EXISTING)) {
        uint64_t file_size = storage_file_size(file);

        if(file_size > 0 && file_size <= MAX_FILE_SIZE) {
            uint8_t* buf = malloc(file_size + 1);
            if(buf) {
                uint32_t nread = storage_file_read(file, buf, (uint32_t)file_size);
                buf[nread] = '\0';

                xor_crypt(buf, nread, pin);

                char* p = (char*)buf;
                char* file_end = p + nread;
                while(p < file_end && count < max) {
                    char* line_end = memchr(p, '\n', (size_t)(file_end - p));
                    if(line_end) *line_end = '\0';

                    size_t line_len = strlen(p);
                    while(line_len && p[line_len - 1] == '\r') p[--line_len] = '\0';

                    if(line_len > 0 && strchr(p, '|')) {
                        memset(&entries[count], 0, sizeof(PassEntry));
                        parse_line(p, &entries[count]);
                        if(entries[count].name[0]) count++;
                    }
                    p = line_end ? line_end + 1 : file_end;
                }

                /* zero decrypted data before freeing */
                memset(buf, 0, file_size + 1);
                free(buf);
            }
        }
        storage_file_close(file);
    }

    storage_file_free(file);
    furi_record_close(RECORD_STORAGE);
    return count;
}

/* ------------------------------------------------------------------ */
/* PIN entry view                                                      */
/* ------------------------------------------------------------------ */

static void pin_draw_cb(Canvas* canvas, void* model_ptr) {
    PinModel* m = model_ptr;

    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str_aligned(canvas, 64, 12, AlignCenter, AlignBottom, "Enter PIN");

    for(int i = 0; i < PIN_LENGTH; i++) {
        int bx = PIN_BOX_X0 + i * (PIN_BOX_W + PIN_BOX_GAP);
        char digit_str[2] = {'0' + m->digits[i], '\0'};

        if(i == (int)m->cursor) {
            canvas_draw_box(canvas, bx, PIN_BOX_Y, PIN_BOX_W, PIN_BOX_H);
            canvas_invert_color(canvas);
        } else {
            canvas_draw_frame(canvas, bx, PIN_BOX_Y, PIN_BOX_W, PIN_BOX_H);
        }

        canvas_set_font(canvas, FontPrimary);
        canvas_draw_str_aligned(
            canvas,
            bx + PIN_BOX_W / 2,
            PIN_BOX_Y + PIN_BOX_H / 2,
            AlignCenter,
            AlignCenter,
            digit_str);

        if(i == (int)m->cursor) {
            canvas_invert_color(canvas);
        }
    }

    canvas_draw_line(canvas, 0, 51, 127, 51);
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str_aligned(canvas, 64, 62, AlignCenter, AlignBottom, "OK: Confirm");
}

static bool pin_input_cb(InputEvent* event, void* context) {
    App* app = context;

    if(event->type != InputTypeShort && event->type != InputTypeRepeat) return false;
    if(event->key == InputKeyBack) return false; /* previous_cb exits app */

    if(event->key == InputKeyOk && event->type == InputTypeShort) {
        /* read digits without holding model across the heavy work below */
        PinModel* m = view_get_model(app->pin_view);
        for(int i = 0; i < PIN_LENGTH; i++) app->pin[i] = '0' + m->digits[i];
        app->pin[PIN_LENGTH] = '\0';
        view_commit_model(app->pin_view, false);

        app->count = load_entries(app->entries, MAX_ENTRIES, app->pin);

        submenu_reset(app->submenu);
        if(app->count == 0) {
            submenu_add_item(app->submenu, "No entries / wrong PIN", 0, NULL, NULL);
        } else {
            for(size_t i = 0; i < app->count; i++) {
                submenu_add_item(
                    app->submenu, app->entries[i].name, (uint32_t)i, menu_item_cb, app);
            }
        }
        view_dispatcher_switch_to_view(app->view_dispatcher, ViewMenu);
        return true;
    }

    with_view_model(
        app->pin_view,
        PinModel * m,
        {
            switch(event->key) {
            case InputKeyUp:
                m->digits[m->cursor] = (m->digits[m->cursor] + 1) % 10;
                break;
            case InputKeyDown:
                m->digits[m->cursor] = (m->digits[m->cursor] + 9) % 10;
                break;
            case InputKeyRight:
                if(m->cursor < PIN_LENGTH - 1) m->cursor++;
                break;
            case InputKeyLeft:
                if(m->cursor > 0) m->cursor--;
                break;
            default:
                break;
            }
        },
        true);

    return true;
}

static uint32_t pin_previous_cb(void* context) {
    UNUSED(context);
    return VIEW_NONE;
}

/* ------------------------------------------------------------------ */
/* Detail view                                                         */
/* ------------------------------------------------------------------ */

static void detail_draw_cb(Canvas* canvas, void* model_ptr) {
    DetailModel* m = model_ptr;
    App* app = m->app;
    if(app->selected_idx >= app->count) return;
    const PassEntry* e = &app->entries[app->selected_idx];

    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 0, 12, e->name);

    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 0, 26, "User:");
    canvas_draw_str(canvas, 32, 26, e->user);
    canvas_draw_str(canvas, 0, 38, "Pass:");
    canvas_draw_str(canvas, 32, 38, e->pass);

    canvas_draw_line(canvas, 0, 51, 127, 51);
    canvas_draw_str(canvas, 2, 62, "OK:Type  Back:Menu");
}

static bool detail_input_cb(InputEvent* event, void* context) {
    App* app = context;
    if(event->type == InputTypeShort && event->key == InputKeyOk) {
        if(app->selected_idx < app->count) {
            hid_type_entry(&app->entries[app->selected_idx]);
        }
        return true;
    }
    return false;
}

static uint32_t detail_back_cb(void* context) {
    UNUSED(context);
    return ViewMenu;
}

/* ------------------------------------------------------------------ */
/* Menu                                                                */
/* ------------------------------------------------------------------ */

static void menu_item_cb(void* context, uint32_t index) {
    App* app = context;
    app->selected_idx = (size_t)index;
    view_dispatcher_switch_to_view(app->view_dispatcher, ViewDetail);
}

static uint32_t menu_exit_cb(void* context) {
    UNUSED(context);
    return VIEW_NONE;
}

/* ------------------------------------------------------------------ */
/* Lifecycle                                                           */
/* ------------------------------------------------------------------ */

static App* app_alloc(void) {
    App* app = malloc(sizeof(App));
    memset(app, 0, sizeof(App));
    app->entries = malloc(sizeof(PassEntry) * MAX_ENTRIES);

    app->gui             = furi_record_open(RECORD_GUI);
    app->view_dispatcher = view_dispatcher_alloc();
    view_dispatcher_enable_queue(app->view_dispatcher);
    view_dispatcher_attach_to_gui(app->view_dispatcher, app->gui, ViewDispatcherTypeFullscreen);

    /* PIN view */
    app->pin_view = view_alloc();
    view_allocate_model(app->pin_view, ViewModelTypeLockFree, sizeof(PinModel));
    view_set_draw_callback(app->pin_view, pin_draw_cb);
    view_set_input_callback(app->pin_view, pin_input_cb);
    view_set_context(app->pin_view, app);
    view_set_previous_callback(app->pin_view, pin_previous_cb);
    view_dispatcher_add_view(app->view_dispatcher, ViewPin, app->pin_view);

    /* submenu (populated after PIN confirmed) */
    app->submenu = submenu_alloc();
    view_set_previous_callback(submenu_get_view(app->submenu), menu_exit_cb);
    view_dispatcher_add_view(app->view_dispatcher, ViewMenu, submenu_get_view(app->submenu));

    /* detail view */
    app->detail_view = view_alloc();
    view_allocate_model(app->detail_view, ViewModelTypeLockFree, sizeof(DetailModel));
    {
        DetailModel* model = view_get_model(app->detail_view);
        model->app = app;
        view_commit_model(app->detail_view, false);
    }
    view_set_draw_callback(app->detail_view, detail_draw_cb);
    view_set_input_callback(app->detail_view, detail_input_cb);
    view_set_context(app->detail_view, app);
    view_set_previous_callback(app->detail_view, detail_back_cb);
    view_dispatcher_add_view(app->view_dispatcher, ViewDetail, app->detail_view);

    view_dispatcher_switch_to_view(app->view_dispatcher, ViewPin);
    return app;
}

static void app_free(App* app) {
    view_dispatcher_remove_view(app->view_dispatcher, ViewPin);
    view_dispatcher_remove_view(app->view_dispatcher, ViewMenu);
    view_dispatcher_remove_view(app->view_dispatcher, ViewDetail);
    view_free(app->pin_view);
    submenu_free(app->submenu);
    view_free(app->detail_view);
    view_dispatcher_free(app->view_dispatcher);
    furi_record_close(RECORD_GUI);
    /* zero sensitive data before freeing */
    memset(app->pin, 0, sizeof(app->pin));
    memset(app->entries, 0, sizeof(PassEntry) * MAX_ENTRIES);
    free(app->entries);
    free(app);
}

int32_t passmanager_app(void* p) {
    UNUSED(p);
    App* app = app_alloc();
    view_dispatcher_run(app->view_dispatcher);
    app_free(app);
    return 0;
}
