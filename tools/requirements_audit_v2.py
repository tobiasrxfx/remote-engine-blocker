#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
REB Requirements Audit v3 (evolução do v2)
- Lê docs/architecture/raw_requirements.md
- Avalia TODOS os requisitos FR/NFR por regras estáticas + semânticas
- Separa success_message e failure_message
- Classifica evidência em forte/fraca
- Calcula score de conformidade por requisito (0-100)
- Gera:
  - artifacts/requirements_audit_v3.json
  - artifacts/requirements_audit_v3.md
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
class Evidence:
    path: str
    excerpt: str
    strength: str  # strong | weak

@dataclass
class RuleCheck:
    name: str
    weight: int
    ok: bool
    success_message: str
    failure_message: str
    evidences: List[Evidence] = field(default_factory=list)

@dataclass
class RequirementResult:
    requirement: str
    covered: str          # implemented | partial | missing
    correctness: str      # pass | fail | unknown
    score: int            # 0..100
    errors: List[str]
    warnings: List[str]
    strong_evidence: List[str]
    weak_evidence: List[str]
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

def find_all(files: Dict[str, str], pattern: str, flags=re.IGNORECASE | re.MULTILINE) -> List[Evidence]:
    out: List[Evidence] = []
    rx = re.compile(pattern, flags)
    for f, t in files.items():
        for m in rx.finditer(t):
            snippet = t[max(0, m.start()-40): min(len(t), m.end()+80)].replace("\n", " ").strip()
            out.append(Evidence(path=f, excerpt=snippet, strength="weak"))
            break
    return out

def find_in_file(files: Dict[str, str], path: str, pattern: str, flags=re.IGNORECASE | re.MULTILINE) -> bool:
    t = files.get(path, "")
    return re.search(pattern, t, flags=flags) is not None

def extract_requirements(md: str) -> List[str]:
    return REQ_ID_RE.findall(md)

def _find_matching_brace(text: str, open_idx: int) -> int:
    level = 0
    for i in range(open_idx, len(text)):
        c = text[i]
        if c == "{":
            level += 1
        elif c == "}":
            level -= 1
            if level == 0:
                return i
    return -1

def extract_function_body(file_text: str, fn_name: str) -> Optional[str]:
    # parser simples: encontra assinatura e balanceia chaves
    sig = re.search(rf"\b{re.escape(fn_name)}\s*\([^;]*\)\s*\{{", file_text)
    if not sig:
        return None
    open_idx = file_text.find("{", sig.start())
    close_idx = _find_matching_brace(file_text, open_idx)
    if open_idx == -1 or close_idx == -1:
        return None
    return file_text[open_idx:close_idx+1]

def extract_case_block(switch_body: str, case_label: str) -> Optional[str]:
    m = re.search(rf"case\s+{re.escape(case_label)}\s*:\s*", switch_body)
    if not m:
        return None
    rest = switch_body[m.end():]
    stop = re.search(r"\n\s*(?:case\s+|default\s*:)", rest)
    return rest[:stop.start()] if stop else rest

def semantic_transition_in_state_machine(files: Dict[str, str], from_state: str, to_state: str) -> Tuple[bool, List[Evidence]]:
    sm = "src/reb_core/reb_state_machine.c"
    txt = files.get(sm, "")
    fn = extract_function_body(txt, "reb_state_machine_step") or txt
    case = extract_case_block(fn, from_state)
    if not case:
        return False, []
    ok = re.search(rf"current_state\s*=\s*{re.escape(to_state)}", case) is not None
    ev = [Evidence(path=sm, excerpt=f"case {from_state}: ... current_state = {to_state}", strength="strong")] if ok else []
    return ok, ev

def semantic_guard_before_transition(files: Dict[str, str], from_state: str, to_state: str, guard_pattern: str) -> Tuple[bool, List[Evidence]]:
    sm = "src/reb_core/reb_state_machine.c"
    txt = files.get(sm, "")
    fn = extract_function_body(txt, "reb_state_machine_step") or txt
    case = extract_case_block(fn, from_state)
    if not case:
        return False, []
    trans_pos = re.search(rf"current_state\s*=\s*{re.escape(to_state)}", case)
    guard = re.search(guard_pattern, case, re.IGNORECASE | re.MULTILINE)
    ok = trans_pos is not None and guard is not None and guard.start() < trans_pos.start()
    ev = [Evidence(path=sm, excerpt=f"guard antes de {from_state}->{to_state}", strength="strong")] if ok else []
    return ok, ev

def summarize(checks: List[RuleCheck]) -> Tuple[str, str, int, List[str], List[str], List[str], List[str]]:
    total = sum(c.weight for c in checks) or 1
    got = sum(c.weight for c in checks if c.ok)
    score = int(round((got / total) * 100))

    failed = [c for c in checks if not c.ok]
    passed = [c for c in checks if c.ok]

    errors = [f"{c.name}: {c.failure_message}" for c in failed]
    warnings: List[str] = []

    if score >= 80:
        covered = "implemented"
    elif score >= 30:
        covered = "partial"
    else:
        covered = "missing"

    if len(failed) == 0:
        correctness = "pass"
    elif len(passed) == 0:
        correctness = "fail"
    else:
        correctness = "unknown"

    strong = sorted({e.path for c in checks for e in c.evidences if e.strength == "strong"})
    weak = sorted({e.path for c in checks for e in c.evidences if e.strength == "weak"})
    return covered, correctness, score, errors, warnings, strong, weak

def rule(name: str, weight: int, ok: bool, success_message: str, failure_message: str, evidences: Optional[List[Evidence]] = None) -> RuleCheck:
    return RuleCheck(name=name, weight=weight, ok=ok, success_message=success_message, failure_message=failure_message, evidences=evidences or [])

# ------------------------------------------------------------
# Requirement rule engine
# ------------------------------------------------------------
def check_fr_001(files: Dict[str, str]) -> RequirementResult:
    sm = "src/reb_core/reb_state_machine.c"
    st = all(find_in_file(files, sm, p) for p in [r"REB_STATE_IDLE", r"REB_STATE_THEFT_CONFIRMED", r"REB_STATE_BLOCKING", r"REB_STATE_BLOCKED"])
    t1_ok, t1_ev = semantic_transition_in_state_machine(files, "REB_STATE_IDLE", "REB_STATE_THEFT_CONFIRMED")
    t2_ok, t2_ev = semantic_transition_in_state_machine(files, "REB_STATE_THEFT_CONFIRMED", "REB_STATE_BLOCKING")
    t3_ok, t3_ev = semantic_transition_in_state_machine(files, "REB_STATE_BLOCKING", "REB_STATE_BLOCKED")
    lg = find_all(files, r"reb_logger_(?:info|warn|error|write)")
    checks = [
        rule("FSM com estados principais", 20, st, "Estados principais encontrados.", "Estados principais ausentes.", [Evidence(sm, "enum/state refs", "weak")]),
        rule("Transição semântica IDLE->THEFT_CONFIRMED", 25, t1_ok, "Transição semântica identificada.", "Transição semântica não identificada.", t1_ev),
        rule("Transição semântica THEFT_CONFIRMED->BLOCKING", 25, t2_ok, "Transição semântica identificada.", "Transição semântica não identificada.", t2_ev),
        rule("Transição semântica BLOCKING->BLOCKED", 20, t3_ok, "Transição semântica identificada.", "Transição semântica não identificada.", t3_ev),
        rule("Registro em log de transições/eventos", 10, len(lg) > 0, "Logging evidenciado.", "Logging não evidenciado.", lg),
    ]
    c, k, s, e, w, strong, weak = summarize(checks)
    return RequirementResult("FR-001", c, k, s, e, w, strong, weak, checks)

def check_fr_002(files: Dict[str, str]) -> RequirementResult:
    sm = "src/reb_core/reb_state_machine.c"
    sec = "src/reb_core/reb_security.c"
    guard_ok, guard_ev = semantic_guard_before_transition(files, "REB_STATE_IDLE", "REB_STATE_THEFT_CONFIRMED", r"reb_security_validate_remote_command")
    invalid_reject = find_in_file(files, sec, r"return\s+false")
    no_direct_lock = not semantic_guard_before_transition(files, "REB_STATE_IDLE", "REB_STATE_BLOCKED", r".")[0]
    checks = [
        rule("Validação semântica de comando remoto antes da transição", 40, guard_ok, "Guarda de segurança encontrada antes da transição.", "Guarda de segurança ausente/fora da ordem.", guard_ev),
        rule("Rejeição de requisições inválidas", 25, invalid_reject, "Rejeição explícita encontrada.", "Sem rejeição explícita de inválidos.", [Evidence(sec, "return false", "weak")]),
        rule("Sem bloqueio direto em IDLE", 20, no_direct_lock, "Não há transição direta para bloqueio final em IDLE.", "Há indício de bloqueio direto em IDLE.", [Evidence(sm, "case IDLE sem salto direto para BLOCKED", "strong")]),
        rule("Evento confirmado em log", 15, find_in_file(files, sm, r"THEFT_CONFIRMED") and find_in_file(files, sm, r"reb_logger"), "Confirmação/log encontrados.", "Sem confirmação/log suficientes.", [Evidence(sm, "THEFT_CONFIRMED + log", "weak")]),
    ]
    c, k, s, e, w, strong, weak = summarize(checks)
    return RequirementResult("FR-002", c, k, s, e, w, strong, weak, checks)

def check_fr_003(files: Dict[str, str]) -> RequirementResult:
    sm = "src/reb_core/reb_state_machine.c"
    speed_guard, speed_ev = semantic_guard_before_transition(files, "REB_STATE_BLOCKING", "REB_STATE_BLOCKED", r"vehicle_speed_kmh\s*<=\s*REB_MAX_ALLOWED_SPEED_FOR_LOCK")
    checks = [
        rule("Uso de vehicle_speed_kmh", 25, find_in_file(files, sm, r"vehicle_speed_kmh"), "Variável de velocidade usada.", "Variável de velocidade ausente na lógica.", [Evidence(sm, "vehicle_speed_kmh", "weak")]),
        rule("Uso de ignition_on", 20, find_in_file(files, sm, r"ignition_on"), "ignition_on usado.", "ignition_on não usado.", [Evidence(sm, "ignition_on", "weak")]),
        rule("Uso de engine_running", 20, find_in_file(files, sm, r"engine_running"), "engine_running usado.", "engine_running não usado.", [Evidence(sm, "engine_running", "weak")]),
        rule("Guarda semântica de velocidade antes do lock final", 35, speed_guard, "Guarda de velocidade aplicada antes da transição crítica.", "Guarda de velocidade semântica não comprovada.", speed_ev),
    ]
    c, k, s, e, w, strong, weak = summarize(checks)
    return RequirementResult("FR-003", c, k, s, e, w, strong, weak, checks)

def check_fr_004(files: Dict[str, str]) -> RequirementResult:
    ca = "src/reb_core/reb_can_adapter.c"
    checks = [
        rule("Comando local via painel/interface", 40, find_in_file(files, ca, r"CAN_MSG_PANEL_"), "Mensagens de painel identificadas.", "Mensagens de painel não identificadas.", [Evidence(ca, "CAN_MSG_PANEL_", "weak")]),
        rule("Validação de autorização local", 30, find_in_file(files, ca, r"auth|authorized|permission"), "Autorização local evidenciada.", "Autorização local não evidenciada.", [Evidence(ca, "auth/permission", "weak")]),
        rule("Fluxo local alcança THEFT_CONFIRMED", 30, find_in_file(files, "src/reb_core/reb_state_machine.c", r"THEFT_CONFIRMED") and find_in_file(files, ca, r"panel|local"), "Fluxo local + estado alvo evidenciados.", "Fluxo local até THEFT_CONFIRMED não evidenciado.", [Evidence(ca, "entrada local", "weak")]),
    ]
    c, k, s, e, w, strong, weak = summarize(checks)
    return RequirementResult("FR-004", c, k, s, e, w, strong, weak, checks)

def check_fr_005(files: Dict[str, str]) -> RequirementResult:
    ca = "src/reb_core/reb_can_adapter.c"
    sm = "src/reb_core/reb_state_machine.c"
    checks = [
        rule("Recebe comando remoto via TCU/CAN", 35, find_in_file(files, ca, r"CAN_MSG_REB_CMD|CAN_MSG_TCU_TO_REB"), "Entrada TCU/CAN encontrada.", "Entrada TCU/CAN não encontrada.", [Evidence(ca, "CAN_MSG_REB_CMD/TCU", "weak")]),
        rule("ACK/status para TCU", 35, find_in_file(files, sm, r"send_status_to_tcu") or find_in_file(files, ca, r"CAN_MSG_REB_STATUS"), "ACK/status para TCU evidenciado.", "ACK/status para TCU não evidenciado.", [Evidence(sm, "send_status_to_tcu", "weak"), Evidence(ca, "CAN_MSG_REB_STATUS", "weak")]),
        rule("Transição remota para THEFT_CONFIRMED", 30, semantic_guard_before_transition(files, "REB_STATE_IDLE", "REB_STATE_THEFT_CONFIRMED", r"REB_REMOTE_BLOCK")[0], "Transição remota validada semanticamente.", "Transição remota sem validação semântica.", semantic_guard_before_transition(files, "REB_STATE_IDLE", "REB_STATE_THEFT_CONFIRMED", r"REB_REMOTE_BLOCK")[1]),
    ]
    c, k, s, e, w, strong, weak = summarize(checks)
    return RequirementResult("FR-005", c, k, s, e, w, strong, weak, checks)

def check_nfr_saf_001(files: Dict[str, str]) -> RequirementResult:
    sm = "src/reb_core/reb_state_machine.c"
    guard_ok, guard_ev = semantic_guard_before_transition(files, "REB_STATE_BLOCKING", "REB_STATE_BLOCKED", r"vehicle_speed_kmh\s*<=\s*REB_MAX_ALLOWED_SPEED_FOR_LOCK")
    checks = [
        rule("Proíbe starter lock em movimento por guarda semântica", 55, guard_ok, "Guarda semântica de segurança encontrada.", "Guarda semântica de segurança não encontrada.", guard_ev),
        rule("Usa bloqueio gradual (derating) em movimento", 45, find_in_file(files, sm, r"reb_apply_derating|derate_percent"), "Derating em movimento evidenciado.", "Derating em movimento não evidenciado.", [Evidence(sm, "reb_apply_derating/derate_percent", "weak")]),
    ]
    c, k, s, e, w, strong, weak = summarize(checks)
    return RequirementResult("NFR-SAF-001", c, k, s, e, w, strong, weak, checks)

def check_nfr_rel_001(files: Dict[str, str]) -> RequirementResult:
    core = "src/reb_core/reb_core.c"
    per = "src/reb_core/reb_persistence.c"
    checks = [
        rule("Persiste contexto/estado em NVM", 40, find_in_file(files, per, r"reb_persistence_save") and find_in_file(files, per, r"fopen"), "Persistência em NVM evidenciada.", "Persistência em NVM não evidenciada.", [Evidence(per, "save + fopen", "strong")]),
        rule("Recupera estado após reset/power loss", 40, find_in_file(files, core, r"reb_persistence_load"), "Recuperação após reset evidenciada.", "Recuperação após reset não evidenciada.", [Evidence(core, "reb_persistence_load", "strong")]),
        rule("Integridade de dado persistido (CRC)", 20, find_in_file(files, per, r"crc|calculate_crc"), "Validação de integridade encontrada.", "Validação de integridade não encontrada.", [Evidence(per, "crc", "weak")]),
    ]
    c, k, s, e, w, strong, weak = summarize(checks)
    return RequirementResult("NFR-REL-001", c, k, s, e, w, strong, weak, checks)

def check_nfr_info_001(files: Dict[str, str]) -> RequirementResult:
    lg = "src/reb_core/reb_logger.c"
    sec = "src/reb_core/reb_security.c"
    checks = [
        rule("Gera log de eventos críticos", 40, find_in_file(files, lg, r"reb_logger_write|reb_logger_info|reb_logger_warn|reb_logger_error"), "Infraestrutura de log encontrada.", "Infraestrutura de log não encontrada.", [Evidence(lg, "logger API", "weak")]),
        rule("Persistência de logs/traços", 25, find_in_file(files, lg, r"artifacts/logs/reb.log"), "Persistência de log evidenciada.", "Persistência de log não evidenciada.", [Evidence(lg, "reb.log", "weak")]),
        rule("Não expõe nonce em plaintext", 35, not find_in_file(files, sec, r"printf\([^)]*nonce"), "Sem exposição de nonce em printf.", "Possível exposição de nonce em printf.", [Evidence(sec, "scan printf(nonce)", "strong")]),
    ]
    c, k, s, e, w, strong, weak = summarize(checks)
    return RequirementResult("NFR-INFO-001", c, k, s, e, w, strong, weak, checks)

def check_nfr_sw_001(files: Dict[str, str]) -> RequirementResult:
    checks = [
        rule("Evidência de verificação MISRA/static analysis", 50, len(find_all(files, r"MISRA|cppcheck|pc-lint|clang-tidy|static analysis")) > 0, "Ferramentas/processo de análise estática evidenciados.", "Sem evidência de MISRA/static analysis no código fonte.", find_all(files, r"MISRA|cppcheck|pc-lint|clang-tidy|static analysis")),
        rule("Relatório MISRA presente em artifacts", 50, (ROOT / "artifacts" / "misra_report.txt").exists() or (ROOT / "artifacts" / "misra_report.xml").exists(), "Relatório MISRA presente.", "Relatório MISRA ausente em artifacts/.", [Evidence("artifacts/misra_report.*", "existence check", "strong")]),
    ]
    c, k, s, e, w, strong, weak = summarize(checks)
    return RequirementResult("NFR-SW-001", c, k, s, e, w, strong, weak, checks)

def generic_check(req_id: str, files: Dict[str, str]) -> RequirementResult:
    token = re.escape(req_id).replace(r"\-", r"[-_ ]?")
    ev = find_all(files, token)
    checks = [
        rule("Evidência textual do requisito", 100, len(ev) > 0, "ID do requisito referenciado textualmente.", "Sem referência textual direta ao requisito.", ev)
    ]
    c, k, s, e, w, strong, weak = summarize(checks)
    return RequirementResult(req_id, c, k, s, e, w, strong, weak, checks)

def build_registry():
    # Mantém cobertura total de IDs; requisitos não especializados caem em generic_check.
    specialized = {
        "FR-001": check_fr_001,
        "FR-002": check_fr_002,
        "FR-003": check_fr_003,
        "FR-004": check_fr_004,
        "FR-005": check_fr_005,
        "NFR-SAF-001": check_nfr_saf_001,
        "NFR-REL-001": check_nfr_rel_001,
        "NFR-INFO-001": check_nfr_info_001,
        "NFR-SW-001": check_nfr_sw_001,
    }
    return specialized

# ------------------------------------------------------------
# Report
# ------------------------------------------------------------
def compute_global_score(results: List[RequirementResult]) -> int:
    if not results:
        return 0
    return int(round(sum(r.score for r in results) / len(results)))

def to_markdown(results: List[RequirementResult], global_score: int) -> str:
    lines: List[str] = []
    lines.append("# REB Requirements Audit v3")
    lines.append("")
    lines.append(f"**Global compliance score:** {global_score}/100")
    lines.append("")
    lines.append("| Requirement | Covered | Correctness | Score | Strong evidence | Weak evidence | Errors |")
    lines.append("|---|---|---|---:|---|---|---|")
    for r in results:
        err = "; ".join(r.errors) if r.errors else "-"
        sev = ", ".join(r.strong_evidence) if r.strong_evidence else "-"
        wev = ", ".join(r.weak_evidence) if r.weak_evidence else "-"
        lines.append(f"| {r.requirement} | {r.covered} | {r.correctness} | {r.score} | {sev} | {wev} | {err} |")
    lines.append("")
    lines.append("## Detailed checks")
    for r in results:
        lines.append(f"### {r.requirement}")
        lines.append(f"- Covered: **{r.covered}**")
        lines.append(f"- Correctness: **{r.correctness}**")
        lines.append(f"- Score: **{r.score}/100**")
        lines.append(f"- Strong evidence: {', '.join(r.strong_evidence) if r.strong_evidence else '-'}")
        lines.append(f"- Weak evidence: {', '.join(r.weak_evidence) if r.weak_evidence else '-'}")
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
            msg = c.success_message if c.ok else c.failure_message
            ev = ", ".join(f"{e.path}({e.strength})" for e in c.evidences[:5]) if c.evidences else "-"
            lines.append(f"  - {mark} [{c.weight}] {c.name}: {msg} | evidence: {ev}")
        lines.append("")
    return "\n".join(lines)

# ------------------------------------------------------------
# CLI
# ------------------------------------------------------------
def main():
    parser = argparse.ArgumentParser(description="REB Requirements Audit v3")
    parser.add_argument("--requirements", type=str, default=str(REQ_FILE), help="Path para raw_requirements.md")
    parser.add_argument("--json-out", type=str, default=str(ART_DIR / "requirements_audit_v3.json"))
    parser.add_argument("--md-out", type=str, default=str(ART_DIR / "requirements_audit_v3.md"))
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

    results.sort(key=lambda x: x.requirement)
    global_score = compute_global_score(results)

    json_payload = {
        "version": "v3",
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

    print("=== REB Requirements Audit v3 ===")
    print(f"Requirements avaliados: {len(results)}")
    print(f"Implemented: {implemented}")
    print(f"Partial: {partial}")
    print(f"Missing: {missing}")
    print(f"Global score: {global_score}/100")
    print(f"JSON: {json_out}")
    print(f"MD:   {md_out}")

if __name__ == "__main__":
    main()
