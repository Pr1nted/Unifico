#!/usr/bin/env python3
"""Collect the launcher's English strings and merge in the game's translations.

    python3 tools/extract_strings.py [--od ../OpenDoctrines] [--check]

1. Every T("...") and N_("...") literal under src/ goes into lang/en.json.
2. Every other lang/<code>.json is brought into line: keys no longer used are
   dropped, new ones are added empty (falling back to English when drawn).
3. With --od, the achievement names and descriptions, and any launcher string
   the GAME already translates identically, are copied from Open Doctrines'
   data/lang/<code>.json -- so the collection reads the same in both programs
   and nothing is translated twice.

--check fails when lang/en.json is out of date.
"""
import argparse
import json
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
LANG = os.path.join(ROOT, "lang")
LIT = re.compile(r'\b(?:T|N_)\(\s*"((?:[^"\\]|\\.)*)"\s*\)')


def unescape(s):
    return bytes(s, "utf-8").decode("unicode_escape").encode("latin-1").decode("utf-8")


def collect():
    found = set()
    for dirpath, _, names in os.walk(os.path.join(ROOT, "src")):
        for n in names:
            if not n.endswith((".cpp", ".h")):
                continue
            text = open(os.path.join(dirpath, n), encoding="utf-8").read()
            for m in LIT.finditer(text):
                found.add(unescape(m.group(1)))
    # The catalog's names and descriptions are drawn through T() at run time.
    gen = os.path.join(ROOT, "src", "gen", "catalog.gen.h")
    if os.path.exists(gen):
        for line in open(gen, encoding="utf-8"):
            if line.strip().startswith('{"'):
                parts = json.loads("[" + line.strip().rstrip(",").strip("{}") .replace("true", "true") + "]") if False else None
                lits = re.findall(r'"((?:[^"\\]|\\.)*)"', line)
                if len(lits) >= 4:
                    found.add(unescape(lits[2]))
                    found.add(unescape(lits[3]))
    return sorted(k for k in found if k.strip())


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--od", default="")
    ap.add_argument("--check", action="store_true")
    a = ap.parse_args()
    keys = collect()
    en = {k: k for k in keys}
    path = os.path.join(LANG, "en.json")
    old = json.load(open(path, encoding="utf-8")) if os.path.exists(path) else {}
    if a.check:
        if old != en:
            print(f"lang/en.json is out of date ({len(set(en) ^ set(old))} differences); run tools/extract_strings.py")
            return 1
        print(f"strings: {len(en)} keys, en.json current")
        return 0
    os.makedirs(LANG, exist_ok=True)
    json.dump(en, open(path, "w", encoding="utf-8"), ensure_ascii=False, indent=1, sort_keys=True)
    codes = []
    od_lang = os.path.join(a.od, "data", "lang") if a.od else ""
    if od_lang and os.path.isdir(od_lang):
        codes = sorted(f[:-5] for f in os.listdir(od_lang) if f.endswith(".json") and not f.endswith(".names.json") and f != "en.json")
    codes = sorted(set(codes) | {f[:-5] for f in os.listdir(LANG) if f.endswith(".json") and f != "en.json"})
    for code in codes:
        p = os.path.join(LANG, f"{code}.json")
        have = json.load(open(p, encoding="utf-8")) if os.path.exists(p) else {}
        if od_lang:
            odp = os.path.join(od_lang, f"{code}.json")
            if os.path.exists(odp):
                od = json.load(open(odp, encoding="utf-8"))
                for k in keys:
                    if not have.get(k) and od.get(k):
                        have[k] = od[k]
        out = {k: have.get(k, "") for k in keys}
        json.dump(out, open(p, "w", encoding="utf-8"), ensure_ascii=False, indent=1, sort_keys=True)
    done = {c: sum(1 for v in json.load(open(os.path.join(LANG, f"{c}.json"), encoding="utf-8")).values() if v) for c in codes}
    print(f"strings: {len(keys)} keys; translated per language: " + ", ".join(f"{c} {n}" for c, n in done.items()))
    return 0


if __name__ == "__main__":
    sys.exit(main())
