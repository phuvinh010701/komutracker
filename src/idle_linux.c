#if !defined(_WIN32) && !defined(__APPLE__)
#include "idle.h"
#include <stdio.h>
#include <stdlib.h>
#include <X11/Xlib.h>
#include <X11/extensions/scrnsaver.h>

static Display *display;
static XScreenSaverInfo *info;
static int use_mutter;

/* ponytail: spawns gdbus per poll; switch to GDBus/sd-bus if the poll rate ever matters. */
static double mutter_idle_seconds(void) {
    FILE *p = popen("gdbus call --session --dest org.gnome.Mutter.IdleMonitor "
                    "--object-path /org/gnome/Mutter/IdleMonitor/Core "
                    "--method org.gnome.Mutter.IdleMonitor.GetIdletime 2>/dev/null", "r");
    unsigned long long ms;
    int ok = p && fscanf(p, "(uint64 %llu", &ms) == 1;
    if (p) pclose(p);
    return ok ? (double)ms / 1000.0 : -1.0;
}

int idle_init(void) {
    if (getenv("WAYLAND_DISPLAY") && mutter_idle_seconds() >= 0) {
        use_mutter = 1; /* GNOME Wayland: XWayland has no MIT-SCREEN-SAVER */
        return 0;
    }
    if (getenv("WAYLAND_DISPLAY") && !getenv("DISPLAY")) {
        fprintf(stderr, "Wayland session has no X11 display; no supported global idle API is available\n");
        return -1;
    }
    display = XOpenDisplay(NULL);
    if (!display) {
        fprintf(stderr, "Cannot open X11 display for idle detection\n");
        return -1;
    }
    info = XScreenSaverAllocInfo();
    return info ? 0 : -1;
}

double idle_seconds(void) {
    if (use_mutter) return mutter_idle_seconds();
    if (!XScreenSaverQueryInfo(display, DefaultRootWindow(display), info)) return -1.0;
    return (double)info->idle / 1000.0;
}

void idle_cleanup(void) {
    if (info) XFree(info);
    if (display) XCloseDisplay(display);
}
#endif
