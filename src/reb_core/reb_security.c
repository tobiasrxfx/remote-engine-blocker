#include "reb_security.h"
#include "reb_config.h"
#include <stdbool.h>
#include <stdio.h>

/* Verifica se o nonce já foi utilizado */
static bool reb_security_is_nonce_replayed(const RebContext *context,
                                            uint32_t nonce)
{
    bool ret_val = false;
    for (uint8_t i = 0; (i < REB_NONCE_HISTORY_SIZE) && (ret_val == false); i++)
    {
        if (context->nonce_history[i] == nonce)
        {
            ret_val = true;
        }
    }

    return ret_val;
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
    bool ret_val = true;
    uint32_t last = context->last_valid_nonce;
    
    if ((nonce <= last) || ((nonce - last) > REB_NONCE_WINDOW_SIZE))
    {
        ret_val = false;
    }
    
    return ret_val;
}

bool reb_security_validate_remote_command(const RebInputs *inputs,
                                           RebContext *context)
{
    bool ret_value = false;
    if ((inputs == NULL) || (context == NULL))
    {
        ret_value = false;
    }
    else 
    {
        (void)printf("Nonce recebido: %u | Último nonce válido: %u\n", inputs->nonce, context->last_valid_nonce);

        if(inputs->remote_command == REB_REMOTE_NONE)
        {
            ret_value = false;
        }
        else if (inputs->timestamp_ms == 0U)
        {
            ret_value = false;
        }
        /* Verifica se o nonce está dentro da janela válida */
        else if (!reb_security_is_nonce_in_window(context, inputs->nonce))
        {
            ret_value = false;
        }
        /* Proteção contra replay */
        else if (reb_security_is_nonce_replayed(context, inputs->nonce))
        {
            ret_value = false;
        }
        else
        {
            /* Atualiza o último nonce válido */
            context->last_valid_nonce = inputs->nonce;

            /* Armazena no histórico */
            reb_security_store_nonce(context, inputs->nonce);
            ret_value = true;
        }
    
    }

    return ret_value;
}

bool reb_security_unlock_allowed(const RebInputs *inputs,
                                 RebContext *context)
{
    bool ret_value = false;
    if (!reb_security_validate_remote_command(inputs, context))
    {
        context->invalid_unlock_attempts++;
    }
    else if (!inputs->tcu_ack_received)
    {
        /* ret_value keep false */
    }
    else
    {
        ret_value = true;
    }

    return ret_value;
}