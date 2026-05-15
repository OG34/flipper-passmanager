#include <furi.h>
#include <gui/gui.h>
#include <gui/view_dispatcher.h>
#include <gui/modules/submenu.h>
#include <gui/view.h>
#include <storage/storage.h>
#include <furi_hal_usb_hid.h>
#include <string.h>
#include <stdlib.h>

#define PASSWORDS_PATH   "/ext/apps/Tools/passwords.txt"
#define MAX_ENTRIES      64
#define MAX_FIELD_LEN    64
#define LINE_BUF_LEN     (MAX_FIELD_LEN * 3 + 3)
#define HID_KEY_DELAY_MS 15
#define HID_ENUM_DELAY_MS 1000

typedef struct {
    char name[MAX_FIELD_LEN];
    char user[MAX_FIELD_LEN];
    char pass[MAX_FIELD_LEN];
} PassEntry;

typedef enum {
    ViewMenu,
    ViewDetail,
} AppView;

typedef struct {
    PassEntry*      entries;
    size_t          count;
    size_t          selected_idx;
    Gui*            gui;
    ViewDispatcher* view_dispatcher;
    Submenu*        submenu;
    View*           detail_view;
} App;

typedef struct {
    App* app;
} DetailModel;

/* ---------- HID typing ---------- */

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

/* ---------- detail view callbacks ---------- */

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

/* ---------- menu callbacks ---------- */

static void menu_item_cb(void* context, uint32_t index) {
    App* app = context;
    app->selected_idx = index;
    view_dispatcher_switch_to_view(app->view_dispatcher, ViewDetail);
}

static uint32_t menu_exit_cb(void* context) {
    UNUSED(context);
    return VIEW_NONE;
}

/* ---------- file loading ---------- */

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

static size_t load_entries(PassEntry* entries, size_t max) {
    Storage* storage = furi_record_open(RECORD_STORAGE);
    File* file = storage_file_alloc(storage);
    size_t count = 0;

    if(storage_file_open(file, PASSWORDS_PATH, FSAM_READ, FSOM_OPEN_EXISTING)) {
        char line[LINE_BUF_LEN];
        size_t pos = 0;
        char ch;

        while(count < max && storage_file_read(file, &ch, 1) == 1) {
            if(ch == '\n' || pos == sizeof(line) - 1) {
                line[pos] = '\0';
                if(pos > 0 && strchr(line, '|')) {
                    memset(&entries[count], 0, sizeof(PassEntry));
                    parse_line(line, &entries[count]);
                    if(entries[count].name[0]) count++;
                }
                pos = 0;
            } else if(ch != '\r') {
                line[pos++] = ch;
            }
        }
        if(pos > 0 && count < max) {
            line[pos] = '\0';
            if(strchr(line, '|')) {
                memset(&entries[count], 0, sizeof(PassEntry));
                parse_line(line, &entries[count]);
                if(entries[count].name[0]) count++;
            }
        }
        storage_file_close(file);
    }

    storage_file_free(file);
    furi_record_close(RECORD_STORAGE);
    return count;
}

/* ---------- lifecycle ---------- */

static App* app_alloc(void) {
    App* app = malloc(sizeof(App));
    memset(app, 0, sizeof(App));

    app->entries = malloc(sizeof(PassEntry) * MAX_ENTRIES);
    app->count   = load_entries(app->entries, MAX_ENTRIES);

    app->gui             = furi_record_open(RECORD_GUI);
    app->view_dispatcher = view_dispatcher_alloc();
    view_dispatcher_enable_queue(app->view_dispatcher);
    view_dispatcher_attach_to_gui(app->view_dispatcher, app->gui, ViewDispatcherTypeFullscreen);

    /* submenu */
    app->submenu = submenu_alloc();
    if(app->count == 0) {
        submenu_add_item(app->submenu, "No entries found", 0, NULL, NULL);
    } else {
        for(size_t i = 0; i < app->count; i++) {
            submenu_add_item(app->submenu, app->entries[i].name, i, menu_item_cb, app);
        }
    }
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

    view_dispatcher_switch_to_view(app->view_dispatcher, ViewMenu);
    return app;
}

static void app_free(App* app) {
    view_dispatcher_remove_view(app->view_dispatcher, ViewMenu);
    view_dispatcher_remove_view(app->view_dispatcher, ViewDetail);
    submenu_free(app->submenu);
    view_free(app->detail_view);
    view_dispatcher_free(app->view_dispatcher);
    furi_record_close(RECORD_GUI);
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
