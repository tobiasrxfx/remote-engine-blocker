#include "reb_security.h"
#include "reb_config.h"
#include <stdbool.h>
#include <stdio.h>

/* Verifica se o nonce já foi utilizado */
static bool reb_security_is_nonce_replayed(const RebContext *context,
                                            uint32_t nonce)
{
    for (uint8_t i = 0; i < REB_NONCE_HISTORY_SIZE; i++)
    {
        if (context->nonce_history[i] == nonce)
        {
            return true;
        }
    }
    return false;
}

/* Registra o nonce no histórico */
static void reb_security_store_nonce(RebContext *context,
                                      uint32_t nonce)
{
    context->nonce_history[context->nonce_history_index] = nonce;
    context->nonce_history_index =
        (context->nonce_history_index + 1U) % REB_NONCE_HISTORY_SIZE;
}

/* Validação com janela deslizante */
static bool reb_security_is_nonce_in_window(const RebContext *context,
                                            uint32_t nonce)
{
    uint32_t last = context->last_valid_nonce;
    
    if (nonce <= last)
    {
        return false;
    }

    if ((nonce - last) > REB_NONCE_WINDOW_SIZE)
    {
        return false;
    }
    
    return true;
}

bool reb_security_validate_remote_command(const RebInputs *inputs,
                                           RebContext *context)
{
    printf("Nonce recebido: %u | Último nonce válido: %u\n", inputs->nonce, context->last_valid_nonce);
    if ((inputs == NULL) || (context == NULL))
    {
        return false;
    }

    if (inputs->remote_command == REB_REMOTE_NONE)
    {
        return false;
    }

    if (inputs->timestamp_ms == 0U)
    {
        return false;
    }

    /* Verifica se o nonce está dentro da janela válida */
    if (!reb_security_is_nonce_in_window(context, inputs->nonce))
    {
        return false;
    }

    /* Proteção contra replay */
    if (reb_security_is_nonce_replayed(context, inputs->nonce))
    {
        return false;
    }

    /* Atualiza o último nonce válido */
    context->last_valid_nonce = inputs->nonce;

    /* Armazena no histórico */
    reb_security_store_nonce(context, inputs->nonce);

    return true;
}

bool reb_security_unlock_allowed(const RebInputs *inputs,
                                 RebContext *context)
{
    if (!reb_security_validate_remote_command(inputs, context))
    {
        context->invalid_unlock_attempts++;
        return false;
    }

    if (!inputs->tcu_ack_received)
    {
        return false;
    }

    return true;
}