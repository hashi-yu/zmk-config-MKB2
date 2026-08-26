#define DT_DRV_COMPAT zmk_behavior_naginata_hold_tap

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <drivers/behavior.h>
#include <zmk/behavior.h>
#include <zmk/event_manager.h>
#include <zmk/events/position_state_changed.h>
#include <zmk/matrix.h>
#include "behavior_naginata_hold_tap.h"

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#define NAGINATA_HOLD_TAP_MAX_ACTIVE 16

struct behavior_naginata_hold_tap_config {
    int32_t tapping_term_ms;
    const char *hold_behavior_dev;
    const char *tap_behavior_dev;
    const char *shortcut_behavior_dev;
    int32_t shortcut_key_position;
    uint32_t shortcut_keycode;
    const int32_t *tap_trigger_key_positions;
    int32_t tap_trigger_key_positions_len;
    const int32_t *hold_trigger_key_positions;
    int32_t hold_trigger_key_positions_len;
    bool tap_only;
};

enum naginata_hold_tap_state {
    NAGINATA_HOLD_TAP_UNUSED,
    NAGINATA_HOLD_TAP_PENDING,
    NAGINATA_HOLD_TAP_TAP,
    NAGINATA_HOLD_TAP_HOLD,
};

struct active_naginata_hold_tap {
    enum naginata_hold_tap_state state;
    int32_t position;
#if IS_ENABLED(CONFIG_ZMK_SPLIT)
    uint8_t source;
#endif
    uint32_t hold_param;
    uint32_t tap_param;
    int64_t timestamp;
    const struct behavior_naginata_hold_tap_config *config;
    struct k_work_delayable work;
    bool interrupted;
    bool shortcut;
};

static struct active_naginata_hold_tap
    active_hold_taps[NAGINATA_HOLD_TAP_MAX_ACTIVE];
static bool pressed_positions[ZMK_KEYMAP_LEN];

static int invoke_binding(struct active_naginata_hold_tap *active, bool tap, bool pressed,
                          int64_t timestamp) {
    const char *behavior_dev =
        tap && active->shortcut ? active->config->shortcut_behavior_dev
                                : tap ? active->config->tap_behavior_dev
                                      : active->config->hold_behavior_dev;
    uint32_t tap_param =
        active->shortcut && active->position == active->config->shortcut_key_position
            ? active->config->shortcut_keycode
            : active->tap_param;
    struct zmk_behavior_binding binding = {
        .behavior_dev = behavior_dev,
        .param1 = tap ? tap_param : active->hold_param,
    };
    struct zmk_behavior_binding_event event = {
        .position = active->position,
        .timestamp = timestamp,
#if IS_ENABLED(CONFIG_ZMK_SPLIT)
        .source = active->source,
#endif
    };

    return zmk_behavior_invoke_binding(&binding, event, pressed);
}

static int invoke_complete_tap(struct active_naginata_hold_tap *active, int64_t timestamp) {
    int err = invoke_binding(active, true, true, timestamp);
    if (err < 0) {
        return err;
    }
    return invoke_binding(active, true, false, timestamp);
}

static bool same_key(const struct active_naginata_hold_tap *active,
                     const struct zmk_behavior_binding_event *event) {
    if (active->state == NAGINATA_HOLD_TAP_UNUSED || active->position != event->position) {
        return false;
    }
#if IS_ENABLED(CONFIG_ZMK_SPLIT)
    return active->source == event->source;
#else
    return true;
#endif
}

static struct active_naginata_hold_tap *find_active(
    const struct zmk_behavior_binding_event *event) {
    for (int i = 0; i < NAGINATA_HOLD_TAP_MAX_ACTIVE; i++) {
        if (same_key(&active_hold_taps[i], event)) {
            return &active_hold_taps[i];
        }
    }
    return NULL;
}

static struct active_naginata_hold_tap *allocate_active(void) {
    for (int i = 0; i < NAGINATA_HOLD_TAP_MAX_ACTIVE; i++) {
        if (active_hold_taps[i].state == NAGINATA_HOLD_TAP_UNUSED) {
            return &active_hold_taps[i];
        }
    }
    return NULL;
}

static bool has_pending_or_tapped_key(void) {
    for (int i = 0; i < NAGINATA_HOLD_TAP_MAX_ACTIVE; i++) {
        enum naginata_hold_tap_state state = active_hold_taps[i].state;
        if (state == NAGINATA_HOLD_TAP_PENDING || state == NAGINATA_HOLD_TAP_TAP) {
            return true;
        }
    }
    return false;
}

static bool has_held_key(void) {
    for (int i = 0; i < NAGINATA_HOLD_TAP_MAX_ACTIVE; i++) {
        if (active_hold_taps[i].state == NAGINATA_HOLD_TAP_HOLD) {
            return true;
        }
    }
    return false;
}

static bool has_pressed_tap_trigger(
    const struct behavior_naginata_hold_tap_config *config) {
    for (int i = 0; i < config->tap_trigger_key_positions_len; i++) {
        int32_t position = config->tap_trigger_key_positions[i];
        if (position >= 0 && position < ZMK_KEYMAP_LEN && pressed_positions[position]) {
            return true;
        }
    }
    return false;
}


static bool position_in_list(int32_t position, const int32_t *positions, int32_t positions_len) {
    for (int i = 0; i < positions_len; i++) {
        if (positions[i] == position) {
            return true;
        }
    }
    return false;
}

uint8_t naginata_hold_tap_pressed_positions(const int32_t *positions, int32_t positions_len,
                                            int64_t timestamp) {
    uint8_t pressed_state = NAGINATA_HOLD_TAP_PRESSED_NONE;

    for (int i = 0; i < NAGINATA_HOLD_TAP_MAX_ACTIVE; i++) {
        struct active_naginata_hold_tap *active = &active_hold_taps[i];
        if (!position_in_list(active->position, positions, positions_len)) {
            continue;
        }

        switch (active->state) {
        case NAGINATA_HOLD_TAP_PENDING:
            if (timestamp >= active->timestamp + active->config->tapping_term_ms) {
                pressed_state |= NAGINATA_HOLD_TAP_PRESSED_EXPIRED;
            } else {
                pressed_state |= NAGINATA_HOLD_TAP_PRESSED_PENDING;
            }
            break;
        case NAGINATA_HOLD_TAP_TAP:
            pressed_state |= NAGINATA_HOLD_TAP_PRESSED_TAP;
            break;
        case NAGINATA_HOLD_TAP_HOLD:
            pressed_state |= NAGINATA_HOLD_TAP_PRESSED_HOLD;
            break;
        case NAGINATA_HOLD_TAP_UNUSED:
            break;
        }
    }

    return pressed_state;
}

int naginata_hold_tap_resolve_pending_positions(const int32_t *positions, int32_t positions_len,
                                                int64_t timestamp) {
    int first_error = 0;

    for (int i = 0; i < NAGINATA_HOLD_TAP_MAX_ACTIVE; i++) {
        struct active_naginata_hold_tap *active = &active_hold_taps[i];
        if (active->state != NAGINATA_HOLD_TAP_PENDING ||
            !position_in_list(active->position, positions, positions_len)) {
            continue;
        }

        k_work_cancel_delayable(&active->work);
        active->shortcut = false;
        active->state = NAGINATA_HOLD_TAP_TAP;
        int err = invoke_binding(active, true, true, timestamp);
        if (first_error == 0 && err < 0) {
            first_error = err;
        }
    }

    return first_error;
}

int naginata_hold_tap_resolve_hold_positions(const int32_t *positions, int32_t positions_len,
                                             int64_t timestamp) {
    int first_error = 0;

    for (int i = 0; i < NAGINATA_HOLD_TAP_MAX_ACTIVE; i++) {
        struct active_naginata_hold_tap *active = &active_hold_taps[i];
        if (active->state != NAGINATA_HOLD_TAP_PENDING ||
            !position_in_list(active->position, positions, positions_len)) {
            continue;
        }

        k_work_cancel_delayable(&active->work);
        active->state = NAGINATA_HOLD_TAP_HOLD;
        active->interrupted = true;
        int err = invoke_binding(active, false, true, timestamp);
        if (first_error == 0 && err < 0) {
            first_error = err;
        }
    }

    return first_error;
}
static int resolve_pending_as_taps(int64_t timestamp) {
    int first_error = 0;

    for (int i = 0; i < NAGINATA_HOLD_TAP_MAX_ACTIVE; i++) {
        struct active_naginata_hold_tap *active = &active_hold_taps[i];
        if (active->state != NAGINATA_HOLD_TAP_PENDING) {
            continue;
        }

        active->shortcut = has_held_key();
        k_work_cancel_delayable(&active->work);
        active->state = NAGINATA_HOLD_TAP_TAP;
        int err = invoke_binding(active, true, true, timestamp);
        if (first_error == 0 && err < 0) {
            first_error = err;
        }
    }

    return first_error;
}

static void resolve_expired_holds(int64_t timestamp) {
    for (int i = 0; i < NAGINATA_HOLD_TAP_MAX_ACTIVE; i++) {
        struct active_naginata_hold_tap *active = &active_hold_taps[i];
        if (active->state != NAGINATA_HOLD_TAP_PENDING ||
            active->config->hold_trigger_key_positions_len > 0 ||
            timestamp < active->timestamp + active->config->tapping_term_ms) {
            continue;
        }

        k_work_cancel_delayable(&active->work);
        active->state = NAGINATA_HOLD_TAP_HOLD;
        invoke_binding(active, false, true, timestamp);
    }
}

static void hold_timer_handler(struct k_work *work) {
    struct k_work_delayable *delayable = k_work_delayable_from_work(work);
    struct active_naginata_hold_tap *active =
        CONTAINER_OF(delayable, struct active_naginata_hold_tap, work);

    if (active->state != NAGINATA_HOLD_TAP_PENDING) {
        return;
    }

    active->state = NAGINATA_HOLD_TAP_HOLD;
    invoke_binding(active, false, true, k_uptime_get());
}

static int on_binding_pressed(struct zmk_behavior_binding *binding,
                              struct zmk_behavior_binding_event event) {
    const struct device *dev = zmk_behavior_get_binding(binding->behavior_dev);
    const struct behavior_naginata_hold_tap_config *config = dev->config;

    if (find_active(&event) != NULL) {
        return -EALREADY;
    }

    resolve_expired_holds(event.timestamp);

    struct active_naginata_hold_tap *active = allocate_active();
    if (active == NULL) {
        return -ENOMEM;
    }

    bool joins_naginata_input;
    bool modifier_active = has_held_key();
    active->position = event.position;
#if IS_ENABLED(CONFIG_ZMK_SPLIT)
    active->source = event.source;
#endif
    active->hold_param = config->tap_only ? 0 : binding->param1;
    active->tap_param = config->tap_only ? binding->param1 : binding->param2;
    active->timestamp = event.timestamp;
    active->config = config;
    active->interrupted = false;
    active->shortcut = modifier_active;
    joins_naginata_input =
        has_pending_or_tapped_key() || modifier_active || has_pressed_tap_trigger(config);

    if (config->tap_only || joins_naginata_input) {
        int err = resolve_pending_as_taps(event.timestamp);
        active->state = NAGINATA_HOLD_TAP_TAP;
        int press_err = invoke_binding(active, true, true, event.timestamp);
        return err < 0 ? err : press_err;
    }

    active->state = NAGINATA_HOLD_TAP_PENDING;
    if (config->hold_trigger_key_positions_len == 0) {
        k_work_reschedule(&active->work, K_MSEC(config->tapping_term_ms));
    }
    return 0;
}

static int on_binding_released(struct zmk_behavior_binding *binding,
                               struct zmk_behavior_binding_event event) {
    struct active_naginata_hold_tap *active = find_active(&event);
    if (active == NULL) {
        return -ENOENT;
    }

    resolve_expired_holds(event.timestamp);

    int err = 0;
    switch (active->state) {
    case NAGINATA_HOLD_TAP_PENDING:
        k_work_cancel_delayable(&active->work);
        active->state = NAGINATA_HOLD_TAP_TAP;
        err = invoke_binding(active, true, true, event.timestamp);
        if (err >= 0) {
            err = invoke_binding(active, true, false, event.timestamp);
        }
        break;
    case NAGINATA_HOLD_TAP_TAP:
        err = invoke_binding(active, true, false, event.timestamp);
        break;
    case NAGINATA_HOLD_TAP_HOLD: {
        int release_err = invoke_binding(active, false, false, event.timestamp);
        if (active->interrupted) {
            err = release_err;
            break;
        }

        int tap_err = invoke_complete_tap(active, event.timestamp);
        err = release_err < 0 ? release_err : tap_err;
        break;
    }
    case NAGINATA_HOLD_TAP_UNUSED:
        err = -ENOENT;
        break;
    }

    active->state = NAGINATA_HOLD_TAP_UNUSED;
    active->shortcut = false;
    active->config = NULL;
    return err;
}

static const struct behavior_driver_api behavior_naginata_hold_tap_driver_api = {
    .binding_pressed = on_binding_pressed,
    .binding_released = on_binding_released,
#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
    .get_parameter_metadata = zmk_behavior_get_empty_param_metadata,
#endif
};

static int naginata_hold_tap_position_state_changed_listener(const zmk_event_t *eh) {
    const struct zmk_position_state_changed *event = as_zmk_position_state_changed(eh);
    if (event != NULL && event->position < ZMK_KEYMAP_LEN) {
        pressed_positions[event->position] = event->state;
    }
    if (event == NULL || !event->state) {
        return ZMK_EV_EVENT_BUBBLE;
    }

    for (int i = 0; i < NAGINATA_HOLD_TAP_MAX_ACTIVE; i++) {
        struct active_naginata_hold_tap *active = &active_hold_taps[i];
        if (active->state != NAGINATA_HOLD_TAP_PENDING &&
            active->state != NAGINATA_HOLD_TAP_HOLD) {
            continue;
        }

        if (active->position == event->position
#if IS_ENABLED(CONFIG_ZMK_SPLIT)
            && active->source == event->source
#endif
        ) {
            continue;
        }

        active->interrupted = true;
        if (active->state == NAGINATA_HOLD_TAP_HOLD ||
            active->config->hold_trigger_key_positions_len == 0 ||
            position_in_list(event->position, active->config->tap_trigger_key_positions,
                             active->config->tap_trigger_key_positions_len)) {
            continue;
        }

        k_work_cancel_delayable(&active->work);
        bool hold_trigger =
            position_in_list(event->position, active->config->hold_trigger_key_positions,
                             active->config->hold_trigger_key_positions_len);
        bool tapping_term_expired =
            event->timestamp >= active->timestamp + active->config->tapping_term_ms;
        if (hold_trigger && tapping_term_expired) {
            active->state = NAGINATA_HOLD_TAP_HOLD;
            invoke_binding(active, false, true, event->timestamp);
        } else {
            active->shortcut = has_held_key();
            active->state = NAGINATA_HOLD_TAP_TAP;
            invoke_binding(active, true, true, event->timestamp);
        }
    }

    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(naginata_hold_tap, naginata_hold_tap_position_state_changed_listener);
ZMK_SUBSCRIPTION(naginata_hold_tap, zmk_position_state_changed);

static int behavior_naginata_hold_tap_init(const struct device *dev) {
    static bool initialized;

    if (!initialized) {
        for (int i = 0; i < NAGINATA_HOLD_TAP_MAX_ACTIVE; i++) {
            k_work_init_delayable(&active_hold_taps[i].work, hold_timer_handler);
        }
        initialized = true;
    }
    return 0;
}

#define NAGINATA_HOLD_TAP_TRIGGER_POSITION(idx, n)                                                 \
    DT_INST_PROP_BY_IDX(n, tap_trigger_key_positions, idx)

#define NAGINATA_HOLD_TAP_TRIGGER_POSITIONS(n)                                                     \
    static const int32_t behavior_naginata_hold_tap_trigger_positions_##n[] =                      \
        COND_CODE_1(DT_INST_NODE_HAS_PROP(n, tap_trigger_key_positions),                           \
                    ({LISTIFY(DT_INST_PROP_LEN(n, tap_trigger_key_positions),                      \
                              NAGINATA_HOLD_TAP_TRIGGER_POSITION, (, ), n)}), ({}));

#define NAGINATA_HOLD_TAP_HOLD_TRIGGER_POSITION(idx, n)                                            \
    DT_INST_PROP_BY_IDX(n, hold_trigger_key_positions, idx)

#define NAGINATA_HOLD_TAP_HOLD_TRIGGER_POSITIONS(n)                                                \
    static const int32_t behavior_naginata_hold_tap_hold_trigger_positions_##n[] = {               \
        COND_CODE_1(DT_INST_NODE_HAS_PROP(n, hold_trigger_key_positions),                          \
                    (LISTIFY(DT_INST_PROP_LEN(n, hold_trigger_key_positions),                      \
                             NAGINATA_HOLD_TAP_HOLD_TRIGGER_POSITION, (, ), n)), ())};

#define NAGINATA_HOLD_TAP_INST(n)                                                                  \
    NAGINATA_HOLD_TAP_TRIGGER_POSITIONS(n)                                                         \
    NAGINATA_HOLD_TAP_HOLD_TRIGGER_POSITIONS(n)                                                    \
    static const struct behavior_naginata_hold_tap_config                                          \
        behavior_naginata_hold_tap_config_##n = {                                                  \
            .tapping_term_ms = DT_INST_PROP(n, tapping_term_ms),                                   \
            .hold_behavior_dev = DEVICE_DT_NAME(DT_INST_PHANDLE_BY_IDX(n, bindings, 0)),           \
            .tap_behavior_dev = DEVICE_DT_NAME(DT_INST_PHANDLE_BY_IDX(n, bindings, 1)),            \
            .shortcut_behavior_dev = DEVICE_DT_NAME(DT_INST_PHANDLE(n, shortcut_binding)),         \
            .shortcut_key_position = DT_INST_PROP_OR(n, shortcut_key_position, -1),                 \
            .shortcut_keycode = DT_INST_PROP_OR(n, shortcut_keycode, 0),                           \
            .tap_trigger_key_positions = behavior_naginata_hold_tap_trigger_positions_##n,         \
            .tap_trigger_key_positions_len =                                                       \
                DT_INST_PROP_LEN_OR(n, tap_trigger_key_positions, 0),                              \
            .hold_trigger_key_positions =                                                         \
                behavior_naginata_hold_tap_hold_trigger_positions_##n,                            \
            .hold_trigger_key_positions_len =                                                      \
                DT_INST_PROP_LEN_OR(n, hold_trigger_key_positions, 0),                            \
            .tap_only = false,                                                                     \
    };                                                                                             \
    BEHAVIOR_DT_INST_DEFINE(n, behavior_naginata_hold_tap_init, NULL, NULL,                        \
                            &behavior_naginata_hold_tap_config_##n, POST_KERNEL,                   \
                            CONFIG_KERNEL_INIT_PRIORITY_DEFAULT,                                  \
                            &behavior_naginata_hold_tap_driver_api);

DT_INST_FOREACH_STATUS_OKAY(NAGINATA_HOLD_TAP_INST)

#define NAGINATA_KEY_INST(node_id)                                                                 \
    static const struct behavior_naginata_hold_tap_config behavior_naginata_key_config_##node_id = { \
        .tap_behavior_dev = DEVICE_DT_NAME(DT_PHANDLE(node_id, binding)),                           \
        .shortcut_behavior_dev = DEVICE_DT_NAME(DT_PHANDLE(node_id, shortcut_binding)),            \
        .shortcut_key_position = DT_PROP_OR(node_id, shortcut_key_position, -1),                   \
        .shortcut_keycode = DT_PROP_OR(node_id, shortcut_keycode, 0),                              \
        .tap_only = true,                                                                          \
    };                                                                                             \
    BEHAVIOR_DT_DEFINE(node_id, behavior_naginata_hold_tap_init, NULL, NULL,                       \
                       &behavior_naginata_key_config_##node_id, POST_KERNEL,                       \
                       CONFIG_KERNEL_INIT_PRIORITY_DEFAULT,                                       \
                       &behavior_naginata_hold_tap_driver_api);

DT_FOREACH_STATUS_OKAY(zmk_behavior_naginata_key, NAGINATA_KEY_INST)
