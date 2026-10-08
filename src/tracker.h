#ifndef TRACKER_TRACKER_H
#define TRACKER_TRACKER_H

#include "auth.h"
#include "http.h"
#include <signal.h>
#include <stdbool.h>

typedef struct {
    double timeout, poll_time;
    bool verbose;
    void (*on_minute)(const http_client *); /* optional; called about once a minute from the tracking thread */
} tracker_config;

/* Blocks, sending heartbeats until *running or *session becomes 0.
   Returns 0 on a clean stop, non-zero if the platform backends cannot start. */
int tracker_run(const http_client *client, const tracker_config *config,
                volatile sig_atomic_t *running, volatile sig_atomic_t *session);

void tracker_sleep(double seconds);

typedef struct {
    const char *server, *token_arg, *device_id;
    auth_options auth;
    tracker_config track;
} app_config;

/* Tray mode: owns the calling (main) thread until Quit or *running == 0. */
int tray_app_run(const app_config *config, volatile sig_atomic_t *running);

#endif
