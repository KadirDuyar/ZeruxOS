# Patch terminal_app.c
with open('kernel/gui/terminal_app.c', 'r', encoding='utf-8') as f:
    c = f.read()

# Add escape_state to struct
struct_old = '''    char      input[TA_LINE_LEN];
    uint32_t  input_len;
} terminal_state_t;'''
struct_new = '''    char      input[TA_LINE_LEN];
    uint32_t  input_len;
    int32_t   escape_state;
} terminal_state_t;'''
c = c.replace(struct_old, struct_new)

# Modify terminal_on_key
key_old = '''static void terminal_on_key(window_t *win, char ch) {
    terminal_state_t *t = (terminal_state_t *)win->app_data;
    if (!t) return;

    if (ch == '\n') {'''
key_new = '''static void terminal_on_key(window_t *win, char ch) {
    terminal_state_t *t = (terminal_state_t *)win->app_data;
    if (!t) return;
    
    if (t->escape_state == 1) {
        if (ch == '[') t->escape_state = 2;
        else t->escape_state = 0;
        return;
    } else if (t->escape_state == 2) {
        /* Arrow keys: A=Up, B=Down, C=Right, D=Left */
        t->escape_state = 0;
        return;
    }
    if (ch == 27) {
        t->escape_state = 1;
        return;
    }

    if (ch == '\n') {'''
c = c.replace(key_old, key_new)

# Find terminal_redraw signature
# It might be static void terminal_redraw(window_t *win)
# Register on_resize
init_old = '''    wm_set_key_handler(id, terminal_on_key);
    wm_set_destroy_handler(id, terminal_on_destroy);'''
init_new = '''    wm_set_key_handler(id, terminal_on_key);
    wm_set_destroy_handler(id, terminal_on_destroy);
    wm_set_resize_handler(id, terminal_redraw);'''
c = c.replace(init_old, init_new)

with open('kernel/gui/terminal_app.c', 'w', encoding='utf-8') as f:
    f.write(c)


# Patch explorer_app.c
with open('kernel/gui/explorer_app.c', 'r', encoding='utf-8') as f:
    c = f.read()

# Add escape_state to struct
struct_old = '''    bool      input_active;
    char      input_path[EXP_MAX_PATH];
    uint32_t  input_len;
} explorer_state_t;'''
struct_new = '''    bool      input_active;
    char      input_path[EXP_MAX_PATH];
    uint32_t  input_len;
    int32_t   escape_state;
} explorer_state_t;'''
c = c.replace(struct_old, struct_new)

# Modify explorer_on_key
key_old = '''static void explorer_on_key(window_t *win, char ch) {
    explorer_state_t *s = (explorer_state_t *)win->app_data;
    if (!s) return;

    if (s->input_active) {'''
key_new = '''static void explorer_on_key(window_t *win, char ch) {
    explorer_state_t *s = (explorer_state_t *)win->app_data;
    if (!s) return;

    if (s->escape_state == 1) {
        if (ch == '[') s->escape_state = 2;
        else s->escape_state = 0;
        return;
    } else if (s->escape_state == 2) {
        s->escape_state = 0;
        return;
    }
    if (ch == 27) {
        s->escape_state = 1;
        return;
    }

    if (s->input_active) {'''
c = c.replace(key_old, key_new)

# Register on_resize
init_old = '''    wm_set_key_handler(id, explorer_on_key);
    wm_set_click_handler(id, explorer_on_click);
    wm_set_destroy_handler(id, explorer_on_destroy);'''
init_new = '''    wm_set_key_handler(id, explorer_on_key);
    wm_set_click_handler(id, explorer_on_click);
    wm_set_destroy_handler(id, explorer_on_destroy);
    wm_set_resize_handler(id, explorer_redraw);'''
c = c.replace(init_old, init_new)

with open('kernel/gui/explorer_app.c', 'w', encoding='utf-8') as f:
    f.write(c)

print("apps patched successfully")
