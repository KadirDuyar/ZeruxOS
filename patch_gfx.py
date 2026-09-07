import re

with open('kernel/drivers/gfx2d.c', 'r', encoding='utf-8') as f:
    code = f.read()

# 1. Add track_dirty_pixel macro before mark_if_screen
track_macro = '''
static inline void track_dirty_pixel(gfx_surface_t *dst, int32_t x, int32_t y) {
    if (!dst->is_dirty) {
        dst->dirty_x0 = x; dst->dirty_y0 = y;
        dst->dirty_x1 = x + 1; dst->dirty_y1 = y + 1;
        dst->is_dirty = true;
    } else {
        if (x < dst->dirty_x0) dst->dirty_x0 = x;
        if (y < dst->dirty_y0) dst->dirty_y0 = y;
        if (x + 1 > dst->dirty_x1) dst->dirty_x1 = x + 1;
        if (y + 1 > dst->dirty_y1) dst->dirty_y1 = y + 1;
    }
}
'''
if 'track_dirty_pixel' not in code:
    code = code.replace('static void mark_if_screen', track_macro + '\nstatic void mark_if_screen')

# 2. Modify mark_if_screen to use is_dirty
mark_if_screen_old = '''static void mark_if_screen(gfx_surface_t *dst, int32_t x, int32_t y, int32_t w, int32_t h) {
    gfx_surface_t *screen = fb_get_screen_surface();
    if (screen && dst == screen) {
        dirty_mark(x, y, w, h);
    }
}'''
mark_if_screen_new = '''static void mark_if_screen(gfx_surface_t *dst, int32_t x, int32_t y, int32_t w, int32_t h) {
    gfx_surface_t *screen = fb_get_screen_surface();
    if (screen && dst == screen) {
        dirty_mark(x, y, w, h);
    }
    // Also if it's the screen, clear the internal dirty flag since it was flushed
    if (screen && dst == screen) {
        dst->is_dirty = false;
    }
}'''
code = code.replace(mark_if_screen_old, mark_if_screen_new)

# 3. Modify set_px
set_px_old = '''    dst->pixels[(uint32_t)y * dst->pitch + (uint32_t)x] = color;'''
set_px_new = '''    uint32_t *p = &dst->pixels[(uint32_t)y * dst->pitch + (uint32_t)x];
    if (*p != color) {
        *p = color;
        track_dirty_pixel(dst, x, y);
    }'''
code = code.replace(set_px_old, set_px_new)

# 4. Modify gfx_fill_rect loop
fill_old = '''        for (int32_t col = 0; col < w; col++) dstrow[col] = color;'''
fill_new = '''        for (int32_t col = 0; col < w; col++) {
            if (dstrow[col] != color) {
                dstrow[col] = color;
                track_dirty_pixel(dst, x + col, y + row);
            }
        }'''
code = code.replace(fill_old, fill_new)

# 5. Modify gfx_blit loop
blit_old = '''        for (int32_t col = 0; col < w; col++) drow[col] = srow[col];'''
blit_new = '''        for (int32_t col = 0; col < w; col++) {
            if (drow[col] != srow[col]) {
                drow[col] = srow[col];
                track_dirty_pixel(dst, dx + col, dy + row);
            }
        }'''
code = code.replace(blit_old, blit_new)

# 6. Modify gfx_stretch_blit loop
stretch_old = '''        for (int32_t col = 0; col < dw; col++) {
            int32_t srcx = (col * x_ratio) >> 16;
            drow[col] = srow[srcx];
        }'''
stretch_new = '''        for (int32_t col = 0; col < dw; col++) {
            int32_t srcx = (col * x_ratio) >> 16;
            if (drow[col] != srow[srcx]) {
                drow[col] = srow[srcx];
                track_dirty_pixel(dst, dx + col, dy + row);
            }
        }'''
code = code.replace(stretch_old, stretch_new)

# 7. Modify gfx_blit_alpha loop
alpha_old = '''            drow[col] = (rr << 16) | (rg << 8) | rb;'''
alpha_new = '''            uint32_t new_color = (rr << 16) | (rg << 8) | rb;
            if (drow[col] != new_color) {
                drow[col] = new_color;
                track_dirty_pixel(dst, dx + col, dy + row);
            }'''
code = code.replace(alpha_old, alpha_new)

with open('kernel/drivers/gfx2d.c', 'w', encoding='utf-8') as f:
    f.write(code)

print("gfx2d.c patched successfully")
