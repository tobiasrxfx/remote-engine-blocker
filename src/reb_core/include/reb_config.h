#ifndef REB_CONFIG_H
#define REB_CONFIG_H

#define REB_THEFT_CONFIRM_WINDOW_MS      (60000U)
#define REB_STOP_HOLD_TIME_MS            (120000U)
#define REB_BLOCKED_RETRANSMIT_MS        (5000U)

#define REB_MAX_ALLOWED_SPEED_FOR_LOCK   (0.5f)
#define REB_SAFE_MOVING_SPEED_KMH        (5.0f)

#define REB_DERATE_STEP_PERCENT          (10U)
#define REB_DERATE_MAX_PERCENT           (90U)
#define REB_DERATE_MIN_PERCENT           (20U)

#define REB_MAX_INVALID_ATTEMPTS         (3U)

#define REB_MAX_SPEED_FOR_BLOCK_KMH      (5.0f)
#define REB_MIN_BATTERY_VOLTAGE          (9.0f)
#define REB_ENGINE_RPM_LIMIT             (1000U)

#define REB_NONCE_WINDOW_SIZE            (32U)
#define REB_NONCE_HISTORY_SIZE    (10U)
#define REB_NONCE_MAX_VALUE       (0xFFFFFFFFU)

#endif