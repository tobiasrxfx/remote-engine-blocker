#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
REB Requirements Audit v2
- Lê docs/architecture/raw_requirements.md
- Avalia TODOS os requisitos FR/NFR por regras estáticas detalhadas
- Classifica cobertura e corretude
- Calcula score de conformidade por requisito (0-100)
- Gera:
  - artifacts/requirements_audit_v2.json
  - artifacts/requirements_audit_v2.md
"""

from __future__ import annotations
import argparse
import json
import re
from dataclasses import dataclass, asdict, field
from pathlib import Path
from typing import Dict, List, Tuple, Optional

# ------------------------------------------------------------
# Paths
# ------------------------------------------------------------
ROOT = Path(__file__).resolve().parents[1]
REQ_FILE = ROOT / "docs" / "architecture" / "raw_requirements.md"
SRC_DIR = ROOT / "src"
ART_DIR = ROOT / "artifacts"

REQ_ID_RE = re.compile(r"^###\s+((?:FR|NFR)-[A-Z0-9-]+)\b", re.MULTILINE)

# ------------------------------------------------------------
# Models
# ------------------------------------------------------------
@dataclass
class RuleCheck:
    name: str
    weight: int
    ok: bool
    message: str
    evidence: List[str] = field(default_factory=list)

@dataclass
class RequirementResult:
    requirement: str
    covered: str          # implemented | partial | missing
    correctness: str      # pass | fail | unknown
    score: int            # 0..100
    errors: List[str]
    warnings: List[str]
    evidence: List[str]
    checks: List[RuleCheck]

# ------------------------------------------------------------
# Helpers
# ------------------------------------------------------------
def read_text(path: Path) -> str:
    return path.read_text(encoding="utf-8", errors="ignore")

def collect_sources() -> Dict[str, str]:
    files: Dict[str, str] = {}
    for p in SRC_DIR.rglob("*"):
        if p.is_file() and p.suffix in (".c", ".h"):
            rel = str(p.relative_to(ROOT))
            files[rel] = read_text(p)
    files[str(REQ_FILE.relative_to(ROOT))] = read_text(REQ_FILE)
    return files

def grep_files(files: Dict[str, str], patterns: List[str], regex_flags=re.IGNORECASE | re.MULTILINE) -> List[str]:
    out: List[str] = []
    for f, t in files.items():
        if all(re.search(p, t, flags=regex_flags) for p in patterns):
            out.append(f)
    return out

def has_any(files: Dict[str, str], patterns: List[str], regex_flags=re.IGNORECASE | re.MULTILINE) -> bool:
    for _, t in files.items():
        for p in patterns:
            if re.search(p, t, flags=regex_flags):
                return True
    return False

def find_in_file(files: Dict[str, str], path: str, pattern: str, flags=re.IGNORECASE | re.MULTILINE) -> bool:
    t = files.get(path, "")
    return re.search(pattern, t, flags=flags) is not None

def extract_requirements(md: str) -> List[str]:
    return REQ_ID_RE.findall(md)

def summarize(checks: List[RuleCheck]) -> Tuple[str, str, int, List[str], List[str], List[str]]:
    total = sum(c.weight for c in checks) or 1
    got = sum(c.weight for c in checks if c.ok)
    score = int(round((got / total) * 100))

    failed = [c for c in checks if not c.ok]
    passed = [c for c in checks if c.ok]

    errors = [f"{c.name}: {c.message}" for c in failed]
    warnings: List[str] = []

    # covered
    if score >= 80:
        covered = "implemented"
    elif score >= 30:
        covered = "partial"
    else:
        covered = "missing"

    # correctness
    if len(failed) == 0:
        correctness = "pass"
    elif len(passed) == 0:
        correctness = "fail"
    else:
        correctness = "unknown"

    evidence = sorted({ev for c in checks for ev in c.evidence})
    return covered, correctness, score, errors, warnings, evidence

def rule(name: str, weight: int, ok: bool, message: str, evidence: Optional[List[str]] = None) -> RuleCheck:
    return RuleCheck(name=name, weight=weight, ok=ok, message=message, evidence=evidence or [])

# ------------------------------------------------------------
# Requirement rule engine
# ------------------------------------------------------------
def check_fr_001(files: Dict[str, str]) -> RequirementResult:
    sm = "src/reb_core/reb_state_machine.c"
    checks = [
        rule("FSM com estados principais", 20,
             find_in_file(files, sm, r"REB_STATE_IDLE") and
             find_in_file(files, sm, r"REB_STATE_THEFT_CONFIRMED") and
             find_in_file(files, sm, r"REB_STATE_BLOCKING") and
             find_in_file(files, sm, r"REB_STATE_BLOCKED"),
             "Estados IDLE/THEFT_CONFIRMED/BLOCKING/BLOCKED não encontrados todos juntos.", [sm]),
        rule("Transição IDLE->THEFT_CONFIRMED", 20,
             find_in_file(files, sm, r"IDLE") and find_in_file(files, sm, r"THEFT_CONFIRMED"),
             "Transição inicial não evidenciada.", [sm]),
        rule("Transição THEFT_CONFIRMED->BLOCKING", 20,
             find_in_file(files, sm, r"THEFT_CONFIRMED[\s\S]*BLOCKING"),
             "Transição THEFT_CONFIRMED->BLOCKING não evidenciada.", [sm]),
        rule("Transição BLOCKING->BLOCKED", 20,
             find_in_file(files, sm, r"BLOCKING[\s\S]*BLOCKED"),
             "Transição BLOCKING->BLOCKED não evidenciada.", [sm]),
        rule("Registro em log de transições/eventos", 20,
             find_in_file(files, sm, r"reb_logger_"),
             "Logs de transição/evento não encontrados.", [sm, "src/reb_core/reb_logger.c"]),
    ]
    c, k, s, e, w, ev = summarize(checks)
    return RequirementResult("FR-001", c, k, s, e, w, ev, checks)

def check_fr_002(files: Dict[str, str]) -> RequirementResult:
    sm = "src/reb_core/reb_state_machine.c"
    sec = "src/reb_core/reb_security.c"
    checks = [
        rule("Validação de comando remoto antes de bloquear", 30,
             find_in_file(files, sm, r"reb_security_validate_remote_command"),
             "Não foi encontrada validação de comando remoto no fluxo de ativação.", [sm, sec]),
        rule("Rejeição de requisições inválidas", 20,
             find_in_file(files, sec, r"return false"),
             "Não há evidência de rejeição de comandos inválidos.", [sec]),
        rule("Sem bloqueio direto antes de THEFT_CONFIRMED", 30,
             find_in_file(files, sm, r"case\s+REB_STATE_IDLE[\s\S]*THEFT_CONFIRMED") and
             not find_in_file(files, sm, r"case\s+REB_STATE_IDLE[\s\S]*starter_lock\s*=\s*true"),
             "Há risco de bloqueio sem passar por THEFT_CONFIRMED.", [sm]),
        rule("Evento confirmado em log", 20,
             find_in_file(files, sm, r"THEFT_CONFIRMED") and find_in_file(files, sm, r"reb_logger"),
             "Sem log claro de confirmação de furto.", [sm]),
    ]
    c, k, s, e, w, ev = summarize(checks)
    return RequirementResult("FR-002", c, k, s, e, w, ev, checks)

def check_fr_003(files: Dict[str, str]) -> RequirementResult:
    sm = "src/reb_core/reb_state_machine.c"
    tp = "src/reb_core/reb_types.h"
    checks = [
        rule("Usa vehicle_speed na decisão de bloqueio", 30,
             find_in_file(files, sm, r"vehicle_speed_kmh"),
             "vehicle_speed_kmh não usado na lógica.", [sm, tp]),
        rule("Usa ignition status", 20,
             find_in_file(files, sm, r"ignition_on"),
             "ignition_on não usado na máquina de estados.", [sm, tp]),
        rule("Usa engine running status", 20,
             find_in_file(files, sm, r"engine_running"),
             "engine_running não usado na máquina de estados.", [sm, tp]),
        rule("Evita lock de partida em movimento", 30,
             find_in_file(files, sm, r"vehicle_speed_kmh\s*>\s*REB_MAX_ALLOWED_SPEED_FOR_LOCK"),
             "Regra de segurança para movimento não evidenciada.", [sm, "src/reb_core/reb_config.h"]),
    ]
    c, k, s, e, w, ev = summarize(checks)
    return RequirementResult("FR-003", c, k, s, e, w, ev, checks)

def check_fr_004(files: Dict[str, str]) -> RequirementResult:
    ca = "src/reb_core/reb_can_adapter.c"
    checks = [
        rule("Comando local via painel/interface", 40,
             find_in_file(files, ca, r"CAN_MSG_PANEL_"),
             "Mensagens de painel/interface não encontradas.", [ca]),
        rule("Validação de permissão/autorização local", 30,
             find_in_file(files, ca, r"auth") or has_any(files, [r"permission", r"authorized"]),
             "Validação de autorização local fraca/ausente.", [ca]),
        rule("Transição para THEFT_CONFIRMED por ativação local", 30,
             has_any(files, [r"panel", r"THEFT_CONFIRMED"]),
             "Não há evidência clara de fluxo local até THEFT_CONFIRMED.", [ca, "src/reb_core/reb_state_machine.c"]),
    ]
    c, k, s, e, w, ev = summarize(checks)
    return RequirementResult("FR-004", c, k, s, e, w, ev, checks)

def check_fr_005(files: Dict[str, str]) -> RequirementResult:
    ca = "src/reb_core/reb_can_adapter.c"
    sm = "src/reb_core/reb_state_machine.c"
    checks = [
        rule("Recebe comando remoto via TCU/CAN", 35,
             find_in_file(files, ca, r"CAN_MSG_REB_CMD|CAN_MSG_TCU_TO_REB"),
             "Entrada via TCU/CAN não evidenciada.", [ca]),
        rule("ACK/status para TCU", 35,
             find_in_file(files, sm, r"send_status_to_tcu") or find_in_file(files, ca, r"CAN_MSG_REB_STATUS"),
             "ACK/status para TCU não evidenciado.", [sm, ca]),
        rule("Transição para THEFT_CONFIRMED por comando remoto", 30,
             find_in_file(files, sm, r"REB_REMOTE_BLOCK") and find_in_file(files, sm, r"THEFT_CONFIRMED"),
             "Ativação remota para THEFT_CONFIRMED não comprovada.", [sm]),
    ]
    c, k, s, e, w, ev = summarize(checks)
    return RequirementResult("FR-005", c, k, s, e, w, ev, checks)

def check_fr_006(files: Dict[str, str]) -> RequirementResult:
    checks = [
        rule("Canal SMS (fallback) explícito", 40,
             has_any(files, [r"\bSMS\b", r"sms"]),
             "Canal SMS explícito não encontrado.", []),
        rule("Autenticação para SMS", 35,
             has_any(files, [r"sms[\s\S]*auth", r"authenticated.*sms", r"sms.*nonce"]),
             "Autenticação específica para SMS não encontrada.", []),
        rule("Ativação até THEFT_CONFIRMED", 25,
             has_any(files, [r"THEFT_CONFIRMED"]),
             "Fluxo SMS -> THEFT_CONFIRMED não evidenciado.", ["src/reb_core/reb_state_machine.c"]),
    ]
    c, k, s, e, w, ev = summarize(checks)
    return RequirementResult("FR-006", c, k, s, e, w, ev, checks)

def check_fr_007(files: Dict[str, str]) -> RequirementResult:
    ca = "src/reb_core/reb_can_adapter.c"
    checks = [
        rule("Detecção automática por sensor BCM", 40,
             find_in_file(files, ca, r"CAN_MSG_BCM_INTRUSION_STATUS|intrusion_detected"),
             "Sinal BCM de intrusão não encontrado.", [ca]),
        rule("Uso de acelerômetro/tilt", 30,
             has_any(files, [r"accelerometer", r"tilt", r"accel", r"x/y/z", r"xyz"]),
             "Acelerômetro/tilt não evidenciado.", []),
        rule("Histerese/filtro falso-positivo", 30,
             has_any(files, [r"hysteresis", r"debounce", r"filter", r"false positive"]),
             "Histerese/filtro de falso-positivo não encontrado.", []),
    ]
    c, k, s, e, w, ev = summarize(checks)
    return RequirementResult("FR-007", c, k, s, e, w, ev, checks)

def check_fr_008(files: Dict[str, str]) -> RequirementResult:
    cfg = "src/reb_core/reb_config.h"
    sm = "src/reb_core/reb_state_machine.c"
    checks = [
        rule("Janela de reversão pré-bloqueio", 35,
             find_in_file(files, cfg, r"REB_THEFT_CONFIRM_WINDOW_MS"),
             "Janela temporal de reversão não encontrada.", [cfg, sm]),
        rule("Abort/cancel retorna para IDLE", 35,
             find_in_file(files, sm, r"REB_REMOTE_CANCEL") and find_in_file(files, sm, r"current_state\s*=\s*REB_STATE_IDLE"),
             "Fluxo de abort/reversão para IDLE não evidenciado.", [sm]),
        rule("Pré-notificação antes do blocking", 30,
             find_in_file(files, sm, r"visual_alert|acoustic_alert"),
             "Pré-alerta não evidenciado.", [sm]),
    ]
    c, k, s, e, w, ev = summarize(checks)
    return RequirementResult("FR-008", c, k, s, e, w, ev, checks)

def check_fr_009(files: Dict[str, str]) -> RequirementResult:
    sm = "src/reb_core/reb_state_machine.c"
    cfg = "src/reb_core/reb_config.h"
    checks = [
        rule("Derating aplicado em movimento", 30,
             find_in_file(files, sm, r"reb_apply_derating|derate_percent"),
             "Lógica de derating em movimento não encontrada.", [sm]),
        rule("Piso mínimo explícito >=10%", 35,
             has_any(files, [r"10U", r"min_derate", r"max\s*\("]),
             "Piso mínimo formal >=10% não evidenciado com clareza.", [sm, cfg]),
        rule("Condição velocidade > 0.5 km/h", 35,
             find_in_file(files, cfg, r"0\.5f") and find_in_file(files, sm, r"REB_MAX_ALLOWED_SPEED_FOR_LOCK"),
             "Condição de velocidade limiar não evidenciada.", [cfg, sm]),
    ]
    c, k, s, e, w, ev = summarize(checks)
    return RequirementResult("FR-009", c, k, s, e, w, ev, checks)

def check_fr_010(files: Dict[str, str]) -> RequirementResult:
    sm = "src/reb_core/reb_state_machine.c"
    cfg = "src/reb_core/reb_config.h"
    checks = [
        rule("Confirma parada segura por 120s", 40,
             find_in_file(files, cfg, r"REB_STOP_HOLD_TIME_MS\s*\(\s*120000U\s*\)") and
             find_in_file(files, sm, r"vehicle_stopped_timestamp_ms"),
             "Confirmação de parada segura por 120s não comprovada.", [cfg, sm]),
        rule("Após parada segura, transição para estado final de bloqueio", 30,
             find_in_file(files, sm, r"current_state\s*=\s*REB_STATE_BLOCKED"),
             "Transição após parada segura não evidenciada.", [sm]),
        rule("Inibição de partida após parada", 30,
             find_in_file(files, sm, r"starter_lock\s*=\s*true"),
             "Starter inhibit pós-parada não evidenciado.", [sm]),
    ]
    c, k, s, e, w, ev = summarize(checks)
    return RequirementResult("FR-010", c, k, s, e, w, ev, checks)

def check_fr_011(files: Dict[str, str]) -> RequirementResult:
    sm = "src/reb_core/reb_state_machine.c"
    checks = [
        rule("Dwell 120s para BLOCKED", 40,
             find_in_file(files, sm, r"REB_STOP_HOLD_TIME_MS"),
             "Dwell para bloqueio definitivo não evidenciado.", [sm, "src/reb_core/reb_config.h"]),
        rule("Comando starter inhibit em BLOCKED", 30,
             find_in_file(files, sm, r"starter_lock\s*=\s*true"),
             "Starter inhibit em BLOCKED não evidenciado.", [sm]),
        rule("Desabilita fuel derating em BLOCKED", 30,
             not find_in_file(files, sm, r"case\s+REB_STATE_BLOCKED[\s\S]*derate_percent\s*="),
             "Derating ainda ativo em BLOCKED (esperado desativar).", [sm]),
    ]
    c, k, s, e, w, ev = summarize(checks)
    return RequirementResult("FR-011", c, k, s, e, w, ev, checks)

def check_fr_012(files: Dict[str, str]) -> RequirementResult:
    cfg = "src/reb_core/reb_config.h"
    sm = "src/reb_core/reb_state_machine.c"
    checks = [
        rule("Janela de 90s", 45,
             find_in_file(files, cfg, r"REB_THEFT_CONFIRM_WINDOW_MS\s*\(\s*90000U\s*\)"),
             "Janela de reversão não está em 90s.", [cfg]),
        rule("Alerta pré-bloqueio", 25,
             find_in_file(files, sm, r"visual_alert|acoustic_alert"),
             "Pré-alerta não encontrado.", [sm]),
        rule("Permite reversão por senha/comando antes da atuação", 30,
             find_in_file(files, sm, r"REB_REMOTE_CANCEL") or has_any(files, [r"password", r"pin"]),
             "Reversão por senha/comando não evidenciada claramente.", [sm]),
    ]
    c, k, s, e, w, ev = summarize(checks)
    return RequirementResult("FR-012", c, k, s, e, w, ev, checks)

def check_fr_013(files: Dict[str, str]) -> RequirementResult:
    sm = "src/reb_core/reb_state_machine.c"
    checks = [
        rule("Alerta visual", 34,
             find_in_file(files, sm, r"visual_alert\s*=\s*true"),
             "Alerta visual não evidenciado.", [sm]),
        rule("Alerta sonoro", 33,
             find_in_file(files, sm, r"acoustic_alert\s*=\s*true"),
             "Alerta sonoro não evidenciado.", [sm]),
        rule("Comandos específicos (buzina/hazard/HMI texto crítico)", 33,
             has_any(files, [r"horn", r"hazard", r"hmi_display_alert", r"infotainment"]),
             "Sinais específicos de buzina/hazard/HMI não encontrados.", []),
    ]
    c, k, s, e, w, ev = summarize(checks)
    return RequirementResult("FR-013", c, k, s, e, w, ev, checks)

def check_nfr_sec_001(files: Dict[str, str]) -> RequirementResult:
    sec = "src/reb_core/reb_security.c"
    checks = [
        rule("Proteção anti-replay (nonce/counter)", 40,
             find_in_file(files, sec, r"nonce") and find_in_file(files, sec, r"replay|history|window"),
             "Mecanismo anti-replay insuficiente/ausente.", [sec]),
        rule("Validação de integridade criptográfica (HMAC/assinatura)", 35,
             has_any(files, [r"HMAC", r"signature", r"digital signature", r"crypto", r"integrity"]),
             "Validação criptográfica de integridade não evidenciada.", []),
        rule("Rejeição robusta de comandos forjados/repetidos", 25,
             find_in_file(files, sec, r"return false"),
             "Regras de rejeição não estão claras o suficiente.", [sec]),
    ]
    c, k, s, e, w, ev = summarize(checks)
    return RequirementResult("NFR-SEC-001", c, k, s, e, w, ev, checks)

def check_nfr_saf_001(files: Dict[str, str]) -> RequirementResult:
    sm = "src/reb_core/reb_state_machine.c"
    checks = [
        rule("Proíbe starter lock em movimento", 50,
             find_in_file(files, sm, r"vehicle_speed_kmh\s*>\s*REB_MAX_ALLOWED_SPEED_FOR_LOCK"),
             "Não ficou claro bloqueio seguro em movimento.", [sm]),
        rule("Usa bloqueio gradual em movimento", 50,
             find_in_file(files, sm, r"reb_apply_derating"),
             "Bloqueio gradual (derating) em movimento não evidenciado.", [sm]),
    ]
    c, k, s, e, w, ev = summarize(checks)
    return RequirementResult("NFR-SAF-001", c, k, s, e, w, ev, checks)

def check_nfr_saf_002(files: Dict[str, str]) -> RequirementResult:
    checks = [
        rule("Detecta perda/corrupção de sinal crítico", 40,
             has_any(files, [r"signal_status", r"timeout", r"invalid data", r"crc error", r"stale"]),
             "Detecção explícita de perda/corrupção de sinal não encontrada.", []),
        rule("Entra em modo conservador/fail-safe", 35,
             has_any(files, [r"failsafe", r"safe mode", r"conservative mode"]),
             "Modo conservador/fail-safe não evidenciado.", []),
        rule("Inibe novos bloqueios sob falha de sinal", 25,
             has_any(files, [r"inhibit.*block", r"block.*inhibit"]),
             "Inibição de bloqueio sob falha não evidenciada.", []),
    ]
    c, k, s, e, w, ev = summarize(checks)
    return RequirementResult("NFR-SAF-002", c, k, s, e, w, ev, checks)

def check_nfr_rel_001(files: Dict[str, str]) -> RequirementResult:
    core = "src/reb_core/reb_core.c"
    per = "src/reb_core/reb_persistence.c"
    checks = [
        rule("Persiste contexto/estado em NVM", 40,
             find_in_file(files, per, r"reb_persistence_save") and find_in_file(files, per, r"fopen"),
             "Persistência de estado não evidenciada.", [per]),
        rule("Recupera estado após reset/power loss", 40,
             find_in_file(files, core, r"reb_persistence_load"),
             "Recuperação de estado na inicialização não evidenciada.", [core, per]),
        rule("Integridade de dado persistido (CRC/validação)", 20,
             find_in_file(files, per, r"crc|calculate_crc"),
             "Validação de integridade do estado persistido não evidenciada.", [per]),
    ]
    c, k, s, e, w, ev = summarize(checks)
    return RequirementResult("NFR-REL-001", c, k, s, e, w, ev, checks)

def check_nfr_info_001(files: Dict[str, str]) -> RequirementResult:
    lg = "src/reb_core/reb_logger.c"
    sec = "src/reb_core/reb_security.c"
    checks = [
        rule("Gera log de eventos críticos", 40,
             find_in_file(files, lg, r"reb_logger_write|reb_logger_info|reb_logger_warn|reb_logger_error"),
             "Infraestrutura de log não encontrada.", [lg]),
        rule("Persistência de logs/traços", 25,
             find_in_file(files, lg, r"artifacts/logs/reb.log"),
             "Persistência de logs não evidenciada.", [lg]),
        rule("Não expõe dados sensíveis em plaintext", 35,
             not find_in_file(files, sec, r"printf\([^)]*nonce"),
             "Há possível exposição de dado sensível (nonce) em output.", [sec]),
    ]
    c, k, s, e, w, ev = summarize(checks)
    return RequirementResult("NFR-INFO-001", c, k, s, e, w, ev, checks)

def check_nfr_sw_001(files: Dict[str, str]) -> RequirementResult:
    checks = [
        rule("Evidência de verificação MISRA no projeto", 50,
             has_any(files, [r"MISRA", r"cppcheck", r"pc-lint", r"clang-tidy", r"static analysis"]),
             "Não há evidência de gate MISRA/static analysis no código.", []),
        rule("Objetivo de 0 violações (artifact/report)", 50,
             (ROOT / "artifacts" / "misra_report.txt").exists() or (ROOT / "artifacts" / "misra_report.xml").exists(),
             "Relatório MISRA não encontrado em artifacts/.", []),
    ]
    c, k, s, e, w, ev = summarize(checks)
    return RequirementResult("NFR-SW-001", c, k, s, e, w, ev, checks)

def generic_check(req_id: str, files: Dict[str, str]) -> RequirementResult:
    # fallback: procura por tokens
    token = re.escape(req_id).replace(r"\-", r"[-_ ]?")
    ev = grep_files(files, [token])
    checks = [
        rule("Evidência textual do requisito", 100, len(ev) > 0,
             "Sem evidência textual direta do requisito no código.", ev[:10])
    ]
    c, k, s, e, w, ev2 = summarize(checks)
    return RequirementResult(req_id, c, k, s, e, w, ev2, checks)

def build_registry():
    return {
        "FR-001": check_fr_001,
        "FR-002": check_fr_002,
        "FR-003": check_fr_003,
        "FR-004": check_fr_004,
        "FR-005": check_fr_005,
        "FR-006": check_fr_006,
        "FR-007": check_fr_007,
        "FR-008": check_fr_008,
        "FR-009": check_fr_009,
        "FR-010": check_fr_010,
        "FR-011": check_fr_011,
        "FR-012": check_fr_012,
        "FR-013": check_fr_013,
        "NFR-SEC-001": check_nfr_sec_001,
        "NFR-SAF-001": check_nfr_saf_001,
        "NFR-SAF-002": check_nfr_saf_002,
        "NFR-REL-001": check_nfr_rel_001,
        "NFR-INFO-001": check_nfr_info_001,
        "NFR-SW-001": check_nfr_sw_001,
    }

# ------------------------------------------------------------
# Report
# ------------------------------------------------------------
def compute_global_score(results: List[RequirementResult]) -> int:
    if not results:
        return 0
    return int(round(sum(r.score for r in results) / len(results)))

def to_markdown(results: List[RequirementResult], global_score: int) -> str:
    lines: List[str] = []
    lines.append("# REB Requirements Audit v2")
    lines.append("")
    lines.append(f"**Global compliance score:** {global_score}/100")
    lines.append("")
    lines.append("| Requirement | Covered | Correctness | Score | Errors |")
    lines.append("|---|---|---|---:|---|")
    for r in results:
        err = "; ".join(r.errors) if r.errors else "-"
        lines.append(f"| {r.requirement} | {r.covered} | {r.correctness} | {r.score} | {err} |")
    lines.append("")
    lines.append("## Detailed checks")
    for r in results:
        lines.append(f"### {r.requirement}")
        lines.append(f"- Covered: **{r.covered}**")
        lines.append(f"- Correctness: **{r.correctness}**")
        lines.append(f"- Score: **{r.score}/100**")
        if r.evidence:
            lines.append(f"- Evidence: {', '.join(r.evidence)}")
        if r.errors:
            lines.append("- Errors:")
            for e in r.errors:
                lines.append(f"  - {e}")
        if r.warnings:
            lines.append("- Warnings:")
            for w in r.warnings:
                lines.append(f"  - {w}")
        lines.append("- Rule checks:")
        for c in r.checks:
            mark = "✅" if c.ok else "❌"
            ev = f" ({', '.join(c.evidence)})" if c.evidence else ""
            lines.append(f"  - {mark} [{c.weight}] {c.name}: {c.message}{ev}")
        lines.append("")
    return "\n".join(lines)

# ------------------------------------------------------------
# CLI
# ------------------------------------------------------------
def main():
    parser = argparse.ArgumentParser(description="REB Requirements Audit v2")
    parser.add_argument("--requirements", type=str, default=str(REQ_FILE), help="Path para raw_requirements.md")
    parser.add_argument("--json-out", type=str, default=str(ART_DIR / "requirements_audit_v2.json"))
    parser.add_argument("--md-out", type=str, default=str(ART_DIR / "requirements_audit_v2.md"))
    parser.add_argument("--only", nargs="*", help="Rodar apenas requisitos específicos (ex.: FR-001 NFR-SAF-001)")
    args = parser.parse_args()

    req_path = Path(args.requirements)
    if not req_path.exists():
        raise SystemExit(f"Arquivo de requisitos não encontrado: {req_path}")

    ART_DIR.mkdir(exist_ok=True)

    files = collect_sources()
    req_text = read_text(req_path)
    req_ids = extract_requirements(req_text)

    registry = build_registry()
    selected = set(args.only) if args.only else None

    results: List[RequirementResult] = []
    for req_id in req_ids:
        if selected is not None and req_id not in selected:
            continue
        fn = registry.get(req_id)
        if fn is None:
            results.append(generic_check(req_id, files))
        else:
            results.append(fn(files))

    # ordenar por requirement
    results.sort(key=lambda x: x.requirement)

    global_score = compute_global_score(results)

    json_payload = {
        "global_score": global_score,
        "requirements_total": len(results),
        "results": [asdict(r) for r in results],
    }

    json_out = Path(args.json_out)
    md_out = Path(args.md_out)

    json_out.write_text(json.dumps(json_payload, indent=2, ensure_ascii=False), encoding="utf-8")
    md_out.write_text(to_markdown(results, global_score), encoding="utf-8")

    implemented = sum(1 for r in results if r.covered == "implemented")
    partial = sum(1 for r in results if r.covered == "partial")
    missing = sum(1 for r in results if r.covered == "missing")

    print("=== REB Requirements Audit v2 ===")
    print(f"Requirements avaliados: {len(results)}")
    print(f"Implemented: {implemented}")
    print(f"Partial: {partial}")
    print(f"Missing: {missing}")
    print(f"Global score: {global_score}/100")
    print(f"JSON: {json_out}")
    print(f"MD:   {md_out}")

if __name__ == "__main__":
    main()
