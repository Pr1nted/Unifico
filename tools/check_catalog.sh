#!/bin/sh
# Fail when the launcher's generated achievement catalog has drifted from the
# game's, which is the one source of truth.
#
# There is deliberately no second copy of the hash function here. This runs the
# GAME's generator against this checkout (gen_achievements.py --check --unifico),
# so the check cannot quietly disagree with whatever produced the file. Both
# copies carry the same kCatalogHash; that is what drifts when someone adds an
# achievement and regenerates only one side.
set -eu

here=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
od=${1:-${OD_DIR:-}}

if [ -z "$od" ]; then
    for candidate in "$here/../Open-Doctrines" "$here/../OpenDoctrines" "$here/../open-doctrines"; do
        if [ -f "$candidate/tools/gen_achievements.py" ]; then
            od=$(CDPATH= cd -- "$candidate" && pwd)
            break
        fi
    done
fi

if [ -z "$od" ] || [ ! -f "$od/tools/gen_achievements.py" ]; then
    # Not verifying is not the same as passing, so this is an error and not a
    # skip: a check that goes quiet when it cannot run protects nothing.
    echo "check_catalog: no Open Doctrines checkout to compare against." >&2
    echo "  pass one:  tools/check_catalog.sh /path/to/Open-Doctrines" >&2
    echo "  or set:    OD_DIR=/path/to/Open-Doctrines" >&2
    exit 1
fi

echo "check_catalog: comparing against $od"
exec python3 "$od/tools/gen_achievements.py" --check --unifico "$here"
