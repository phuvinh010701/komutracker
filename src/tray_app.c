#include "afk.h"
#include "tracker.h"
#include "tray.h"

#include <curl/curl.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#ifdef _WIN32
#include <windows.h>
typedef HANDLE thread_t;
static DWORD WINAPI thread_main(LPVOID arg) { ((void (*)(void))arg)(); return 0; }
static int thread_start(thread_t *t, void (*fn)(void)) { return (*t = CreateThread(NULL, 0, thread_main, (LPVOID)fn, 0, NULL)) ? 0 : -1; }
static void thread_join(thread_t t) { WaitForSingleObject(t, INFINITE); CloseHandle(t); }
#else
#include <pthread.h>
typedef pthread_t thread_t;
static void *thread_main(void *arg) { ((void (*)(void))arg)(); return NULL; }
static int thread_start(thread_t *t, void (*fn)(void)) { return pthread_create(t, NULL, thread_main, (void *)fn); }
static void thread_join(thread_t t) { pthread_join(t, NULL); }
#endif

enum { ID_LOGIN = 1, ID_LOGOUT, ID_QUIT, ID_PROFILE };
enum { CMD_NONE, CMD_LOGIN, CMD_LOGOUT };
enum { ST_OFFLINE, ST_LOGGED_OUT, ST_LOGGING_IN, ST_TRACKING };

#define RETRY_SECONDS 30

static const app_config *cfg;
static volatile sig_atomic_t *running;
static volatile sig_atomic_t session, command;
static char stats_user[512];
static char profile_url[1024]; /* set by the worker before the menu item becomes clickable */

/* Fixed layout (ids are bound at tray_init); show() only toggles label/enabled/visible. */
static void fill(tray_menu *menu) {
    memset(menu, 0, sizeof(*menu));
    menu->count = 6;
    menu->items[0] = (tray_item){ "KomuTracker", ID_PROFILE, 0, 1 };
    menu->items[1] = (tray_item){ "-", 0, 0, 1 };
    menu->items[2] = (tray_item){ "Login", ID_LOGIN, 1, 0 };
    menu->items[3] = (tray_item){ "Logout", ID_LOGOUT, 1, 0 };
    menu->items[4] = (tray_item){ "-", 0, 0, 1 };
    menu->items[5] = (tray_item){ "Quit KomuTracker", ID_QUIT, 1, 1 };
}

/* Today's active time (GMT+7 day, same as the web report), shown next to the tray icon. */
static void refresh_active(const http_client *client) {
    static char events[65536];
    long local = (long)time(NULL) + 7 * 3600;
    double from = (double)(local - local % 86400 - 7 * 3600), to = from + 86400;
    char start[32], end[32], label[64];
    if (afk_format_timestamp(start, sizeof(start), from) || afk_format_timestamp(end, sizeof(end), to)) return;
    if (http_get_events(client, stats_user, start, end, events, sizeof(events))) return;
    long minutes = (long)(afk_active_seconds(events, from, to) / 60);
    snprintf(label, sizeof(label), "Active: %ldh %ldm", minutes / 60, minutes % 60);
    tray_set_label(label);
}

static void show(int state, const char *user) {
    if (state != ST_TRACKING) tray_set_label("");
    tray_menu menu;
    fill(&menu);
    switch (state) {
    case ST_OFFLINE: snprintf(menu.items[0].label, sizeof(menu.items[0].label), "Server unavailable, retrying..."); break;
    case ST_LOGGED_OUT: snprintf(menu.items[0].label, sizeof(menu.items[0].label), "Not signed in"); break;
    case ST_LOGGING_IN: snprintf(menu.items[0].label, sizeof(menu.items[0].label), "Waiting for browser login..."); break;
    default: snprintf(menu.items[0].label, sizeof(menu.items[0].label), "%s", user);
             menu.items[0].enabled = profile_url[0] != 0; break;
    }
    menu.items[2].visible = state == ST_LOGGED_OUT || state == ST_LOGGING_IN;
    menu.items[2].enabled = state == ST_LOGGED_OUT;
    menu.items[3].visible = state == ST_TRACKING;
    tray_set_menu(&menu);
}

static void on_click(int id) {
    if (id == ID_LOGIN) command = CMD_LOGIN;
    else if (id == ID_LOGOUT) { command = CMD_LOGOUT; session = 0; }
    else if (id == ID_PROFILE) { if (profile_url[0]) auth_open_browser(profile_url); }
    else if (id == ID_QUIT) { *running = 0; tray_quit(); }
}

static void nap(double seconds) {
    for (double left = seconds; left > 0 && *running && !command; left -= 0.2) tracker_sleep(0.2);
}

static void worker(void) {
    static char device[256], token[8192], name[512], email[512];
    const char *token_arg = cfg->token_arg;
    snprintf(device, sizeof(device), "%s", cfg->device_id);
    const char *active = token_arg;
    if (!active && auth_read_token(token, sizeof(token)) == 0) active = token;
    http_client client = { cfg->server, active, device, cfg->track.verbose };

    while (*running) {
        client.token = active;
        int check = active ? http_auth_me(&client, name, sizeof(name), email, sizeof(email)) : HTTP_AUTH_UNAUTHORIZED;
        if (check == HTTP_AUTH_OK) {
            command = CMD_NONE;
            session = 1;
            char user[512];
            snprintf(user, sizeof(user), "%.*s", (int)strcspn(email, "@"), email);
            profile_url[0] = 0;
            if (user[0]) {
                CURL *curl = curl_easy_init();
                char *escaped = curl ? curl_easy_escape(curl, user, 0) : NULL;
                if (escaped) snprintf(profile_url, sizeof(profile_url), "https://tracker-api.komu.vn/?username=%s&timespan=today", escaped);
                curl_free(escaped); if (curl) curl_easy_cleanup(curl);
            }
            show(ST_TRACKING, user[0] ? user : name);
            snprintf(stats_user, sizeof(stats_user), "%s", user);
            tracker_config track = cfg->track;
            track.on_minute = refresh_active;
            int failed = tracker_run(&client, &track, running, &session);
            if (command == CMD_LOGOUT) {
                http_auth_delete(&client);
                auth_remove_token();
                active = token_arg = NULL;
                command = CMD_NONE;
            } else if (failed) {
                show(ST_OFFLINE, NULL);
                nap(RETRY_SECONDS);
            }
        } else if (check == HTTP_AUTH_UNAUTHORIZED) {
            if (active && !token_arg) auth_remove_token();
            active = NULL; client.token = NULL;
            show(ST_LOGGED_OUT, NULL);
            while (*running && command != CMD_LOGIN) tracker_sleep(0.2);
            if (!*running) break;
            command = CMD_NONE;
            show(ST_LOGGING_IN, NULL);
            if (auth_login(&client, &cfg->auth, token, sizeof(token), running) == 0) active = token;
        } else {
            show(ST_OFFLINE, NULL);
            nap(RETRY_SECONDS);
            command = CMD_NONE;
        }
    }
    tray_quit();
}

int tray_app_run(const app_config *config, volatile sig_atomic_t *run_flag) {
    cfg = config; running = run_flag;
    http_set_running_flag(run_flag);
    if (http_global_init()) return 1;
    tray_menu initial;
    fill(&initial);
    if (tray_init("KomuTracker", &initial, on_click)) {
        fprintf(stderr, "Unable to create the tray icon; use --no-tray\n");
        http_global_cleanup();
        return 1;
    }
    thread_t thread;
    if (thread_start(&thread, worker)) { http_global_cleanup(); return 1; }
    tray_run();
    *running = 0; session = 0;
    thread_join(thread);
    http_global_cleanup();
    return 0;
}
