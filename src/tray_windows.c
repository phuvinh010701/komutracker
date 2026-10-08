#ifdef _WIN32
#include "tray.h"
#include <windows.h>
#include <shellapi.h>
#include <string.h>

#define WM_TRAY (WM_APP + 1)
#define ICON_UID 1
#define RES_ICON 1 /* komutracker.rc */

static void (*click_cb)(int);
static HWND window;
static NOTIFYICONDATAA nid;
static UINT taskbar_created;
static tray_menu current;
static CRITICAL_SECTION lock;
static char tip[128];

int tray_available(void) { return 1; }

static void add_icon(void) {
    memset(&nid, 0, sizeof(nid));
    nid.cbSize = sizeof(nid);
    nid.hWnd = window;
    nid.uID = ICON_UID;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = WM_TRAY;
    nid.hIcon = LoadIconA(GetModuleHandleA(NULL), MAKEINTRESOURCEA(RES_ICON));
    if (!nid.hIcon) nid.hIcon = LoadIconA(NULL, IDI_APPLICATION);
    strncpy(nid.szTip, tip, sizeof(nid.szTip) - 1);
    Shell_NotifyIconA(NIM_ADD, &nid);
}

static void show_menu(void) {
    HMENU popup = CreatePopupMenu();
    if (!popup) return;
    EnterCriticalSection(&lock);
    int pending_separator = 0, added = 0;
    for (int i = 0; i < current.count; i++) {
        const tray_item *item = &current.items[i];
        if (!strcmp(item->label, "-")) { pending_separator = added; continue; }
        if (!item->visible) continue;
        if (pending_separator) { AppendMenuA(popup, MF_SEPARATOR, 0, NULL); pending_separator = 0; }
        AppendMenuA(popup, MF_STRING | (item->enabled ? MF_ENABLED : MF_GRAYED), (UINT_PTR)item->id, item->label);
        added++;
    }
    LeaveCriticalSection(&lock);
    POINT pt; GetCursorPos(&pt);
    SetForegroundWindow(window); /* required so the menu closes when clicking elsewhere */
    int id = (int)TrackPopupMenu(popup, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY, pt.x, pt.y, 0, window, NULL);
    PostMessageA(window, WM_NULL, 0, 0);
    DestroyMenu(popup);
    if (id && click_cb) click_cb(id);
}

static LRESULT CALLBACK proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_TRAY) {
        if (lp == WM_LBUTTONUP || lp == WM_RBUTTONUP || lp == WM_CONTEXTMENU) show_menu();
        return 0;
    }
    if (msg == taskbar_created && taskbar_created) { add_icon(); return 0; } /* explorer restarted */
    if (msg == WM_CLOSE) { DestroyWindow(hwnd); return 0; }
    if (msg == WM_DESTROY) {
        Shell_NotifyIconA(NIM_DELETE, &nid);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcA(hwnd, msg, wp, lp);
}

int tray_init(const char *tooltip, const tray_menu *menu, void (*on_click)(int)) {
    click_cb = on_click;
    InitializeCriticalSection(&lock);
    current = *menu;
    strncpy(tip, tooltip, sizeof(tip) - 1);
    WNDCLASSA wc = {0};
    wc.lpfnWndProc = proc;
    wc.hInstance = GetModuleHandleA(NULL);
    wc.lpszClassName = "KomuTrackerTray";
    if (!RegisterClassA(&wc)) return -1;
    window = CreateWindowExA(0, wc.lpszClassName, "KomuTracker", 0, 0, 0, 0, 0, NULL, NULL, wc.hInstance, NULL);
    if (!window) return -1;
    taskbar_created = RegisterWindowMessageA("TaskbarCreated");
    add_icon();
    return 0;
}

void tray_set_menu(const tray_menu *menu) {
    EnterCriticalSection(&lock);
    current = *menu;
    LeaveCriticalSection(&lock);
}

void tray_run(void) {
    MSG msg;
    while (GetMessageA(&msg, NULL, 0, 0) > 0) { TranslateMessage(&msg); DispatchMessageA(&msg); }
}

void tray_set_label(const char *text) { (void)text; }
void tray_quit(void) { PostMessageA(window, WM_CLOSE, 0, 0); }
#endif
