#pragma once
#include "json_config.h"

struct IOResult {
    int  configured = 0;
    int  skipped    = 0;
    int  failed     = 0;
};

// Apply I/O scheduler config from io_config.json.
// Iterates /sys/block/*/queue, skips ram/zram/dm-*, applies preferred scheduler.
IOResult apply_io_scheduler(const Json& cfg);
