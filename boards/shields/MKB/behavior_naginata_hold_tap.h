#pragma once

#include <stdint.h>

enum naginata_hold_tap_pressed_state {
    NAGINATA_HOLD_TAP_PRESSED_NONE = 0,
    NAGINATA_HOLD_TAP_PRESSED_PENDING = 1 << 0,
    NAGINATA_HOLD_TAP_PRESSED_EXPIRED = 1 << 1,
    NAGINATA_HOLD_TAP_PRESSED_TAP = 1 << 2,
    NAGINATA_HOLD_TAP_PRESSED_HOLD = 1 << 3,
};

uint8_t naginata_hold_tap_pressed_positions(const int32_t *positions, int32_t positions_len,
                                            int64_t timestamp);
int naginata_hold_tap_resolve_pending_positions(const int32_t *positions, int32_t positions_len,
                                                int64_t timestamp);
int naginata_hold_tap_resolve_hold_positions(const int32_t *positions, int32_t positions_len,
                                             int64_t timestamp);
