with open('kernel/gui/compositor.c', 'r', encoding='utf-8') as f:
    code = f.read()

old_loop = '''        bool changed;
        if (g_have_prev[win->id]) {
            wm_snapshot_t *p = &g_prev[win->id];
            changed = (p->x != win->x || p->y != win->y || p->w != win->w || p->h != win->h) || win->dirty;
            if (changed) extend_damage(&dx0, &dy0, &dx1, &dy1, &has_damage, p->x, p->y, p->w, p->h);
        } else {
            changed = true; /* never seen this slot before */
        }
        if (changed) extend_damage(&dx0, &dy0, &dx1, &dy1, &has_damage, win->x, win->y, win->w, win->h);'''

new_loop = '''        if (g_have_prev[win->id]) {
            wm_snapshot_t *p = &g_prev[win->id];
            bool bounds_changed = (p->x != win->x || p->y != win->y || p->w != win->w || p->h != win->h);
            if (bounds_changed || win->dirty) {
                extend_damage(&dx0, &dy0, &dx1, &dy1, &has_damage, p->x, p->y, p->w, p->h);
                extend_damage(&dx0, &dy0, &dx1, &dy1, &has_damage, win->x, win->y, win->w, win->h);
            } else if (win->content && win->content->is_dirty) {
                extend_damage(&dx0, &dy0, &dx1, &dy1, &has_damage,
                              win->x + win->content->dirty_x0,
                              win->y + WM_TITLEBAR_H + win->content->dirty_y0,
                              win->content->dirty_x1 - win->content->dirty_x0,
                              win->content->dirty_y1 - win->content->dirty_y0);
            }
        } else {
            extend_damage(&dx0, &dy0, &dx1, &dy1, &has_damage, win->x, win->y, win->w, win->h);
        }'''

code = code.replace(old_loop, new_loop)

# Also clear win->content->is_dirty at the end of paint_window
old_paint = '''    win->dirty = false;
}'''
new_paint = '''    win->dirty = false;
    if (win->content) win->content->is_dirty = false;
}'''

code = code.replace(old_paint, new_paint)

with open('kernel/gui/compositor.c', 'w', encoding='utf-8') as f:
    f.write(code)

print("compositor.c patched successfully")
