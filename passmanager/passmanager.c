#include <furi.h>
#include <gui/gui.h>
#include <gui/view_dispatcher.h>
#include <gui/modules/submenu.h>
#include <gui/modules/widget.h>
#include <storage/storage.h>
#include <string.h>
#include <stdlib.h>

#define PASSWORDS_PATH  "/ext/apps/Tools/passwords.txt"
#define MAX_ENTRIES     64
#define MAX_FIELD_LEN   64
#define LINE_BUF_LEN    (MAX_FIELD_LEN * 3 + 3)

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
    Gui*            gui;
    ViewDispatcher* view_dispatcher;
    Submenu*        submenu;
    Widget*         widget;
} App;

/* ---------- helpers ---------- */

static void parse_line(const char* line, PassEntry* e) {
    char buf[LINE_BUF_LEN];
    strncpy(buf, line, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    char* p = buf;
    char* tok;

    tok = strchr(p, '|');
    if(!tok) return;
    *tok = '\0';
    strncpy(e->name, p, MAX_FIELD_LEN - 1);
    p = tok + 1;

    tok = strchr(p, '|');
    if(!tok) return;
    *tok = '\0';
    strncpy(e->user, p, MAX_FIELD_LEN - 1);
    p = tok + 1;

    /* strip trailing newline */
    size_t len = strlen(p);
    if(len && (p[len - 1] == '\n' || p[len - 1] == '\r')) p[--len] = '\0';
    if(len && (p[len - 1] == '\r')) p[--len] = '\0';

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
            } else {
                line[pos++] = ch;
            }
        }
        /* handle file not ending with newline */
        if(pos > 0) {
            line[pos] = '\0';
            if(strchr(line, '|') && count < max) {
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

/* ---------- callbacks ---------- */

static void menu_callback(void* context, uint32_t index) {
    App* app = context;
    if(index >= app->count) return;

    PassEntry* e = &app->entries[index];

    widget_reset(app->widget);
    widget_add_string_element(app->widget, 0, 0,  AlignLeft, AlignTop, FontPrimary,   e->name);
    widget_add_string_element(app->widget, 0, 16, AlignLeft, AlignTop, FontSecondary, "User:");
    widget_add_string_element(app->widget, 30, 16, AlignLeft, AlignTop, FontSecondary, e->user);
    widget_add_string_element(app->widget, 0, 28, AlignLeft, AlignTop, FontSecondary, "Pass:");
    widget_add_string_element(app->widget, 30, 28, AlignLeft, AlignTop, FontSecondary, e->pass);
    widget_add_string_element(app->widget, 0, 50, AlignLeft, AlignTop, FontSecondary, "Press Back to return");

    view_dispatcher_switch_to_view(app->view_dispatcher, ViewDetail);
}

static uint32_t detail_back_callback(void* context) {
    UNUSED(context);
    return ViewMenu;
}

static uint32_t menu_exit_callback(void* context) {
    UNUSED(context);
    return VIEW_NONE;
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
            submenu_add_item(app->submenu, app->entries[i].name, i, menu_callback, app);
        }
    }
    view_set_previous_callback(submenu_get_view(app->submenu), menu_exit_callback);
    view_dispatcher_add_view(app->view_dispatcher, ViewMenu, submenu_get_view(app->submenu));

    /* detail widget */
    app->widget = widget_alloc();
    view_set_previous_callback(widget_get_view(app->widget), detail_back_callback);
    view_dispatcher_add_view(app->view_dispatcher, ViewDetail, widget_get_view(app->widget));

    view_dispatcher_switch_to_view(app->view_dispatcher, ViewMenu);
    return app;
}

static void app_free(App* app) {
    view_dispatcher_remove_view(app->view_dispatcher, ViewMenu);
    view_dispatcher_remove_view(app->view_dispatcher, ViewDetail);
    submenu_free(app->submenu);
    widget_free(app->widget);
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
