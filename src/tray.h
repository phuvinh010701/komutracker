#ifndef TRACKER_TRAY_H
#define TRACKER_TRAY_H

#define TRAY_MAX_ITEMS 8

/* label "-" is a separator. */
typedef struct {
    char label[160];
    int id;
    int enabled;
    int visible;
} tray_item;

typedef struct {
    int count;
    tray_item items[TRAY_MAX_ITEMS];
} tray_menu;

/* 1 if a tray can be shown in this environment (Linux: GTK3 + appindicator + display). */
int tray_available(void);
/* Main thread. The layout (count, separators, ids) is fixed by `menu`;
   on_click(id) runs on the UI thread and must not block. Returns 0 on success. */
int tray_init(const char *tooltip, const tray_menu *menu, void (*on_click)(int id));
/* Any thread: updates label/enabled/visible of the items by index. */
void tray_set_menu(const tray_menu *menu);
/* Main thread: runs the UI loop until tray_quit(). */
void tray_run(void);
/* Any thread: text shown next to the icon ("" clears it). Linux only; no-op elsewhere. */
void tray_set_label(const char *text);
/* Any thread. */
void tray_quit(void);

#endif
