#include "reb_state_machine.h"
#include "reb_config.h"
#include "reb_logger.h"
#include "reb_security.h"
#include <string.h>

static void reb_apply_derating(const RebInputs *inputs,
                               RebOutputs *outputs)
{
    if (inputs->vehicle_speed_kmh > REB_SAFE_MOVING_SPEED_KMH)
    {
        if (outputs->derate_percent < REB_DERATE_MAX_PERCENT)
        {
            outputs->derate_percent += REB_DERATE_STEP_PERCENT;
        }
    }
    else
    {
        outputs->derate_percent = REB_DERATE_MIN_PERCENT;
    }
}

void reb_state_machine_init(RebContext *context)
{
    memset(context, 0, sizeof(RebContext));
    context->current_state = REB_STATE_IDLE;
    context->last_valid_nonce = 0U;
    reb_logger_info("State machine initialized to IDLE");
}

void reb_state_machine_step(RebContext *context,
                            const RebInputs *inputs,
                            RebOutputs *outputs)
{
    memset(outputs, 0, sizeof(RebOutputs));

    switch (context->current_state)
    {
        case REB_STATE_IDLE:
        {
            if (inputs->intrusion_detected == true)
            {
                context->current_state = REB_STATE_THEFT_CONFIRMED;
                context->theft_confirmed_timestamp_ms = inputs->timestamp_ms;
                reb_logger_info("Transition: IDLE -> THEFT_CONFIRMED (automatic)");
            }
            else if ((inputs->remote_command == REB_REMOTE_BLOCK) &&
                     reb_security_validate_remote_command(inputs, context))
            {
                context->current_state = REB_STATE_THEFT_CONFIRMED;
                context->theft_confirmed_timestamp_ms = inputs->timestamp_ms;
                reb_logger_info("Transition: IDLE -> THEFT_CONFIRMED (remote)");

                if (inputs->vehicle_speed_kmh <= 0.0f)
                {
                    context->current_state = REB_STATE_BLOCKING;
                    reb_logger_info("Transition: THEFT_CONFIRMED -> BLOCKING (stationary remote block)");
                }
            }
            break;
        }

        case REB_STATE_THEFT_CONFIRMED:
        {
            outputs->visual_alert = true;
            outputs->acoustic_alert = true;

            if (inputs->remote_command == REB_REMOTE_CANCEL)
            {
                context->current_state = REB_STATE_IDLE;
                reb_logger_info("Theft event cancelled");
                break;
            }

            if (inputs->vehicle_speed_kmh <= 0.0f)
            {
                context->current_state = REB_STATE_BLOCKING;
                reb_logger_info("Transition: THEFT_CONFIRMED -> BLOCKING (stationary)");
                break;
            }

            if ((inputs->timestamp_ms -
                 context->theft_confirmed_timestamp_ms) >=
                REB_THEFT_CONFIRM_WINDOW_MS)
            {
                context->current_state = REB_STATE_BLOCKING;
                reb_logger_info("Transition: THEFT_CONFIRMED -> BLOCKING");
            }
            break;
        }

        case REB_STATE_BLOCKING:
        {
            outputs->visual_alert = true;
            outputs->acoustic_alert = true;

            /* Safe stop: nunca bloquear partida ou ignição em movimento */
            if (inputs->vehicle_speed_kmh > REB_MAX_ALLOWED_SPEED_FOR_LOCK)
            {
                reb_apply_derating(inputs, outputs);
                context->vehicle_stopped_timestamp_ms = 0U;
            }
            else
            {
                /*
                outputs->derate_percent = REB_DERATE_MAX_PERCENT;

                if (context->vehicle_stopped_timestamp_ms == 0U)
                {
                    context->vehicle_stopped_timestamp_ms =
                        inputs->timestamp_ms;
                }

                if ((inputs->timestamp_ms -
                     context->vehicle_stopped_timestamp_ms) >=
                    REB_STOP_HOLD_TIME_MS)
                {
                    context->current_state = REB_STATE_BLOCKED;
                    reb_logger_info("Transition: BLOCKING -> BLOCKED");
                }
                */
                context->current_state = REB_STATE_BLOCKED;
                reb_logger_info("Transition: BLOCKING -> BLOCKED");
            }
            break;
        }

        case REB_STATE_BLOCKED:
        {
            outputs->visual_alert = true;
            outputs->starter_lock = true;
            outputs->derate_percent = REB_DERATE_MAX_PERCENT;

            if ((inputs->timestamp_ms -
                 context->last_blocked_retx_timestamp_ms) >=
                REB_BLOCKED_RETRANSMIT_MS)
            {
                outputs->send_status_to_tcu = true;
                context->last_blocked_retx_timestamp_ms =
                    inputs->timestamp_ms;
            }

            if ((inputs->remote_command == REB_REMOTE_UNLOCK) &&
                reb_security_unlock_allowed(inputs, context))
            {
                reb_logger_info("Authenticated unlock accepted");
                reb_state_machine_init(context);
            }
            else if (inputs->remote_command == REB_REMOTE_UNLOCK)
            {
                context->invalid_unlock_attempts++;
                reb_logger_warn("Rejected unlock command");
            }
            break;
        }

        default:
        {
            reb_logger_error("Invalid state detected");
            reb_state_machine_init(context);
            break;
        }
    }
}