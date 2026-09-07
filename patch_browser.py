with open('kernel/gui/browser_app.c', 'r', encoding='utf-8') as f:
    c = f.read()

init_old = '''    wm_set_key_handler(id, br_on_key);
    wm_set_click_handler(id, br_on_click);
    wm_set_destroy_handler(id, br_on_destroy);'''
init_new = '''    wm_set_key_handler(id, br_on_key);
    wm_set_click_handler(id, br_on_click);
    wm_set_destroy_handler(id, br_on_destroy);
    wm_set_resize_handler(id, br_redraw);'''
if init_old in c:
    c = c.replace(init_old, init_new)
    with open('kernel/gui/browser_app.c', 'w', encoding='utf-8') as f:
        f.write(c)
    print("browser_app.c patched")
else:
    print("Could not find init in browser_app.c")
