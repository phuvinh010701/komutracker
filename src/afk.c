#include "afk.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#define timegm _mkgmtime
#endif

afk_sample afk_update(bool was_afk, double idle, double timeout) {
    afk_sample sample = {0};
    sample.afk = idle >= timeout;
    sample.changed = sample.afk != was_afk;
    sample.idle_seconds = idle;
    sample.event_offset = -idle;
    sample.duration = sample.afk ? idle : 0.0;
    return sample;
}

int afk_format_timestamp(char *buffer, size_t size, double unix_seconds) {
    time_t whole = (time_t)unix_seconds;
    int millis = (int)((unix_seconds - (double)whole) * 1000.0 + 0.5);
    if (millis >= 1000) {
        whole++;
        millis = 0;
    }
    struct tm utc;
#ifdef _WIN32
    if (gmtime_s(&utc, &whole) != 0) return -1;
#else
    if (gmtime_r(&whole, &utc) == NULL) return -1;
#endif
    return snprintf(buffer, size, "%04d-%02d-%02dT%02d:%02d:%02d.%03dZ",
                    utc.tm_year + 1900, utc.tm_mon + 1, utc.tm_mday,
                    utc.tm_hour, utc.tm_min, utc.tm_sec, millis) >= (int)size ? -1 : 0;
}

static double parse_timestamp(const char *s) {
    struct tm t = {0};
    int year;
    if (memchr(s, 0, 20) || sscanf(s, "%4d-%2d-%2dT%2d:%2d:%2d", &year, &t.tm_mon, &t.tm_mday, &t.tm_hour, &t.tm_min, &t.tm_sec) != 6 ||
        (s[19] != '.' && s[19] != 'Z')) return -1;
    t.tm_year = year - 1900;
    t.tm_mon--;
    return (double)timegm(&t) + (s[19] == '.' ? strtod(s + 19, NULL) : 0.0);
}

/* ponytail: relies on the server's key order (status, startAt, endAt) and compact JSON; use a real parser if that changes. */
double afk_active_seconds(const char *json, double from, double to) {
    double total = 0.0;
    for (const char *p = json; (p = strstr(p, "\"status\":\"")); p += 10) {
        const char *start = strstr(p, "\"startAt\":\""), *end = strstr(p, "\"endAt\":\"");
        if (!start || !end) break;
        if (strncmp(p + 10, "not-afk", 7)) continue;
        double a = parse_timestamp(start + 11), b = parse_timestamp(end + 9);
        if (a < from) a = from;
        if (b > to) b = to;
        if (b > a) total += b - a;
    }
    return total;
}
