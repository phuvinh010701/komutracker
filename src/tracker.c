#include "tracker.h"
#include "afk.h"
#include "idle.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

static bool verbose_logging;

void tracker_sleep(double seconds) {
#ifdef _WIN32
    Sleep((DWORD)(seconds * 1000.0));
#else
    usleep((useconds_t)(seconds * 1000000.0));
#endif
}

static double now_seconds(void) {
#ifdef _WIN32
    FILETIME ft; GetSystemTimeAsFileTime(&ft);
    ULARGE_INTEGER value = { .LowPart = ft.dwLowDateTime, .HighPart = ft.dwHighDateTime };
    return (double)(value.QuadPart - 116444736000000000ULL) / 10000000.0;
#else
    struct timespec value; clock_gettime(CLOCK_REALTIME, &value);
    return (double)value.tv_sec + (double)value.tv_nsec / 1000000000.0;
#endif
}

static int now_local(char *buffer, size_t size) {
    time_t whole = time(NULL);
    struct tm local;
#ifdef _WIN32
    if (localtime_s(&local, &whole) != 0) return -1;
#else
    if (localtime_r(&whole, &local) == NULL) return -1;
#endif
    return snprintf(buffer, size, "%02d:%02d:%02d", local.tm_hour, local.tm_min, local.tm_sec) >= (int)size ? -1 : 0;
}

static int get_hostname(char *buffer, size_t size) {
#ifdef _WIN32
    DWORD length = (DWORD)size; return GetComputerNameA(buffer, &length) ? 0 : -1;
#else
    return gethostname(buffer, size) == 0 ? 0 : -1;
#endif
}

static void log_send(const char *event, int result) {
    if (!verbose_logging) return;
    char stamp[16];
    if (now_local(stamp, sizeof(stamp)) == 0)
        fprintf(stderr, "komutracker %s [%s] %s: %s\n", KOMUTRACKER_VERSION, stamp, event,
                result == 0 ? "OK" : "FAILED");
}

static void log_info(const char *message) {
    if (!verbose_logging) return;
    char stamp[16];
    if (now_local(stamp, sizeof(stamp)) == 0)
        fprintf(stderr, "komutracker %s [%s] %s\n", KOMUTRACKER_VERSION, stamp, message);
}

/* Sleep up to `seconds`, waking early when tracking is asked to stop. */
static void interruptible_sleep(double seconds, volatile sig_atomic_t *running,
                                volatile sig_atomic_t *session) {
    while (seconds > 0 && *running && *session) {
        double step = seconds < 0.2 ? seconds : 0.2;
        tracker_sleep(step);
        seconds -= step;
    }
}

int tracker_run(const http_client *client, const tracker_config *config,
                volatile sig_atomic_t *running, volatile sig_atomic_t *session) {
    verbose_logging = config->verbose;
    bool verbose = config->verbose;
    char host[256] = {0}; if (get_hostname(host, sizeof(host) - 1)) strcpy(host, "unknown");
    char name[512] = {0}, email[512] = {0};
    int profile = http_auth_me(client, name, sizeof(name), email, sizeof(email));
    char username[512] = {0};
    if (profile == HTTP_AUTH_OK) {
        size_t at = strcspn(email, "@");
        snprintf(username, sizeof(username), "%.*s", (int)at, email);
        if (verbose) {
            char message[1080];
            snprintf(message, sizeof(message), "logged in as %s <%s>", name, email);
            log_info(message);
        }
    } else {
        strcpy(username, host);
        if (verbose) log_info("logged-in user unavailable; using hostname for bucket name");
    }
    char afk_bucket[512];
    snprintf(afk_bucket, sizeof(afk_bucket), "aw-watcher-afk_%s", username);
    if (verbose) {
        char message[1080];
        snprintf(message, sizeof(message), "afk bucket: %s", afk_bucket);
        log_info(message);
    }
    if (idle_init()) return 1;
    if (http_create_bucket(client, afk_bucket, "aw-watcher-afk", "afkstatus", host) && verbose)
        fprintf(stderr, "Unable to create AFK bucket\n");

    double timeout = config->timeout, poll_time = config->poll_time;
    bool afk = false;
    double next_afk = now_seconds(), next_minute = 0;
    if (verbose) fprintf(stderr, "komutracker %s started for %s\n", KOMUTRACKER_VERSION, client->base_url);
    while (*running && *session) {
        double now = now_seconds();
        if (now >= next_afk) {
            double idle = idle_seconds();
            if (idle >= 0) {
                afk_sample sample = afk_update(afk, idle, timeout);
                double last_input = now + sample.event_offset;
                char timestamp[32];
                if (sample.changed) {
                    if (!afk_format_timestamp(timestamp, sizeof(timestamp), last_input))
                        log_send("afk heartbeat", http_heartbeat(client, afk_bucket, timestamp, 0, afk, timeout + poll_time));
                    if (!afk_format_timestamp(timestamp, sizeof(timestamp), last_input + 0.001))
                        log_send("afk heartbeat", http_heartbeat(client, afk_bucket, timestamp, sample.duration, sample.afk, timeout + poll_time));
                } else if (!afk_format_timestamp(timestamp, sizeof(timestamp), last_input))
                    log_send("afk heartbeat", http_heartbeat(client, afk_bucket, timestamp, sample.duration, sample.afk, timeout + poll_time));
                afk = sample.afk;
            }
            next_afk = now + poll_time;
        }

        if (config->on_minute && now >= next_minute) {
            config->on_minute(client);
            next_minute = now + 60;
        }

        double delay = next_afk - now_seconds();
        if (delay < 0.01) delay = 0.01;
        interruptible_sleep(delay, running, session);
    }
    idle_cleanup();
    return 0;
}
