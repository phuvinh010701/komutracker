#include "afk.h"
#include "auth.h"
#include "http.h"
#include "instance.h"
#include "tracker.h"
#include "tray.h"

#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#endif

static volatile sig_atomic_t running = 1;
static volatile sig_atomic_t session = 1;
static void stop(int signal_number) { (void)signal_number; running = 0; }

static void usage(const char *program) {
    fprintf(stderr,
        "Usage: %s [--login|--logout|--status] [--no-tray] [--no-browser] [--testing] [-v]\n"
        "  [--server URL] [--token TOKEN] [--device-id ID] [--timeout SEC]\n"
        "  [--poll-time SEC]\n"
        "  [--auth-url URL] [--client-id ID]\n"
        "  [--redirect-uri URL] [--auth-timeout SEC] [--daemon] [--version]\n", program);
}

int main(int argc, char **argv) {
#ifdef _WIN32
    /* GUI-subsystem binary: reattach to the parent terminal so CLI output still shows. */
    if (AttachConsole(ATTACH_PARENT_PROCESS)) {
        freopen("CONOUT$", "w", stdout); freopen("CONOUT$", "w", stderr);
    }
#endif
    bool testing = false, verbose = false, login = false, logout = false, status = false, no_browser = false;
    bool version = false, daemon = false, no_tray = false;
    double timeout = 180.0, poll_time = 5.0;
    int auth_timeout = getenv("AW_AUTH_TIMEOUT") ? atoi(getenv("AW_AUTH_TIMEOUT")) : 300;
    const char *server = getenv("AW_SERVER_URL"), *token_arg = getenv("AW_AUTH_TOKEN");
    const char *device_arg = getenv("AW_DEVICE_ID");
    const char *auth_url = getenv("AW_AUTH_URL");
    const char *client_id = getenv("AW_CLIENT_ID");
    const char *redirect_uri = getenv("AW_REDIRECT_URI");

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--testing")) testing = true;
        else if (!strcmp(argv[i], "--verbose") || !strcmp(argv[i], "-v")) verbose = true;
        else if (!strcmp(argv[i], "--login")) login = true;
        else if (!strcmp(argv[i], "--logout")) logout = true;
        else if (!strcmp(argv[i], "--status")) status = true;
        else if (!strcmp(argv[i], "--no-browser")) no_browser = true;
        else if (!strcmp(argv[i], "--no-tray")) no_tray = true;
        else if (!strcmp(argv[i], "--daemon") || !strcmp(argv[i], "-d")) daemon = true;
        else if (!strcmp(argv[i], "--version") || !strcmp(argv[i], "-V")) version = true;
        else if (!strcmp(argv[i], "--timeout") && i + 1 < argc) timeout = strtod(argv[++i], NULL);
        else if (!strcmp(argv[i], "--poll-time") && i + 1 < argc) poll_time = strtod(argv[++i], NULL);
        else if (!strcmp(argv[i], "--auth-timeout") && i + 1 < argc) auth_timeout = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--server") && i + 1 < argc) server = argv[++i];
        else if (!strcmp(argv[i], "--token") && i + 1 < argc) token_arg = argv[++i];
        else if (!strcmp(argv[i], "--device-id") && i + 1 < argc) device_arg = argv[++i];
        else if (!strcmp(argv[i], "--auth-url") && i + 1 < argc) auth_url = argv[++i];
        else if (!strcmp(argv[i], "--client-id") && i + 1 < argc) client_id = argv[++i];
        else if (!strcmp(argv[i], "--redirect-uri") && i + 1 < argc) redirect_uri = argv[++i];
        else { usage(argv[0]); return 2; }
    }
    if ((login ? 1 : 0) + (logout ? 1 : 0) + (status ? 1 : 0) > 1) { usage(argv[0]); return 2; }
    if (version) { printf("komutracker %s\n", KOMUTRACKER_VERSION); return 0; }
    if (testing) { timeout = 20.0; poll_time = 1.0; }
    if (!server) server = testing ? "http://127.0.0.1:5666" : "https://tracker-api.komu.vn";
    if (!auth_url) auth_url = "https://oauth2.mezon.ai";
    if (!client_id) client_id = "1840672452439445504";
    if (!redirect_uri) redirect_uri = "https://tracker-api.komu.vn/api/0/auth/callback";
    if (timeout <= 0 || poll_time <= 0 || timeout < poll_time || auth_timeout <= 0) return 2;

    bool tray = !no_tray && !login && !logout && !status && !daemon && tray_available();
    if (tray) {
        char device[256];
        if (device_arg) snprintf(device, sizeof(device), "%s", device_arg);
        else if (auth_get_device_id(device, sizeof(device))) { fprintf(stderr, "Cannot load device ID\n"); return 1; }
        int lock_status = instance_lock();
        if (lock_status != 0) return lock_status < 0 ? 1 : 0;
        signal(SIGINT, stop); signal(SIGTERM, stop);
        app_config app = { server, token_arg, device,
            { auth_url, client_id, redirect_uri, auth_timeout, !no_browser },
            { timeout, poll_time, verbose } };
        return tray_app_run(&app, &running);
    }

    signal(SIGINT, stop); signal(SIGTERM, stop);
    http_set_running_flag(&running);
    if (http_global_init()) return 1;

    char device[256], saved_token[8192] = {0};
    if (device_arg) snprintf(device, sizeof(device), "%s", device_arg);
    else if (auth_get_device_id(device, sizeof(device))) { fprintf(stderr, "Cannot load device ID\n"); return 1; }
    const char *token = token_arg;
    if (!token && auth_read_token(saved_token, sizeof(saved_token)) == 0) token = saved_token;
    http_client client = { server, token, device, verbose };
    auth_options options = { auth_url, client_id, redirect_uri, auth_timeout, !no_browser };

    if (logout) {
        int remote = http_auth_delete(&client);
        auth_remove_token();
        http_global_cleanup();
        if (remote) { fprintf(stderr, "Local token removed; server logout failed\n"); return 1; }
        printf("Logged out\n"); return 0;
    }

    int verification = token ? http_auth_me(&client, NULL, 0, NULL, 0) : HTTP_AUTH_UNAUTHORIZED;
    if (login || verification == HTTP_AUTH_UNAUTHORIZED) {
        if (!token_arg) auth_remove_token();
        if (auth_login(&client, &options, saved_token, sizeof(saved_token), &running)) {
            http_global_cleanup(); return running ? 1 : 130;
        }
        token = saved_token;
    } else if (verification != HTTP_AUTH_OK) {
        fprintf(stderr, "Unable to verify authentication because the server is unavailable\n");
        http_global_cleanup(); return 1;
    }

    if (status || login) {
        char name[512], email[512];
        int result = http_auth_me(&client, name, sizeof(name), email, sizeof(email));
        http_global_cleanup();
        if (result != HTTP_AUTH_OK) return 1;
        printf("Authenticated as %s <%s>\n", name, email);
        if (status || login) return 0;
    }

    /* Once committed to tracking, ensure only one instance runs. Acquire the
       lock before detaching so a duplicate launch is reported to the terminal. */
    int lock_status = instance_lock();
    if (lock_status != 0) {
        if (lock_status < 0) {
            fprintf(stderr, "Failed to acquire instance lock\n");
        } else if (verbose) {
            fprintf(stderr, "Another komutracker instance is already running\n");
        }
        http_global_cleanup();
        return 1;
    }

    if (daemon) {
        if (instance_daemonize() != 0) {
            fprintf(stderr, "Failed to run as daemon\n");
            http_global_cleanup();
            return 1;
        }
    }

    tracker_config track = { timeout, poll_time, verbose };
    http_client tracking = { server, token, device, verbose };
    int result = tracker_run(&tracking, &track, &running, &session);
    http_global_cleanup();
    return result;
}
