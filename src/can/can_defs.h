/**
 * @file    can_defs.h
 * @brief   REB — Definições CAN (IDs, DLCs, sinais)
 * @version v1.0
 * @date    2026-04
 *
 * Migrado de REB_CAN_DATABASE_v5.dbc e alinhado ao SRS §3.1.
 * Arquivo compartilhado entre a dupla Backend (REB core) e a dupla CAN.
 *
 * IMPORTANTE: DEC-001 — GPS/geofencing removido do escopo.
 * 0x402 (REB_GPS_REQUEST) NÃO está neste arquivo.
 * 0x401 (REB_to_BCM) está incluído para FR-013 (alertas).
 */

#ifndef CAN_DEFS_H
#define CAN_DEFS_H

#include <stdint.h>

/* =========================================================================
 * CAN IDs — Fonte: REB_CAN_DATABASE_v5.dbc
 * ========================================================================= */

/* --- Mensagens recebidas pelo REB --- */
#define CAN_ID_TCU_STATUS           0x100U /**< TCU → REB, cíclico ~50ms     */
#define CAN_ID_TCU_AUTH             0x103U /**< TCU → REB, event-driven       */
#define CAN_ID_REB_CMD              0x200U /**< TCU → REB, event-driven       */
#define CAN_ID_TCU_ACK              0x202U /**< TCU → REB, event-driven       */
#define CAN_ID_ECU_POWERTRAIN       0x105U /**< ECU_Powertrain → REB, 100ms   */
#define CAN_ID_ECU_SENSOR_BCM       0x110U /**< BCM → REB, 20ms               */
#define CAN_ID_ECU_PANEL            0x120U /**< ECU_Panel → REB, 50ms         */
#define CAN_ID_ECU_PANEL_AUTH       0x121U /**< ECU_Panel → REB, on-event     */

/* --- Mensagens transmitidas pelo REB --- */
#define CAN_ID_REB_STATUS           0x201U /**< REB → TCU, event-driven       */
#define CAN_ID_REB_DERATE_CMD       0x400U /**< REB → ECU_Powertrain, 10ms    */
#define CAN_ID_REB_TO_BCM           0x401U /**< REB → BCM, on-event (FR-013)  */

/* =========================================================================
 * DLCs (Data Length Codes) — bytes por frame
 * ========================================================================= */
#define CAN_DLC_TCU_STATUS          1U
#define CAN_DLC_TCU_AUTH            1U
#define CAN_DLC_REB_CMD             8U
#define CAN_DLC_TCU_ACK             1U
#define CAN_DLC_ECU_POWERTRAIN      5U
#define CAN_DLC_ECU_SENSOR_BCM      5U  /* 4 bytes sinais + 1 reservado */
#define CAN_DLC_ECU_PANEL           1U
#define CAN_DLC_ECU_PANEL_AUTH      4U
#define CAN_DLC_REB_STATUS          1U
#define CAN_DLC_REB_DERATE_CMD      2U
#define CAN_DLC_REB_TO_BCM          1U

/* =========================================================================
 * Valores enumerados — alinhados ao VAL_ do DBC
 * ========================================================================= */

/* 0x103 TCU_AUTH — auth_block_automatic */
#define TCU_AUTH_AUTO_PENDING       0U
#define TCU_AUTH_AUTO_CONFIRM_YES   1U
#define TCU_AUTH_AUTO_CONFIRM_NO    2U

/* 0x200 REB_CMD — cmd_sig_ok */
#define REB_CMD_SIG_INVALID         0U
#define REB_CMD_SIG_OK              1U

/* 0x261 ECU_POWERTRAIN — ignition_state_out */
#define IGN_OFF                     0U
#define IGN_ACC                     1U
#define IGN_ON                      2U
#define IGN_START                   3U

/* 0x202 TCU_ACK — tcu_ack */
#define TCU_NO_ACK                  0U
#define TCU_ACK                     1U

/* 0x400 REB_DERATE_CMD — starterOk */
#define STARTER_BLOCK_START         0U
#define STARTER_ALLOW_START         1U

/* 0x401 REB_to_BCM — alert_visual / alert_sonic */
#define ALERT_OFF                   0U
#define ALERT_ON                    1U
#define ALERT_MUTE                  0U
#define ALERT_SONIC_ON              1U

/* 0x513 REB_STATUS — sinais */
#define REB_STATUS_NO_THEFT         0U
#define REB_STATUS_THEFT_DETECTED   1U
#define REB_STATUS_NOT_BLOCKED      0U
#define REB_STATUS_VEHICLE_BLOCKED  1U
#define REB_STATUS_NO_GPS_REQ       0U
#define REB_STATUS_SEND_GPS         1U

/* =========================================================================
 * Estruturas de frame CAN — para uso pela dupla CAN no pack/unpack
 * Estas structs definem a interface de dados entre as duplas.
 * ========================================================================= */

/** 0x100 TCU_STATUS (RX) */
typedef struct {
    uint8_t ip_rx_ok  : 1;  /**< bit 0 */
    uint8_t sms_rx_ok : 1;  /**< bit 1 */
    uint8_t reserved  : 6;
} can_tcu_status_t;

/** 0x103 TCU_AUTH (RX) */
typedef struct {
    uint8_t auth_blocked_remote  : 1; /**< bit 0 */
    uint8_t auth_block_automatic : 2; /**< bits 1-2 */
    uint8_t remote_unblock_remote: 1; /**< bit 3 */
    uint8_t reserved             : 4;
} can_tcu_auth_t;

/** 0x200 REB_CMD (RX) — 8 bytes */
typedef struct {
    uint16_t cmd_nonce;       /**< bytes 0-1: nonce 16-bit              */
    uint32_t cmd_timestamp_ms;/**< bytes 2-5: timestamp em ms           */
    uint8_t  cmd_sig_ok  : 1; /**< byte 6, bit 0: assinatura            */
    uint8_t  reserved    : 7;
    uint8_t  _pad;            /**< byte 7                               */
} can_reb_cmd_t;

/** 0x105 ECU_POWERTRAIN (RX) — 5 bytes */
typedef struct {
    uint8_t  ignition_state  : 2; /**< byte 0 bits 0-1                 */
    uint8_t  reserved        : 6;
    uint16_t engine_rpm_raw;      /**< bytes 1-2: rpm * 4 (factor 0.25)*/
    uint16_t vehicle_speed_raw;   /**< bytes 3-4: kmh * 100 (0.01)     */
} can_ecu_powertrain_t;

/** 0x110 ECU_SENSOR_BCM (RX) — 5 bytes */
typedef struct {
    uint16_t accel_peak_raw;       /**< bytes 0-1: accel * 100         */
    uint16_t glass_break_flag_raw; /**< bytes 2-3: glass * 100         */
    uint8_t  _pad;
} can_ecu_bcm_t;

/** 0x120 ECU_PANEL (RX) — 1 byte */
typedef struct {
    uint8_t cancel_request  : 1; /**< bit 0 */
    uint8_t auth_manual_out : 1; /**< bit 1 */
    uint8_t reserved        : 6;
} can_ecu_panel_t;

/** 0x121 ECU_PANEL_AUTH (RX) — 4 bytes */
typedef struct {
    uint32_t password_attempt; /**< Hash de senha (uint32)             */
} can_ecu_panel_auth_t;

/** 0x202 TCU_ACK (RX) — 1 byte */
typedef struct {
    uint8_t tcu_ack  : 1;
    uint8_t reserved : 7;
} can_tcu_ack_t;

/** 0x201 REB_STATUS (TX) — 1 byte */
typedef struct {
    uint8_t notify_theft   : 1; /**< bit 0 */
    uint8_t notify_blocked : 1; /**< bit 1 */
    uint8_t gps_send       : 2; /**< bits 2-3 (reservado para extensão)*/
    uint8_t reserved       : 4;
} can_reb_status_t;

/** 0x400 REB_DERATE_CMD (TX) — 2 bytes */
typedef struct {
    uint8_t derate_pct; /**< byte 0: % combustível [0..100]            */
    uint8_t starter_ok; /**< byte 1, bit 0: 0=BLOCK, 1=ALLOW          */
} can_reb_derate_cmd_t;

/** 0x401 REB_to_BCM (TX) — 1 byte */
typedef struct {
    uint8_t alert_visual : 1; /**< bit 0: 0=OFF, 1=ON                 */
    uint8_t alert_sonic  : 1; /**< bit 1: 0=MUTE, 1=ALERT             */
    uint8_t reserved     : 6;
} can_reb_to_bcm_t;

#endif /* CAN_DEFS_H */
