#include "afk.h"
#include <assert.h>
#include <string.h>

int main(void) {
    afk_sample sample = afk_update(false, 10.0, 180.0);
    assert(!sample.afk && !sample.changed && sample.duration == 0.0);

    sample = afk_update(false, 181.0, 180.0);
    assert(sample.afk && sample.changed && sample.duration == 181.0);

    sample = afk_update(true, 1.0, 180.0);
    assert(!sample.afk && sample.changed);

    char timestamp[32];
    assert(afk_format_timestamp(timestamp, sizeof(timestamp), 0.001) == 0);
    assert(strcmp(timestamp, "1970-01-01T00:00:00.001Z") == 0);
    const char *events = "[{\"id\":\"1\",\"status\":\"not-afk\",\"startAt\":\"1970-01-01T00:00:00.000Z\",\"endAt\":\"1970-01-01T00:10:00.500Z\"},"
                         "{\"id\":\"2\",\"status\":\"afk\",\"startAt\":\"1970-01-01T00:10:00.500Z\",\"endAt\":\"1970-01-01T00:20:00.000Z\"},"
                         "{\"id\":\"3\",\"status\":\"not-afk\",\"startAt\":\"1970-01-01T00:20:00.000Z\",\"endAt\":\"1970-01-01T00:30:00.000Z\"}]";
    assert(afk_active_seconds(events, 0, 86400) == 1200.5);
    assert(afk_active_seconds(events, 300, 1500) == 600.5);
    return 0;
}
