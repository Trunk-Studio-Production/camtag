#!/usr/bin/env python3
"""Controlla data/lenses.json: schema, unità plausibili, id unici, sigle coerenti.

Uso:  python3 data/validate.py [percorso/lenses.json]
Esce con codice 1 se trova errori. Gli avvisi (warning) non fanno fallire.
"""
import json
import re
import sys
from collections import defaultdict
from datetime import date
from pathlib import Path

TYPES = {"prime", "macro", "zoom", "anamorphic", "probe", "specialty"}
COVERAGES = {"S35", "S35+", "FF", "LF", "65"}
FIELDS = [
    "id", "brand", "series", "seriesCode", "name", "type",
    "focal", "focalMin", "focalMax", "tstop", "tstopLong", "stopType",
    "mod_m", "front_mm", "filterThread", "coverage", "imageCircle_mm",
    "squeeze", "sources", "checked", "notes",
]
ID_RE = re.compile(r"^[a-z0-9]+(-[a-z0-9.]+)*$")
CODE_RE = re.compile(r"^[A-Z][A-Z0-9]{1,3}$")
THREAD_RE = re.compile(r"^M\d{2,3}(\.\d+)?(x\d(\.\d+)?)?$")  # M82, M105x0.75


def num(v):
    return isinstance(v, (int, float)) and not isinstance(v, bool)


def check_lens(l, errs, warns):
    lid = l.get("id", "?")
    e = lambda m: errs.append(f"{lid}: {m}")
    w = lambda m: warns.append(f"{lid}: {m}")

    missing = [f for f in FIELDS if f not in l]
    extra = [f for f in l if f not in FIELDS]
    if missing:
        e(f"campi mancanti {missing}")
    if extra:
        e(f"campi sconosciuti {extra}")

    if not isinstance(l.get("id"), str) or not ID_RE.match(l.get("id", "")):
        e("id non in kebab-case minuscolo")
    for f in ("brand", "series", "name"):
        if not isinstance(l.get(f), str) or not l.get(f).strip():
            e(f"{f} vuoto")
    if not isinstance(l.get("seriesCode"), str) or not CODE_RE.match(l.get("seriesCode", "")):
        e("seriesCode deve essere 2-4 caratteri maiuscoli (lettera iniziale)")

    t = l.get("type")
    if t not in TYPES:
        e(f"type '{t}' non valido")

    if t == "zoom":
        if l.get("focal") is not None:
            e("zoom: focal deve essere null (usa focalMin/focalMax)")
        lo, hi = l.get("focalMin"), l.get("focalMax")
        if not (num(lo) and num(hi) and 1 <= lo < hi <= 2000):
            e("zoom: focalMin/focalMax mancanti o incoerenti")
        tl = l.get("tstopLong")
        if tl is not None and not (num(tl) and num(l.get("tstop")) and tl >= l["tstop"]):
            e("tstopLong deve essere >= tstop")
    else:
        if not (num(l.get("focal")) and 2 <= l["focal"] <= 2000):
            e("focal mancante o fuori scala (mm)")
        if l.get("focalMin") is not None or l.get("focalMax") is not None:
            e("focalMin/focalMax solo per gli zoom")
        if l.get("tstopLong") is not None:
            e("tstopLong solo per gli zoom")

    ts = l.get("tstop")
    if not (num(ts) and 0.7 <= ts <= 16):
        e("tstop mancante o fuori scala")
    if l.get("stopType") not in ("T", "f"):
        e("stopType deve essere 'T' o 'f'")

    mod = l.get("mod_m")
    if mod is not None and not (num(mod) and 0.01 <= mod <= 10):
        e(f"mod_m {mod} non plausibile (metri)")
    fr = l.get("front_mm")
    if fr is not None and not (num(fr) and 30 <= fr <= 250):
        e(f"front_mm {fr} non plausibile (mm)")
    ft = l.get("filterThread")
    if ft is not None and not (isinstance(ft, str) and THREAD_RE.match(ft)):
        e(f"filterThread '{ft}' non nel formato M82 o M105x0.75")
    if l.get("coverage") not in COVERAGES and l.get("coverage") is not None:
        e(f"coverage '{l.get('coverage')}' non tra {sorted(COVERAGES)}")
    ic = l.get("imageCircle_mm")
    if ic is not None and not (num(ic) and 10 <= ic <= 90):
        e(f"imageCircle_mm {ic} non plausibile")

    sq = l.get("squeeze")
    if t == "anamorphic":
        if not (num(sq) and 1 < sq <= 2.5):
            e("anamorfica senza squeeze valido")
    elif t == "zoom":
        if sq is not None and not (num(sq) and 1 < sq <= 2.5):
            e("zoom anamorfico con squeeze non valido")
    elif sq is not None:
        e("squeeze solo per le anamorfiche e gli zoom anamorfici")

    src = l.get("sources")
    if not (isinstance(src, list) and src and all(isinstance(s, str) and s.startswith("http") for s in src)):
        e("sources deve essere una lista non vuota di URL")
    try:
        d = date.fromisoformat(l.get("checked", ""))
        if d > date.today():
            e("checked nel futuro")
    except (TypeError, ValueError):
        e("checked non è una data ISO")

    nulls = [f for f in ("mod_m", "front_mm", "coverage") if l.get(f) is None]
    if nulls and not (isinstance(l.get("notes"), str) and l["notes"].strip()):
        e(f"{nulls} null senza spiegazione in notes")
    if nulls:
        w(f"null: {', '.join(nulls)}")


def main():
    path = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).with_name("lenses.json")
    data = json.loads(path.read_text(encoding="utf-8"))
    errs, warns = [], []
    if data.get("version") != 1:
        errs.append("version deve essere 1")
    lenses = data.get("lenses")
    if not isinstance(lenses, list) or not lenses:
        errs.append("lenses deve essere una lista non vuota")
        lenses = []

    ids = defaultdict(int)
    code_by_series = defaultdict(set)
    series_by_code = defaultdict(set)
    for l in lenses:
        check_lens(l, errs, warns)
        ids[l.get("id")] += 1
        key = (l.get("brand"), l.get("series"))
        code_by_series[key].add(l.get("seriesCode"))
        series_by_code[l.get("seriesCode")].add(key)

    for i, n in ids.items():
        if n > 1:
            errs.append(f"id duplicato: {i} ({n} volte)")
    for k, codes in code_by_series.items():
        if len(codes) > 1:
            errs.append(f"serie {k[0]} {k[1]}: sigle diverse {sorted(codes)}")
    for c, series in series_by_code.items():
        if len(series) > 1:
            errs.append(f"sigla {c} usata da più serie: {sorted(series)}")

    for m in warns:
        print("avviso:", m)
    for m in errs:
        print("ERRORE:", m)
    print(f"{len(lenses)} lenti, {len(code_by_series)} serie, {len(errs)} errori, {len(warns)} avvisi")
    sys.exit(1 if errs else 0)


if __name__ == "__main__":
    main()
