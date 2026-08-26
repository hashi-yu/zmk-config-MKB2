#define DT_DRV_COMPAT zmk_behavior_naginata_space

#include <zephyr/device.h>
#include <zephyr/kernel.h>

#include <drivers/behavior.h>
#include <zmk/behavior.h>
#include <zmk/event_manager.h>
#include <zmk/events/position_state_changed.h>
#include "behavior_naginata_hold_tap.h"

#define NAGINATA_SPACE_MAX_ACTIVE 2

enum naginata_space_state {
    NAGINATA_SPACE_UNUSED,
    NAGINATA_SPACE_HOLD,
    NAGINATA_SPACE_TAP,
    NAGINATA_SPACE_SHORTCUT,
};

struct behavior_naginata_space_config {
    int32_t tapping_term_ms;
    const char *hold_behavior_dev;
    const char *tap_behavior_dev;
    const char *shortcut_behavior_dev;
    const int32_t *tap_trigger_key_positions;
    int32_t tap_trigger_key_positions_len;
};

struct active_naginata_space {
    enum naginata_space_state state;
    int32_t position;
#if IS_ENABLED(CONFIG_ZMK_SPLIT)
    uint8_t source;
#endif
    uint32_t hold_param;
    uint32_t tap_param;
    bool hold_committed;
    bool interrupted;
    const struct behavior_naginata_space_config *config;
    struct k_work_delayable work;
};

static struct active_naginata_space active_spaces[NAGINATA_SPACE_MAX_ACTIVE];

static int invoke_binding(struct active_naginata_space *active, bool tap, bool pressed,
                          int64_t timestamp) {
    struct zmk_behavior_binding binding = {
        .behavior_dev = tap ? active->config->tap_behavior_dev
                            : active->config->hold_behavior_dev,
        .param1 = tap ? active->tap_param : active->hold_param,
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

static int invoke_shortcut_binding(struct active_naginata_space *active, bool pressed,
                                   int64_t timestamp) {
    struct zmk_behavior_binding binding = {
        .behavior_dev = active->config->shortcut_behavior_dev,
        .param1 = active->tap_param,
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

static bool same_key(const struct active_naginata_space *active,
                     const struct zmk_behavior_binding_event *event) {
    if (active->state == NAGINATA_SPACE_UNUSED || active->position != event->position) {
        return false;
    }
#if IS_ENABLED(CONFIG_ZMK_SPLIT)
    return active->source == event->source;
#else
    return true;
#endif
}

static struct active_naginata_space *find_active(
    const struct zmk_behavior_binding_event *event) {
    for (int i = 0; i < NAGINATA_SPACE_MAX_ACTIVE; i++) {
        if (same_key(&active_spaces[i], event)) {
            return &active_spaces[i];
        }
    }
    return NULL;
}

static struct active_naginata_space *allocate_active(void) {
    for (int i = 0; i < NAGINATA_SPACE_MAX_ACTIVE; i++) {
        if (active_spaces[i].state == NAGINATA_SPACE_UNUSED) {
            return &active_spaces[i];
        }
    }
    return NULL;
}

static bool is_tap_trigger(const struct active_naginata_space *active, int32_t position) {
    for (int i = 0; i < active->config->tap_trigger_key_positions_len; i++) {
        if (active->config->tap_trigger_key_positions[i] == position) {
            return true;
        }
    }
    return false;
}

static bool same_position(const struct active_naginata_space *active,
                          const struct zmk_position_state_changed *event) {
    if (active->position != event->position) {
        return false;
    }
#if IS_ENABLED(CONFIG_ZMK_SPLIT)
    return active->source == event->source;
#else
    return true;
#endif
}

static void hold_timer_handler(struct k_work *work) {
    struct k_work_delayable *delayable = k_work_delayable_from_work(work);
    struct active_naginata_space *active =
        CONTAINER_OF(delayable, struct active_naginata_space, work);

    if (active->state == NAGINATA_SPACE_HOLD) {
        active->hold_committed = true;
    }
}

static int on_binding_pressed(struct zmk_behavior_binding *binding,
                              struct zmk_behavior_binding_event event) {
    if (find_active(&event) != NULL) {
        return -EALREADY;
    }

    struct active_naginata_space *active = allocate_active();
    if (active == NULL) {
        return -ENOMEM;
    }

    const struct device *dev = zmk_behavior_get_binding(binding->behavior_dev);
    active->position = event.position;
#if IS_ENABLED(CONFIG_ZMK_SPLIT)
    active->source = event.source;
#endif
    active->hold_param = binding->param1;
    active->tap_param = binding->param2;
    active->config = dev->config;
    active->hold_committed = false;
    active->interrupted = false;

    uint8_t pressed_state = naginata_hold_tap_pressed_positions(
        active->config->tap_trigger_key_positions,
        active->config->tap_trigger_key_positions_len, event.timestamp);
    if (pressed_state & NAGINATA_HOLD_TAP_PRESSED_EXPIRED) {
        int hold_err = naginata_hold_tap_resolve_hold_positions(
            active->config->tap_trigger_key_positions,
            active->config->tap_trigger_key_positions_len, event.timestamp);
        active->state = NAGINATA_SPACE_SHORTCUT;
        int shortcut_err = invoke_shortcut_binding(active, true, event.timestamp);
        return hold_err < 0 ? hold_err : shortcut_err;
    }
    if (pressed_state & NAGINATA_HOLD_TAP_PRESSED_PENDING) {
        active->state = NAGINATA_SPACE_TAP;
        int space_err = invoke_binding(active, true, true, event.timestamp);
        if (space_err < 0) {
            return space_err;
        }
        return naginata_hold_tap_resolve_pending_positions(
            active->config->tap_trigger_key_positions,
            active->config->tap_trigger_key_positions_len, event.timestamp);
    }
    if (pressed_state & NAGINATA_HOLD_TAP_PRESSED_TAP) {
        active->state = NAGINATA_SPACE_TAP;
        return invoke_binding(active, true, true, event.timestamp);
    }
    if (pressed_state & NAGINATA_HOLD_TAP_PRESSED_HOLD) {
        active->state = NAGINATA_SPACE_SHORTCUT;
        return invoke_shortcut_binding(active, true, event.timestamp);
    }

    active->state = NAGINATA_SPACE_HOLD;
    k_work_reschedule(&active->work, K_MSEC(active->config->tapping_term_ms));
    return invoke_binding(active, false, true, event.timestamp);
}

static int on_binding_released(struct zmk_behavior_binding *binding,
                               struct zmk_behavior_binding_event event) {
    struct active_naginata_space *active = find_active(&event);
    if (active == NULL) {
        return -ENOENT;
    }

    k_work_cancel_delayable(&active->work);

    int err;
    if (active->state == NAGINATA_SPACE_TAP) {
        err = invoke_binding(active, true, false, event.timestamp);
    } else if (active->state == NAGINATA_SPACE_SHORTCUT) {
        err = invoke_shortcut_binding(active, false, event.timestamp);
    } else {
        int release_err = invoke_binding(active, false, false, event.timestamp);
        if (active->hold_committed || active->interrupted) {
            err = release_err;
        } else {
            int press_err = invoke_binding(active, true, true, event.timestamp);
            int tap_release_err =
                press_err < 0 ? press_err : invoke_binding(active, true, false, event.timestamp);
            err = release_err < 0 ? release_err : tap_release_err;
        }
    }

    active->state = NAGINATA_SPACE_UNUSED;
    active->config = NULL;
    return err;
}

static const struct behavior_driver_api behavior_naginata_space_driver_api = {
    .binding_pressed = on_binding_pressed,
    .binding_released = on_binding_released,
#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
    .get_parameter_metadata = zmk_behavior_get_empty_param_metadata,
#endif
};

static int naginata_space_position_state_changed_listener(const zmk_event_t *eh) {
    const struct zmk_position_state_changed *event = as_zmk_position_state_changed(eh);
    if (event == NULL || !event->state) {
        return ZMK_EV_EVENT_BUBBLE;
    }

    for (int i = 0; i < NAGINATA_SPACE_MAX_ACTIVE; i++) {
        struct active_naginata_space *active = &active_spaces[i];
        if (active->state != NAGINATA_SPACE_HOLD || same_position(active, event)) {
            continue;
        }

        if (!is_tap_trigger(active, event->position)) {
            active->interrupted = true;
            continue;
        }

        k_work_cancel_delayable(&active->work);
        int err = invoke_binding(active, false, false, event->timestamp);
        if (err < 0) {
            continue;
        }

        active->state = NAGINATA_SPACE_TAP;
        invoke_binding(active, true, true, event->timestamp);
    }

    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(naginata_space, naginata_space_position_state_changed_listener);
ZMK_SUBSCRIPTION(naginata_space, zmk_position_state_changed);

static int behavior_naginata_space_init(const struct device *dev) {
    static bool initialized;

    if (!initialized) {
        for (int i = 0; i < NAGINATA_SPACE_MAX_ACTIVE; i++) {
            k_work_init_delayable(&active_spaces[i].work, hold_timer_handler);
        }
        initialized = true;
    }
    return 0;
}

#define NAGINATA_SPACE_TRIGGER_POSITION(idx, n)                                                    \
    DT_INST_PROP_BY_IDX(n, tap_trigger_key_positions, idx)

#define NAGINATA_SPACE_INST(n)                                                                     \
    static const int32_t behavior_naginata_space_trigger_positions_##n[] = {                       \
        LISTIFY(DT_INST_PROP_LEN(n, tap_trigger_key_positions),                                    \
                NAGINATA_SPACE_TRIGGER_POSITION, (, ), n)};                                        \
    static const struct behavior_naginata_space_config behavior_naginata_space_config_##n = {      \
        .tapping_term_ms = DT_INST_PROP(n, tapping_term_ms),                                      \
        .hold_behavior_dev = DEVICE_DT_NAME(DT_INST_PHANDLE_BY_IDX(n, bindings, 0)),               \
        .tap_behavior_dev = DEVICE_DT_NAME(DT_INST_PHANDLE_BY_IDX(n, bindings, 1)),                \
        .shortcut_behavior_dev = DEVICE_DT_NAME(DT_INST_PHANDLE(n, shortcut_binding)),             \
        .tap_trigger_key_positions = behavior_naginata_space_trigger_positions_##n,                \
        .tap_trigger_key_positions_len = DT_INST_PROP_LEN(n, tap_trigger_key_positions),           \
    };                                                                                             \
    BEHAVIOR_DT_INST_DEFINE(n, behavior_naginata_space_init, NULL, NULL,                          \
                            &behavior_naginata_space_config_##n, POST_KERNEL,                     \
                            CONFIG_KERNEL_INIT_PRIORITY_DEFAULT,                                  \
                            &behavior_naginata_space_driver_api);

DT_INST_FOREACH_STATUS_OKAY(NAGINATA_SPACE_INST)
