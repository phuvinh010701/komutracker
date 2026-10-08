#ifndef AW_AFk_H
#define AW_AFk_H

#include <stdbool.h>
#include <stddef.h>

typedef struct {
    bool afk;
    bool changed;
    double idle_seconds;
    double event_offset;
    double duration;
} afk_sample;

afk_sample afk_update(bool was_afk, double idle_seconds, double timeout);
int afk_format_timestamp(char *buffer, size_t size, double unix_seconds);

/* Sum of "not-afk" events in the server's events JSON, clipped to [from, to] unix seconds. */
double afk_active_seconds(const char *json, double from, double to);

#endif
