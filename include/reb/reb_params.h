/**
 * @file    reb_params.h
 * @brief   REB — Parâmetros Compilados (migração de reb_params_v9.m)
 * @version v1.0
 * @date    2026-04
 *
 * Todos os valores alinhados ao reb_params_v9.m e ao REB-SRS-001 v0.2.
 * MISRA C: todos os literais com sufixo de tipo explícito.
 */

#ifndef REB_PARAMS_H
#define REB_PARAMS_H

/* =========================================================================
 * 1. SOLVER / TEMPO — NFR-DET-001
 * ========================================================================= */
#define REB_TS_MS               10U      /**< Passo de solver em ms (10ms = 100Hz) */
#define REB_TS_S                0.01f    /**< Passo de solver em segundos          */
#define REB_T_SIM_S             300U     /**< Duração padrão de cenário [s]        */

/* =========================================================================
 * 2. LIMITES DE VELOCIDADE E SEGURANÇA — FR-010, FR-011, NFR-SAF-001/002
 * ========================================================================= */
#define V_STOP_KMH              0.5f     /**< Limiar "veículo parado" [km/h]       */
#define V_SAFE_KMH              20.0f    /**< Vel. max para bloqueio abrupto [km/h]*/
#define V_STOP_MS               0.139f   /**< V_STOP convertido [m/s]              */
#define V_SAFE_MS               5.556f   /**< V_SAFE convertido [m/s]              */

/* =========================================================================
 * 3. TIMERS DE ESTADO — FR-008, FR-010, FR-011, FR-012
 * ========================================================================= */
#define T_PARKED_S              120U     /**< Dwell para BLOCKED [s]               */
#define T_PREALERT_S            60U      /**< Janela cancelamento SOURCE_AUTO [s]  */
#define T_REVERSAL_S            90U      /**< Janela de aviso pré-bloqueio [s]     */
#define T_MIN_AFTER_CONFIRMED_S 60U      /**< Janela pós-THEFT não-AUTO [s]        */

/** Conversão de timers para ciclos (Ts=10ms) */
#define T_PARKED_CYCLES         12000U   /**< 120 s / 0.01 s                       */
#define T_PREALERT_CYCLES       6000U    /**< 60 s  / 0.01 s                       */
#define T_REVERSAL_CYCLES       9000U    /**< 90 s  / 0.01 s                       */
#define T_MIN_AFTER_CYCLES      6000U    /**< 60 s  / 0.01 s                       */

/* =========================================================================
 * 4. DERATING DE COMBUSTÍVEL — FR-009, NFR-SAF-001
 * ========================================================================= */
#define FUEL_FLOOR_PCT          10U      /**< Piso mínimo durante derating [%]     */
#define DERATE_RATE_PCT_S       0.75f    /**< Taxa de queda do derate [%/s]        */
#define DERATE_PCT_INIT         100U     /**< Condição inicial do derate [%]       */

/* =========================================================================
 * 5. AUTENTICAÇÃO DO PAINEL — FR-004, NFR-SEC-001
 * ========================================================================= */
#define PANEL_PASSWORD_HASH     0xA3F2B891UL /**< Hash da senha do painel (uint32) */
#define MAX_AUTH_ATTEMPTS       3U       /**< Tentativas antes do lockout          */
#define LOCKOUT_DURATION_S      300U     /**< Duração do lockout [s]               */
#define LOCKOUT_CYCLES          30000U   /**< 300 s / 0.01 s                       */

/* =========================================================================
 * 6. REDE E COMUNICAÇÃO — FR-005, FR-006
 * ========================================================================= */
#define ACK_TIMEOUT_S           5U       /**< Timeout para ACK do TCU [s]          */
#define MAX_RETRIES             3U       /**< Retentativas SMS fallback             */
#define ACK_TIMEOUT_CYCLES      500U     /**< 5 s / 0.01 s                         */
#define STATUS_PERIOD_S         0.1f     /**< Período de envio de status [s]       */
#define STATUS_PERIOD_CYCLES    10U      /**< 0.1 s / 0.01 s                       */

/* =========================================================================
 * 7. SEGURANÇA / ANTI-REPLAY — NFR-SEC-001
 * ========================================================================= */
#define NONCE_WINDOW_S          30U      /**< Janela de validade do nonce [s]      */
#define NONCE_WINDOW_MS         30000U   /**< Janela de validade do nonce [ms]     */
#define SIG_VERIFY_LATENCY_S    0.1f     /**< Latência de verificação de sig [s]   */

/* =========================================================================
 * 8. SENSOR FUSION — FR-007
 * ========================================================================= */
#define SF_W_GLASS              0.6f     /**< Peso do sensor BCM / vidro           */
#define SF_W_ACCEL              0.4f     /**< Peso do acelerômetro                 */
#define SF_THRESH               0.7f     /**< Threshold de detecção                */
#define SF_THRESH_HYST_LOW      0.4f     /**< Threshold de histerese (desativação) */
#define SF_DEBOUNCE_S           2.0f     /**< Tempo de debounce [s]                */
#define SF_DEBOUNCE_CYCLES      200U     /**< 2 s / 0.01 s                         */
#define ACCEL_MAX               10.0f    /**< Aceleração máxima normalizada        */

/* =========================================================================
 * 9. PARTIDA / STARTER — FR-011, NFR-SAF-002
 * ========================================================================= */
#define RETRANSMIT_BLOCK_TIMEOUT_S      5U   /**< Timeout de retransmissão [s]    */
#define RETRANSMIT_BLOCK_TIMEOUT_CYCLES 500U /**< 5 s / 0.01 s                    */

/* =========================================================================
 * 10. EVENT LOG — NFR-INFO-001
 * ========================================================================= */
#define EVENT_LOG_MAX_ENTRIES   256U     /**< Máximo de entradas no log circular   */

/* =========================================================================
 * 11. MONTE CARLO — NFR-VV-001
 * ========================================================================= */
#define MC_RUNS                 500U     /**< Número de rodadas Monte Carlo        */

/* =========================================================================
 * 12. CRITÉRIOS DE ACEITAÇÃO MIL — NFR-VV-001
 * ========================================================================= */
#define ACCEPT_FPR_MAX          0.01f    /**< Taxa máxima de falso positivo        */
#define ACCEPT_LATENCY_MAX_S    5.0f     /**< Latência máxima de bloqueio [s]      */
#define ACCEPT_BLOCK_SUCCESS    0.99f    /**< Taxa de sucesso de bloqueio          */
#define ACCEPT_SPOOF_DETECT     0.95f    /**< Taxa de detecção de spoofing         */

#endif /* REB_PARAMS_H */
