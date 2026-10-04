#!/usr/bin/env python3
"""Merge translations into lang/<code>.json:  python3 tools/put_strings.py de < batch.json
Refuses keys that are not in lang/en.json and translations that change the
printf placeholders."""
import json, os, re, sys
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
code = sys.argv[1]
en = json.load(open(os.path.join(ROOT, "lang", "en.json"), encoding="utf-8"))
path = os.path.join(ROOT, "lang", f"{code}.json")
have = json.load(open(path, encoding="utf-8")) if os.path.exists(path) else {}
batch = json.load(sys.stdin)
ph = re.compile(r"%[-+ 0#]*\d*(?:\.\d+)?[a-zA-Z]")
bad = [k for k in batch if k not in en]
mism = [k for k, v in batch.items() if k in en and v and ph.findall(k) != ph.findall(v)]
if bad or mism:
    for k in bad[:10]: print("unknown key:", repr(k))
    for k in mism[:10]: print("placeholder mismatch:", repr(k))
    sys.exit(1)
n = 0
for k, v in batch.items():
    if v and have.get(k) != v:
        have[k] = v; n += 1
out = {k: have.get(k, "") for k in en}
json.dump(out, open(path, "w", encoding="utf-8"), ensure_ascii=False, indent=1, sort_keys=True)
print(f"{code}: +{n}, now {sum(1 for v in out.values() if v)}/{len(en)}")
