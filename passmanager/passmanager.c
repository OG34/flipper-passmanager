#include <furi.h>
#include <furi_hal.h>
#include <gui/gui.h>
#include <gui/view_dispatcher.h>
#include <gui/modules/submenu.h>
#include <gui/view.h>
#include <storage/storage.h>
#include <string.h>
#include <stdlib.h>

extern const FuriHalUsbInterface usb_hid;

#define PASSWORDS_PATH    "/ext/apps/Tools/passwords.txt"
#define MAX_ENTRIES       64
#define MAX_FIELD_LEN     64
#define LINE_BUF_LEN      (MAX_FIELD_LEN * 3 + 3)
#define MAX_FILE_SIZE     (MAX_ENTRIES * (size_t)LINE_BUF_LEN)
#define HID_KEY_DELAY_MS  15
#define HID_ENUM_DELAY_MS 1000
#define PIN_LENGTH        6
#define AUTO_LOCK_MS      (60 * 1000)
#define EVENT_LOCK        0u

/* PIN box geometry (128x64 screen, 6 digits: 6*16 + 5*3 = 111px wide) */
#define PIN_BOX_W   16
#define PIN_BOX_H   24
#define PIN_BOX_GAP 3
#define PIN_BOX_Y   18
#define PIN_BOX_X0  ((128 - (PIN_LENGTH * PIN_BOX_W + (PIN_LENGTH - 1) * PIN_BOX_GAP)) / 2)

/* ChaCha20 */
#define CHACHA20_KEY_SIZE   32
#define CHACHA20_NONCE_SIZE 12

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
    FuriTimer*      lock_timer;
} App;

typedef struct {
    uint8_t digits[PIN_LENGTH];
    uint8_t cursor;
    bool    error;
} PinModel;

typedef struct {
    App* app;
} DetailModel;

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
    FuriHalUsbInterface* prev = (FuriHalUsbInterface*)furi_hal_usb_get_config();
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
/* ChaCha20 stream cipher (RFC 7539)                                  */
/* ------------------------------------------------------------------ */

#define ROTL32(v, n) (((uint32_t)(v) << (n)) | ((uint32_t)(v) >> (32u - (n))))

#define QR(a, b, c, d) do { \
    (a) += (b); (d) ^= (a); (d) = ROTL32((d), 16); \
    (c) += (d); (b) ^= (c); (b) = ROTL32((b), 12); \
    (a) += (b); (d) ^= (a); (d) = ROTL32((d),  8); \
    (c) += (d); (b) ^= (c); (b) = ROTL32((b),  7); \
} while(0)

static void chacha20_block(const uint32_t key[8], uint32_t ctr,
                           const uint32_t nonce[3], uint8_t out[64]) {
    static const uint32_t C[4] = {
        0x61707865u, 0x3320646eu, 0x79622d32u, 0x6b206574u
    };
    uint32_t s[16], w[16];

    s[0]  = C[0];     s[1]  = C[1];     s[2]  = C[2];     s[3]  = C[3];
    s[4]  = key[0];   s[5]  = key[1];   s[6]  = key[2];   s[7]  = key[3];
    s[8]  = key[4];   s[9]  = key[5];   s[10] = key[6];   s[11] = key[7];
    s[12] = ctr;      s[13] = nonce[0]; s[14] = nonce[1]; s[15] = nonce[2];

    for(int i = 0; i < 16; i++) w[i] = s[i];

    for(int i = 0; i < 10; i++) {
        QR(w[0], w[4], w[ 8], w[12]); QR(w[1], w[5], w[ 9], w[13]);
        QR(w[2], w[6], w[10], w[14]); QR(w[3], w[7], w[11], w[15]);
        QR(w[0], w[5], w[10], w[15]); QR(w[1], w[6], w[11], w[12]);
        QR(w[2], w[7], w[ 8], w[13]); QR(w[3], w[4], w[ 9], w[14]);
    }

    for(int i = 0; i < 16; i++) {
        uint32_t v = w[i] + s[i];
        out[i * 4]     = (uint8_t)(v);
        out[i * 4 + 1] = (uint8_t)(v >>  8);
        out[i * 4 + 2] = (uint8_t)(v >> 16);
        out[i * 4 + 3] = (uint8_t)(v >> 24);
    }
}

static void chacha20_xor(const uint8_t key[CHACHA20_KEY_SIZE],
                         const uint8_t nonce_bytes[CHACHA20_NONCE_SIZE],
                         uint8_t* data, size_t len) {
    uint32_t k[8], n[3];
    uint8_t  stream[64];

    for(int i = 0; i < 8; i++)
        k[i] = (uint32_t)key[i*4]         | ((uint32_t)key[i*4+1] <<  8)
             | ((uint32_t)key[i*4+2] << 16) | ((uint32_t)key[i*4+3] << 24);
    for(int i = 0; i < 3; i++)
        n[i] = (uint32_t)nonce_bytes[i*4]         | ((uint32_t)nonce_bytes[i*4+1] <<  8)
             | ((uint32_t)nonce_bytes[i*4+2] << 16) | ((uint32_t)nonce_bytes[i*4+3] << 24);

    for(uint32_t blk = 0; (size_t)blk * 64 < len; blk++) {
        chacha20_block(k, blk, n, stream);
        size_t off = (size_t)blk * 64;
        size_t end = off + 64 < len ? off + 64 : len;
        for(size_t j = off; j < end; j++) data[j] ^= stream[j - off];
    }
    memset(stream, 0, sizeof(stream));
}

/* ------------------------------------------------------------------ */
/* Key derivation: PIN → 32-byte key (10 000 mixing rounds)           */
/* ------------------------------------------------------------------ */

static void derive_key(const char* pin, uint8_t key[CHACHA20_KEY_SIZE]) {
    size_t pin_len = strlen(pin);
    for(size_t i = 0; i < CHACHA20_KEY_SIZE; i++)
        key[i] = (uint8_t)pin[i % pin_len];

    for(uint32_t r = 0; r < 10000; r++) {
        uint16_t carry = 0;
        for(size_t i = 0; i < CHACHA20_KEY_SIZE; i++) {
            uint16_t v = (uint16_t)key[i]
                       + (uint16_t)key[(i + 1) % CHACHA20_KEY_SIZE]
                       + carry
                       + (uint8_t)pin[r % pin_len];
            key[i] = (uint8_t)(v & 0xFF);
            carry  = v >> 8;
        }
    }
}

/* ------------------------------------------------------------------ */
/* File loading                                                        */
/* File format: [12-byte random nonce][ChaCha20 ciphertext]           */
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
    File*    file    = storage_file_alloc(storage);
    size_t   count   = 0;

    if(storage_file_open(file, PASSWORDS_PATH, FSAM_READ, FSOM_OPEN_EXISTING)) {
        uint64_t file_size = storage_file_size(file);
        uint64_t max_size  = (uint64_t)MAX_FILE_SIZE + CHACHA20_NONCE_SIZE;

        if(file_size > CHACHA20_NONCE_SIZE && file_size <= max_size) {
            uint8_t* buf = malloc((size_t)file_size + 1);
            if(buf) {
                uint16_t nread = storage_file_read(file, buf, (uint16_t)file_size);

                if((size_t)nread > CHACHA20_NONCE_SIZE) {
                    uint8_t  key[CHACHA20_KEY_SIZE];
                    uint8_t* nonce      = buf;
                    uint8_t* ciphertext = buf + CHACHA20_NONCE_SIZE;
                    size_t   ct_len     = (size_t)nread - CHACHA20_NONCE_SIZE;

                    derive_key(pin, key);
                    chacha20_xor(key, nonce, ciphertext, ct_len);
                    memset(key, 0, sizeof(key));

                    ciphertext[ct_len] = '\0';

                    char* p        = (char*)ciphertext;
                    char* file_end = p + ct_len;
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
                }

                memset(buf, 0, (size_t)file_size + 1);
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
/* Auto-lock                                                           */
/* ------------------------------------------------------------------ */

static void lock_timer_cb(void* context) {
    App* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, EVENT_LOCK);
}

static bool app_custom_event_cb(void* context, uint32_t event) {
    App* app = context;
    if(event == EVENT_LOCK) {
        view_dispatcher_switch_to_view(app->view_dispatcher, ViewPin);
        return true;
    }
    return false;
}

/* ------------------------------------------------------------------ */
/* PIN entry view                                                      */
/* ------------------------------------------------------------------ */

static void pin_enter_cb(void* context) {
    App* app = context;
    furi_timer_stop(app->lock_timer);
    if(app->count > 0) {
        memset(app->entries, 0, sizeof(PassEntry) * MAX_ENTRIES);
        app->count        = 0;
        app->selected_idx = 0;
        submenu_reset(app->submenu);
    }
    with_view_model(
        app->pin_view,
        PinModel* m,
        { memset(m, 0, sizeof(PinModel)); },
        true);
}

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

        if(i == (int)m->cursor) canvas_invert_color(canvas);
    }

    canvas_draw_line(canvas, 0, 51, 127, 51);
    canvas_set_font(canvas, FontSecondary);
    if(m->error) {
        canvas_draw_str_aligned(canvas, 64, 62, AlignCenter, AlignBottom, "Wrong PIN — try again");
    } else {
        canvas_draw_str_aligned(canvas, 64, 62, AlignCenter, AlignBottom, "OK: Confirm");
    }
}

static bool pin_input_cb(InputEvent* event, void* context) {
    App* app = context;

    if(event->type != InputTypeShort && event->type != InputTypeRepeat) return false;
    if(event->key == InputKeyBack) return false;

    if(event->key == InputKeyOk && event->type == InputTypeShort) {
        PinModel* m = view_get_model(app->pin_view);
        for(int i = 0; i < PIN_LENGTH; i++) app->pin[i] = '0' + m->digits[i];
        app->pin[PIN_LENGTH] = '\0';
        view_commit_model(app->pin_view, false);

        app->count = load_entries(app->entries, MAX_ENTRIES, app->pin);

        if(app->count == 0) {
            with_view_model(app->pin_view, PinModel* m, { m->error = true; }, true);
        } else {
            submenu_reset(app->submenu);
            for(size_t i = 0; i < app->count; i++) {
                submenu_add_item(
                    app->submenu, app->entries[i].name, (uint32_t)i, menu_item_cb, app);
            }
            furi_timer_start(app->lock_timer, AUTO_LOCK_MS);
            view_dispatcher_switch_to_view(app->view_dispatcher, ViewMenu);
        }
        return true;
    }

    with_view_model(
        app->pin_view,
        PinModel* m,
        {
            m->error = false;
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
    canvas_draw_str(canvas, 32, 38, "********");

    canvas_draw_line(canvas, 0, 51, 127, 51);
    canvas_draw_str(canvas, 2, 62, "OK:Type  Back:Menu");
}

static bool detail_input_cb(InputEvent* event, void* context) {
    App* app = context;
    if(event->type == InputTypeShort && event->key == InputKeyOk) {
        furi_timer_start(app->lock_timer, AUTO_LOCK_MS);
        if(app->selected_idx < app->count)
            hid_type_entry(&app->entries[app->selected_idx]);
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
    return ViewPin;
}

/* ------------------------------------------------------------------ */
/* Lifecycle                                                           */
/* ------------------------------------------------------------------ */

static App* app_alloc(void) {
    App* app = malloc(sizeof(App));
    if(!app) return NULL;
    memset(app, 0, sizeof(App));
    app->entries = malloc(sizeof(PassEntry) * MAX_ENTRIES);
    if(!app->entries) { free(app); return NULL; }

    app->gui             = furi_record_open(RECORD_GUI);
    app->view_dispatcher = view_dispatcher_alloc();
    view_dispatcher_enable_queue(app->view_dispatcher);
    view_dispatcher_attach_to_gui(app->view_dispatcher, app->gui, ViewDispatcherTypeFullscreen);
    view_dispatcher_set_custom_event_callback(app->view_dispatcher, app_custom_event_cb, app);

    app->lock_timer = furi_timer_alloc(lock_timer_cb, FuriTimerTypeOnce, app);

    app->pin_view = view_alloc();
    view_allocate_model(app->pin_view, ViewModelTypeLockFree, sizeof(PinModel));
    view_set_draw_callback(app->pin_view, pin_draw_cb);
    view_set_input_callback(app->pin_view, pin_input_cb);
    view_set_enter_callback(app->pin_view, pin_enter_cb);
    view_set_context(app->pin_view, app);
    view_set_previous_callback(app->pin_view, pin_previous_cb);
    view_dispatcher_add_view(app->view_dispatcher, ViewPin, app->pin_view);

    app->submenu = submenu_alloc();
    view_set_previous_callback(submenu_get_view(app->submenu), menu_exit_cb);
    view_dispatcher_add_view(app->view_dispatcher, ViewMenu, submenu_get_view(app->submenu));

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
    furi_timer_stop(app->lock_timer);
    furi_timer_free(app->lock_timer);
    view_dispatcher_remove_view(app->view_dispatcher, ViewPin);
    view_dispatcher_remove_view(app->view_dispatcher, ViewMenu);
    view_dispatcher_remove_view(app->view_dispatcher, ViewDetail);
    view_free(app->pin_view);
    submenu_free(app->submenu);
    view_free(app->detail_view);
    view_dispatcher_free(app->view_dispatcher);
    furi_record_close(RECORD_GUI);
    memset(app->pin, 0, sizeof(app->pin));
    if(app->entries) {
        memset(app->entries, 0, sizeof(PassEntry) * MAX_ENTRIES);
        free(app->entries);
    }
    free(app);
}

int32_t passmanager_app(void* p) {
    UNUSED(p);
    App* app = app_alloc();
    if(!app) return -1;
    view_dispatcher_run(app->view_dispatcher);
    app_free(app);
    return 0;
}
