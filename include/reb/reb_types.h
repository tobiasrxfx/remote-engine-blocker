/**
 * @file    reb_types.h
 * @brief   REB — Remote Engine Blocker: Tipos e Enumerações Centrais
 * @version v1.0
 * @date    2026-04
 *
 * Alinhado ao REB-SRS-001 v0.2 §1.3, §4, §5
 * Gerado a partir de reb_params_v9.m e REB_CAN_DATABASE_v5.dbc
 *
 * MISRA C: todos os tipos são explicitamente sizados (uint8_t, uint16_t, etc.)
 */

#ifndef REB_TYPES_H
#define REB_TYPES_H

#include <stdint.h>
#include <stdbool.h>

/* =========================================================================
 * Estados do sistema — FR-001, FR-002, FR-003
 * Alinhado com reb_params_v9.m §2
 * ========================================================================= */
typedef enum {
    STATE_IDLE            = 0U, /**< Sistema monitorando, sem evento ativo  */
    STATE_THEFT_CONFIRMED = 1U, /**< Roubo confirmado, aguardando ação      */
    STATE_BLOCKING        = 2U, /**< Bloqueio progressivo em curso          */
    STATE_BLOCKED         = 3U  /**< Bloqueio definitivo — partida inibida  */
} reb_state_t;

/* =========================================================================
 * Tipo de comando — FR-005, alinhado com DBC 0x200 VAL_
 * ========================================================================= */
typedef enum {
    CMD_NOP            = 0U, /**< Sem operação                             */
    CMD_BLOCK          = 1U, /**< Comando de bloqueio remoto               */
    CMD_UNBLOCK        = 2U, /**< Comando de desbloqueio remoto            */
    CMD_STATUS_REQUEST = 3U  /**< Solicitação de status                    */
} cmd_type_t;

/* =========================================================================
 * Fonte de ativação — FR-004, FR-005, FR-006, FR-007
 * ========================================================================= */
typedef enum {
    SOURCE_PANEL  = 0U, /**< Painel físico local (FR-004)                 */
    SOURCE_REMOTE = 1U, /**< Remoto 4G/5G ou SMS (FR-005, FR-006)         */
    SOURCE_AUTO   = 2U  /**< Automático por sensores (FR-007)             */
} activation_source_t;

/* =========================================================================
 * Código de falha de autenticação — NFR-SEC-001
 * Alinhado com security_check do Stateflow
 * ========================================================================= */
typedef enum {
    AUTH_OK             = 0U, /**< Autenticação bem-sucedida               */
    AUTH_SIG_INVALID    = 1U, /**< Assinatura inválida (cmd_sig_ok==0)     */
    AUTH_NONCE_REPLAY   = 2U, /**< Nonce repetido                          */
    AUTH_TS_EXPIRED     = 3U  /**< Timestamp fora da janela de 30 s        */
} auth_fail_t;

/* =========================================================================
 * Status de validade de sinal — NFR-SAF-002
 * ========================================================================= */
typedef enum {
    SIG_VALID   = 0U, /**< Sinal válido e atualizado                      */
    SIG_MISSING = 1U, /**< Sinal ausente (timeout CAN)                    */
    SIG_TIMEOUT = 2U  /**< Sinal com timestamp expirado                   */
} signal_status_t;

/* =========================================================================
 * Modo da janela de reversão — FR-008 (60s) e FR-012 (90s)
 * ========================================================================= */
typedef enum {
    RW_MODE_60 = 0U, /**< Janela de cancelamento SOURCE_AUTO (FR-008)    */
    RW_MODE_90 = 1U  /**< Janela de aviso pré-bloqueio (FR-012)          */
} rw_mode_t;

/* =========================================================================
 * Resultado da janela de reversão
 * ========================================================================= */
typedef enum {
    RW_RUNNING = 0U, /**< Timer em curso, aguardando                      */
    RW_ABORT   = 1U, /**< Usuário cancelou com senha válida               */
    RW_EXPIRE  = 2U  /**< Timer expirou sem cancelamento                  */
} rw_result_t;

/* =========================================================================
 * Resultado da fusão de sensores — FR-007
 * ========================================================================= */
typedef struct {
    float    theft_score;       /**< Score ponderado [0.0 .. 1.0]         */
    bool     theft_detected;    /**< TRUE quando score >= sensor_thresh    */
    uint16_t debounce_cnt;      /**< Contador de debounce (ciclos)         */
} sf_output_t;

/* =========================================================================
 * Saídas do atuador — FR-009, FR-010, FR-011
 * ========================================================================= */
typedef struct {
    uint8_t  derate_pct;            /**< % combustível [10..100]           */
    bool     starter_ok;            /**< TRUE=permitir, FALSE=inibir start */
    bool     fuel_derating_active;  /**< Derating está ativo               */
    bool     starter_inhibit_active;/**< Inibição de partida ativa         */
} actuator_output_t;

/* =========================================================================
 * Saídas de alerta — FR-013
 * ========================================================================= */
typedef struct {
    bool    horn_active;    /**< Buzina intermitente 1 Hz                  */
    bool    hazard_active;  /**< Pisca-alerta ligado                        */
    bool    hmi_alert;      /**< Alerta crítico no painel HMI               */
} alert_output_t;

/* =========================================================================
 * Saídas de status CAN — FR-005 (0x201 REB_STATUS)
 * ========================================================================= */
typedef struct {
    bool notify_theft;   /**< SG_ notify_theft  : 0|1                      */
    bool notify_blocked; /**< SG_ notify_blocked: 1|1                      */
    bool gps_send;       /**< SG_ gps_send      : 2|1                      */
} reb_status_can_t;

/* =========================================================================
 * Registro de evento — NFR-INFO-001
 * ========================================================================= */
typedef struct {
    uint32_t timestamp_ms;  /**< Tempo de simulação em ms                  */
    uint8_t  event_code;    /**< Código do evento (ver event_log.h)         */
    uint8_t  state_from;    /**< Estado de origem                           */
    uint8_t  state_to;      /**< Estado de destino                          */
    uint8_t  source;        /**< Fonte (activation_source_t)               */
    uint8_t  auth_fail;     /**< Código de falha de auth (auth_fail_t)     */
    uint8_t  _pad[3];       /**< Padding MISRA — alinhamento de struct      */
} event_record_t;

/* =========================================================================
 * Dados NVM persistidos — NFR-REL-001
 * ========================================================================= */
typedef struct {
    reb_state_t last_state;          /**< Último estado antes do reset      */
    uint32_t    last_nonce;          /**< Último nonce aceito (anti-replay) */
    uint8_t     panel_wrong_cnt;     /**< Tentativas erradas de painel      */
    bool        lockout_active;      /**< Painel em lockout                 */
    uint32_t    lockout_remaining_s; /**< Segundos restantes de lockout     */
    uint8_t     _pad[2];             /**< Padding                           */
    uint32_t    crc32;               /**< CRC32 de validação                */
} nvm_data_t;

/* =========================================================================
 * Estado do painel de autenticação — FR-004
 * ========================================================================= */
typedef struct {
    uint8_t  wrong_cnt;       /**< Contador de tentativas erradas          */
    bool     lockout_active;  /**< Painel bloqueado                        */
    uint32_t lockout_timer;   /**< Ciclos restantes de lockout             */
    bool     prev_auth_pulse; /**< Nível anterior (detecção de borda)      */
    bool     auth_ok;         /**< Autenticação bem-sucedida               */
} panel_auth_ctx_t;

/* =========================================================================
 * Contexto da fusão de sensores — FR-007
 * ========================================================================= */
typedef struct {
    float    last_score;       /**< Score do ciclo anterior                 */
    uint16_t debounce_cnt;     /**< Ciclos consecutivos acima do threshold  */
    bool     active;           /**< Detecção de roubo ativa                 */
    uint8_t  _pad;
} sf_ctx_t;

/* =========================================================================
 * Contexto do security manager — NFR-SEC-001
 * ========================================================================= */
typedef struct {
    uint32_t last_nonce;      /**< Último nonce aceito                     */
} sec_ctx_t;

/* =========================================================================
 * Contexto da janela de reversão — FR-008, FR-012
 * ========================================================================= */
typedef struct {
    rw_mode_t mode;                    /**< Modo ativo (60s ou 90s)        */
    uint32_t  timer_cycles;            /**< Ciclos decorridos               */
    uint32_t  limit_cycles;            /**< Limite de ciclos para expirar   */
    bool      active;                  /**< Janela em andamento             */
    bool      pre_block_alert_active;  /**< Alerta de pré-bloqueio ativo    */
    bool      blocking_actuation_issued; /**< Atuação de bloqueio emitida   */
    uint8_t   _pad;
} rw_ctx_t;

/* =========================================================================
 * Contexto principal da FSM — FR-001..003
 * ========================================================================= */
typedef struct {
    reb_state_t        state;              /**< Estado atual                */
    reb_state_t        prev_state;         /**< Estado anterior             */
    activation_source_t source;            /**< Fonte que ativou o bloqueio */
    uint32_t           parked_timer;       /**< Ciclos parado (FR-010/011)  */
    uint32_t           min_after_timer;    /**< Janela pós-THEFT (FR-012)   */
    bool               unblock_requested;  /**< Pedido de desbloqueio       */
    signal_status_t    speed_signal_status;/**< Validade do sinal de vel.   */
    bool               nvm_state_loaded;   /**< Estado restaurado do NVM    */
    uint32_t           sim_time_ms;        /**< Tempo de simulação em ms    */
    uint8_t            _pad[3];
} fsm_ctx_t;

/* =========================================================================
 * Entradas da FSM (vindas da camada CAN / IHM da outra dupla)
 * ========================================================================= */
typedef struct {
    /* --- Powertrain (0x105) --- */
    float           vehicle_speed_kmh;  /**< Velocidade km/h              */
    uint16_t        engine_rpm;         /**< RPM do motor                  */
    uint8_t         ignition_state;     /**< 0=OFF,1=ACC,2=ON,3=START      */
    signal_status_t speed_sig_status;   /**< Validade NFR-SAF-002          */

    /* --- Sensor BCM (0x110) --- */
    float           accel_peak;         /**< Pico de aceleração (decimal)  */
    float           glass_break_flag;   /**< Quebra de vidro   (decimal)   */

    /* --- TCU Status (0x100) --- */
    bool            ip_rx_ok;           /**< Canal 4G disponível           */
    bool            sms_rx_ok;          /**< Canal SMS disponível          */

    /* --- TCU Auth (0x103) --- */
    bool            auth_blocked_remote;     /**< Bloqueio remoto via TCU  */
    uint8_t         auth_block_automatic;    /**< 0=PENDING,1=YES,2=NO     */
    bool            remote_unblock_remote;   /**< Desbloqueio remoto       */

    /* --- REB CMD (0x200) --- */
    uint16_t        cmd_nonce;          /**< Nonce do comando              */
    uint32_t        cmd_timestamp_ms;   /**< Timestamp em ms (anti-replay) */
    bool            cmd_sig_ok;         /**< Assinatura válida             */

    /* --- TCU ACK (0x202) --- */
    bool            tcu_ack;            /**< ACK do TCU                    */

    /* --- Panel (0x120) --- */
    bool            cancel_request;     /**< Cancelamento via painel       */
    bool            auth_manual_out;    /**< Ativação manual via painel    */

    /* --- Panel Auth (0x121) --- */
    uint32_t        password_attempt;   /**< Hash de senha tentada         */

    /* --- Tempo --- */
    uint32_t        sim_time_ms;        /**< Tempo de simulação em ms      */
} reb_inputs_t;

/* =========================================================================
 * Saídas da FSM (enviadas para a camada CAN / IHM da outra dupla)
 * ========================================================================= */
typedef struct {
    /* --- Derate CMD (0x400) --- */
    uint8_t  derate_pct;            /**< % combustível [0..100]            */
    bool     starter_ok;            /**< TRUE=permitir start               */

    /* --- BCM Alerts (0x401) --- */
    bool     alert_visual;          /**< Pisca-alerta                      */
    bool     alert_sonic;           /**< Buzina                            */

    /* --- REB STATUS (0x201) --- */
    bool     notify_theft;          /**< Notificação de roubo              */
    bool     notify_blocked;        /**< Notificação de bloqueado          */
    bool     gps_send;              /**< Solicitar GPS ao TCU              */

    /* --- Dados de diagnóstico para IHM --- */
    reb_state_t current_state;      /**< Estado atual da FSM               */
    float       sensor_score;       /**< Score de sensor fusion [0..1]     */
    bool        pre_block_alert;    /**< Alerta de pré-bloqueio ativo      */
    uint32_t    reversal_timer_s;   /**< Segundos restantes da janela      */
    bool        starter_inhibit_active; /**< Inibição de partida ativa     */
    bool        fuel_derating_active;   /**< Derating ativo                */
    bool        nvm_state_loaded;   /**< Estado restaurado de NVM          */
} reb_outputs_t;

#endif /* REB_TYPES_H */
