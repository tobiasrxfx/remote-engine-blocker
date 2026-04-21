#!/usr/bin/env python3
from __future__ import annotations

import json
import re
from dataclasses import asdict, dataclass
from pathlib import Path
from typing import Callable, Dict, Iterable, List, Tuple

ROOT = Path(__file__).resolve().parents[1]
REQ_FILE = ROOT / "docs/architecture/raw_requirements.md"
SRC_DIR = ROOT / "src"
ART_DIR = ROOT / "artifacts"
ART_DIR.mkdir(exist_ok=True)

REQ_ID_RE = re.compile(r"^###\s+((?:FR-\d{3}|NFR-[A-Z]{3}-\d{3}))\b", re.MULTILINE)


@dataclass
class Result:
    requirement: str
    covered: str  # implemented | partial | missing
    correctness: str  # pass | fail | unknown
    errors: List[str]
    evidence: List[str]


def read_text(path: Path) -> str:
    return path.read_text(encoding="utf-8", errors="ignore")


def collect_sources() -> Dict[str, str]:
    files: Dict[str, str] = {}
    for p in sorted(SRC_DIR.rglob("*")):
        if p.suffix.lower() in {".c", ".h"} and p.is_file():
            files[str(p.relative_to(ROOT))] = read_text(p)
    files[str(REQ_FILE.relative_to(ROOT))] = read_text(REQ_FILE)
    return files


def any_match(files: Dict[str, str], patterns: Iterable[str]) -> List[str]:
    compiled = [re.compile(p, re.IGNORECASE | re.MULTILINE) for p in patterns]
    ev: List[str] = []
    for file_path, text in files.items():
        if all(p.search(text) for p in compiled):
            ev.append(file_path)
    return ev


def check_fr_012(files: Dict[str, str]) -> Result:
    errs: List[str] = []
    ev: List[str] = []
    cfg_path = "src/reb_core/reb_config.h"
    sm_path = "src/reb_core/reb_state_machine.c"
    cfg = files.get(cfg_path, "")
    sm = files.get(sm_path, "")

    ev.extend(any_match({cfg_path: cfg}, [r"REB_THEFT_CONFIRM_WINDOW_MS"]))
    ev.extend(any_match({sm_path: sm}, [r"THEFT_CONFIRMED", r"REB_THEFT_CONFIRM_WINDOW_MS"]))

    m = re.search(r"#define\s+REB_THEFT_CONFIRM_WINDOW_MS\s+\((\d+)U\)", cfg)
    if not m:
        return Result("FR-012", "missing", "fail", ["Constante de janela não encontrada"], ev)

    ms = int(m.group(1))
    if ms != 90000:
        errs.append(f"Janela configurada em {ms}ms, requisito pede 90000ms")
        return Result("FR-012", "partial", "fail", errs, ev)

    return Result("FR-012", "implemented", "pass", [], ev)


def check_fr_011(files: Dict[str, str]) -> Result:
    sm_path = "src/reb_core/reb_state_machine.c"
    sm = files.get(sm_path, "")
    ev = any_match({sm_path: sm}, [r"case\s+REB_STATE_BLOCKED"]) 
    errs: List[str] = []

    blocked_case = re.search(
        r"case\s+REB_STATE_BLOCKED\s*:\s*\{([\s\S]*?)\n\s*break;",
        sm,
        re.MULTILINE,
    )
    if not blocked_case:
        return Result("FR-011", "missing", "fail", ["Estado BLOCKED não encontrado"], ev)

    blocked_body = blocked_case.group(1)
    if re.search(r"derate_percent\s*=\s*REB_DERATE_MAX_PERCENT", blocked_body):
        errs.append("Em BLOCKED há derating ativo; requisito pede desativar derating")
        return Result("FR-011", "partial", "fail", errs, ev)

    return Result("FR-011", "implemented", "pass", [], ev)


def check_fr_009(files: Dict[str, str]) -> Result:
    sm_path = "src/reb_core/reb_state_machine.c"
    cfg_path = "src/reb_core/reb_config.h"
    sm = files.get(sm_path, "")
    cfg = files.get(cfg_path, "")

    ev: List[str] = []
    ev.extend(any_match({sm_path: sm}, [r"derate_percent"]))
    ev.extend(any_match({cfg_path: cfg}, [r"REB_DERATE_MIN_PERCENT"]))

    errs: List[str] = []
    # Aceita clamp explícito via max() OU fallback em ramo else definindo o mínimo.
    has_explicit_floor = bool(
        re.search(r"max\s*\(", sm, re.IGNORECASE)
        or re.search(r"else\s*\{[\s\S]*derate_percent\s*=\s*REB_DERATE_MIN_PERCENT", sm)
    )
    if not has_explicit_floor:
        errs.append("Não há garantia explícita de piso mínimo de derate em movimento")

    m = re.search(r"#define\s+REB_DERATE_MIN_PERCENT\s+\((\d+)U\)", cfg)
    if not m:
        errs.append("Piso mínimo de derate não encontrado na configuração")
    elif int(m.group(1)) <= 0:
        errs.append("Piso mínimo de derate inválido (<= 0)")

    if errs:
        return Result("FR-009", "partial", "fail", errs, ev)

    return Result("FR-009", "implemented", "pass", [], ev)


def check_nfr_saf_002(files: Dict[str, str]) -> Result:
    joined = "\n".join(files.values())
    ev: List[str] = []

    if re.search(r"signal_status|timeout|invalid", joined, re.IGNORECASE):
        ev.append("src/* (indícios: status/timeout/invalid)")
    if re.search(r"failsafe|safe.*mode|fallback|default", joined, re.IGNORECASE):
        ev.append("src/* (indícios: fallback/failsafe)")

    if not ev:
        return Result(
            "NFR-SAF-002",
            "missing",
            "fail",
            ["Não há lógica explícita de fallback sob perda/corrupção de sinal"],
            [],
        )

    return Result(
        "NFR-SAF-002",
        "partial",
        "unknown",
        ["Indícios encontrados; validar cobertura real com testes SIL de falha de sinal"],
        ev,
    )


def generic_requirement_check(req_id: str, files: Dict[str, str]) -> Result:
    token = req_id.replace("-", r"[-_ ]?")
    ev = any_match(files, [token])
    if ev:
        return Result(
            req_id,
            "partial",
            "unknown",
            ["Evidência textual encontrada; falta validação semântica"],
            ev[:5],
        )
    return Result(req_id, "missing", "unknown", ["Sem evidência estática direta"], [])


def extract_requirements(md: str) -> List[str]:
    return REQ_ID_RE.findall(md)


def to_terminal_table(results: List[Result]) -> str:
    rows = [[r.requirement, r.covered, r.correctness, "; ".join(r.errors) if r.errors else "-"] for r in results]
    headers = ["Requirement", "Covered", "Correctness", "Errors"]
    widths = [len(h) for h in headers]
    for row in rows:
        for i, col in enumerate(row):
            widths[i] = max(widths[i], len(col))

    def fmt(row: List[str]) -> str:
        return " | ".join(col.ljust(widths[i]) for i, col in enumerate(row))

    sep = "-+-".join("-" * w for w in widths)
    lines = [fmt(headers), sep]
    lines.extend(fmt(r) for r in rows)
    return "\n".join(lines)


def build_markdown(results: List[Result]) -> str:
    lines = [
        "# Requirements Audit",
        "",
        "## Resumo",
        "",
        "| Requirement | Covered | Correctness | Errors |",
        "|---|---|---|---|",
    ]
    for r in results:
        err = "; ".join(r.errors) if r.errors else "-"
        lines.append(f"| {r.requirement} | {r.covered} | {r.correctness} | {err} |")

    lines.append("")
    lines.append("## Evidências")
    lines.append("")
    for r in results:
        lines.append(f"### {r.requirement}")
        if r.evidence:
            for e in r.evidence:
                lines.append(f"- {e}")
        else:
            lines.append("- (sem evidência estática)")
        lines.append("")
    return "\n".join(lines)


def status_counts(results: List[Result]) -> Tuple[int, int, int]:
    implemented = sum(1 for r in results if r.covered == "implemented")
    partial = sum(1 for r in results if r.covered == "partial")
    missing = sum(1 for r in results if r.covered == "missing")
    return implemented, partial, missing


def main() -> None:
    files = collect_sources()
    req_md = files[str(REQ_FILE.relative_to(ROOT))]
    reqs = extract_requirements(req_md)

    special: Dict[str, Callable[[Dict[str, str]], Result]] = {
        "FR-009": check_fr_009,
        "FR-011": check_fr_011,
        "FR-012": check_fr_012,
        "NFR-SAF-002": check_nfr_saf_002,
    }

    results: List[Result] = []
    for req in reqs:
        fn = special.get(req)
        results.append(fn(files) if fn else generic_requirement_check(req, files))

    data = [asdict(r) for r in results]
    (ART_DIR / "requirements_audit.json").write_text(
        json.dumps(data, indent=2, ensure_ascii=False),
        encoding="utf-8",
    )

    (ART_DIR / "requirements_audit.md").write_text(
        build_markdown(results),
        encoding="utf-8",
    )

    implemented, partial, missing = status_counts(results)

    print(to_terminal_table(results))
    print()
    print(f"Total: {len(results)}")
    print(f"Implemented: {implemented}")
    print(f"Partial: {partial}")
    print(f"Missing: {missing}")
    print(f"JSON: {ART_DIR / 'requirements_audit.json'}")
    print(f"MD:   {ART_DIR / 'requirements_audit.md'}")


if __name__ == "__main__":
    main()
