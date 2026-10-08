#if !defined(_WIN32) && !defined(__APPLE__)
#define _GNU_SOURCE
#include "tray.h"
#include "tray_icon.h"

#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* GTK3 and appindicator are loaded at runtime so the binary still starts
   (in CLI mode) on machines without them. */
typedef void *W;
static struct {
    int (*init_check)(int *, char ***);
    W (*menu_new)(void);
    W (*item_new)(const char *);
    W (*sep_new)(void);
    void (*shell_append)(W, W);
    void (*show)(W);
    void (*hide)(W);
    void (*set_sensitive)(W, int);
    void (*set_label)(W, const char *);
    void (*main_loop)(void);
    void (*main_quit)(void);
    unsigned long (*connect)(void *, const char *, void (*)(void), void *, void *, int);
    unsigned (*idle_add)(int (*)(void *), void *);
    W (*ind_new)(const char *, const char *, int, const char *);
    void (*ind_status)(W, int);
    void (*ind_menu)(W, W);
    void (*ind_title)(W, const char *);
    void (*ind_label)(W, const char *, const char *);
} g;

static void (*click_cb)(int);
static W indicator;
static W widgets[TRAY_MAX_ITEMS];
static int loaded;
static char icon_dir[64], icon_file[96];

static void *sym(void *lib, const char *name) { return lib ? dlsym(lib, name) : NULL; }

static int load(void) {
    if (loaded) return loaded > 0;
    loaded = -1;
    void *gtk = dlopen("libgtk-3.so.0", RTLD_NOW | RTLD_GLOBAL);
    void *ind = dlopen("libayatana-appindicator3.so.1", RTLD_NOW | RTLD_GLOBAL);
    if (!ind) ind = dlopen("libappindicator3.so.1", RTLD_NOW | RTLD_GLOBAL);
    if (!gtk || !ind) return 0;
    *(void **)&g.init_check = sym(gtk, "gtk_init_check");
    *(void **)&g.menu_new = sym(gtk, "gtk_menu_new");
    *(void **)&g.item_new = sym(gtk, "gtk_menu_item_new_with_label");
    *(void **)&g.sep_new = sym(gtk, "gtk_separator_menu_item_new");
    *(void **)&g.shell_append = sym(gtk, "gtk_menu_shell_append");
    *(void **)&g.show = sym(gtk, "gtk_widget_show");
    *(void **)&g.hide = sym(gtk, "gtk_widget_hide");
    *(void **)&g.set_sensitive = sym(gtk, "gtk_widget_set_sensitive");
    *(void **)&g.set_label = sym(gtk, "gtk_menu_item_set_label");
    *(void **)&g.main_loop = sym(gtk, "gtk_main");
    *(void **)&g.main_quit = sym(gtk, "gtk_main_quit");
    *(void **)&g.connect = sym(gtk, "g_signal_connect_data");
    *(void **)&g.idle_add = sym(gtk, "g_idle_add");
    *(void **)&g.ind_new = sym(ind, "app_indicator_new_with_path");
    *(void **)&g.ind_status = sym(ind, "app_indicator_set_status");
    *(void **)&g.ind_menu = sym(ind, "app_indicator_set_menu");
    *(void **)&g.ind_title = sym(ind, "app_indicator_set_title");
    *(void **)&g.ind_label = sym(ind, "app_indicator_set_label");
    void **fn = (void **)&g;
    for (size_t i = 0; i < sizeof(g) / sizeof(*fn); i++) if (!fn[i]) return 0;
    loaded = 1;
    return 1;
}

int tray_available(void) {
    const char *x = getenv("DISPLAY"), *w = getenv("WAYLAND_DISPLAY");
    return ((x && *x) || (w && *w)) && load();
}

static void on_activate(void *item, void *data) { (void)item; if (click_cb) click_cb((int)(long)data); }

static void apply(const tray_menu *menu) {
    for (int i = 0; i < menu->count; i++) {
        if (!widgets[i] || !strcmp(menu->items[i].label, "-")) continue;
        g.set_label(widgets[i], menu->items[i].label);
        g.set_sensitive(widgets[i], menu->items[i].enabled);
        if (menu->items[i].visible) g.show(widgets[i]); else g.hide(widgets[i]);
    }
}

static int apply_idle(void *data) { apply(data); free(data); return 0; }
static int quit_idle(void *data) { (void)data; g.main_quit(); return 0; }

int tray_init(const char *tooltip, const tray_menu *menu, void (*on_click)(int)) {
    if (!load() || !g.init_check(NULL, NULL)) return -1;
    click_cb = on_click;
    /* appindicator only takes icons by theme name/path, so write the embedded PNG to a temp dir. */
    snprintf(icon_dir, sizeof(icon_dir), "/tmp/komutracker-%d", (int)getpid());
    snprintf(icon_file, sizeof(icon_file), "%s/komutracker.png", icon_dir);
    if (mkdir(icon_dir, 0700) && access(icon_dir, W_OK)) return -1;
    FILE *f = fopen(icon_file, "wb");
    if (!f) return -1;
    fwrite(tray_icon_linux, 1, sizeof(tray_icon_linux), f);
    fclose(f);

    W ind = g.ind_new("komutracker", "komutracker", 0 /* APPLICATION_STATUS */, icon_dir);
    if (!ind) return -1;
    indicator = ind;
    g.ind_title(ind, tooltip);
    W ui_menu = g.menu_new();
    for (int i = 0; i < menu->count; i++) {
        if (!strcmp(menu->items[i].label, "-")) {
            W sep = g.sep_new();
            g.shell_append(ui_menu, sep); g.show(sep);
            continue;
        }
        widgets[i] = g.item_new("");
        g.connect(widgets[i], "activate", (void (*)(void))on_activate, (void *)(long)menu->items[i].id, NULL, 0);
        g.shell_append(ui_menu, widgets[i]);
    }
    apply(menu);
    g.ind_menu(ind, ui_menu);
    g.ind_status(ind, 1 /* ACTIVE */);
    return 0;
}

void tray_set_menu(const tray_menu *menu) {
    tray_menu *copy = malloc(sizeof(*copy));
    if (!copy) return;
    *copy = *menu;
    g.idle_add(apply_idle, copy);
}

static int label_idle(void *text) { g.ind_label(indicator, text, "Active: 00h 00m"); free(text); return 0; }

void tray_set_label(const char *text) {
    char *copy = strdup(text);
    if (copy) g.idle_add(label_idle, copy);
}

void tray_run(void) {
    g.main_loop();
    unlink(icon_file); rmdir(icon_dir);
}

void tray_quit(void) { g.idle_add(quit_idle, NULL); }
#endif
