with open('kernel/include/window.h', 'r', encoding='utf-8') as f:
    code = f.read()

# Fix the broken wm_invalidate / get_content_surface from the bad multi_replace
broken = '''/* Returns a pointer to the window struct, or NULL if invalid/dead */
window_t *wm_get_window(int32_t id);

/* Flags the window to be fully repainted by the compositor next frame */
void wm_invalidate(window_t *win);'''
fixed = '''/* NULL if id is invalid or the window was destroyed. */
gfx_surface_t *wm_get_content_surface(int32_t id);

/* Direct pointer to the window_t for a given id, or NULL. Prefer this over
 * wm_get_window_by_zorder() when you already have the id (e.g. right after
 * wm_create_window() returns it) — it's O(1) and doesn't depend on the
 * window still being on top of the z-order. */
window_t *wm_get_window(int32_t id);

/* Call after drawing new content into a window's surface so the
 * compositor picks it up on the next compositor_compose(). */
void wm_invalidate(int32_t id);'''
code = code.replace(broken, fixed)

with open('kernel/include/window.h', 'w', encoding='utf-8') as f:
    f.write(code)

with open('kernel/gui/window.c', 'r', encoding='utf-8') as f:
    c = f.read()

# Add wm_set_resize_handler
new_func = '''void wm_set_update_handler(int32_t id, wm_update_handler_t handler) {
    if (id >= 0 && id < WM_MAX_WINDOWS) g_windows[id].on_update = handler;
}

void wm_set_resize_handler(int32_t id, wm_resize_handler_t handler) {
    if (id >= 0 && id < WM_MAX_WINDOWS) g_windows[id].on_resize = handler;
}'''
c = c.replace('void wm_set_update_handler(int32_t id, wm_update_handler_t handler) {\n    if (id >= 0 && id < WM_MAX_WINDOWS) g_windows[id].on_update = handler;\n}', new_func)

# Call on_resize in wm_toggle_maximize
max_old = '''    /* Icerik yuzeyini yeni boyuta gore yeniden olustur */
    gfx_surface_t *new_content = surface_create((uint32_t)win->w, (uint32_t)(win->h - WM_TITLEBAR_H));
    if (new_content) {
        surface_clear(new_content, 0xFFFFFF);
        if (win->content) surface_destroy(win->content);
        win->content = new_content;
    }
'''
max_new = '''    /* Icerik yuzeyini yeni boyuta gore yeniden olustur */
    gfx_surface_t *new_content = surface_create((uint32_t)win->w, (uint32_t)(win->h - WM_TITLEBAR_H));
    if (new_content) {
        surface_clear(new_content, 0xFFFFFF);
        if (win->content) surface_destroy(win->content);
        win->content = new_content;
    }
    if (win->on_resize) win->on_resize(win);
    wm_invalidate(id);
'''
c = c.replace(max_old, max_new)

with open('kernel/gui/window.c', 'w', encoding='utf-8') as f:
    f.write(c)

print("window code patched successfully")
