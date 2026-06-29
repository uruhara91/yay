#pragma once
#include "json_config.h"

struct GameResult {
    int downscale_set   = 0;
    int logs_cleaned    = 0;
    int failed          = 0;
};

// Apply game mode config: per-package downscale via Android GameManager API,
// and periodic log cleanup (replace bind-mount with simple truncation).
GameResult apply_game_mode(const Json& cfg);
