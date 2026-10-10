#!/bin/bash
# Builds stock moonlight-qt 6.1.0 (the submodule, unpatched) plus ONLY the screenshot hooks, to
# compare its flat UI with the launcher's pixel for pixel. build-launcher/stock/obj/app/moonlight.
# GPLv3.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
B="$ROOT/build-launcher/stock"
if [ ! -f "$B/src/.hooked" ]; then
    rm -rf "$B/src"; mkdir -p "$B/src"
    rsync -a --exclude='.git*' --exclude=/libs "$ROOT/third_party/moonlight-qt/" "$B/src/"
    cp "$ROOT/launcher/src/uihooks.h" "$B/src/app/"
    python3 -I - "$B/src" <<'PY'
import sys
r=sys.argv[1]
def edit(p, old, new):
    s=open(r+'/'+p).read(); assert s.count(old)==1, (p, old); open(r+'/'+p,'w').write(s.replace(old,new))
edit('app/main.cpp','#include "gui/sdlgamepadkeynavigation.h"\n','#include "gui/sdlgamepadkeynavigation.h"\n#include "uihooks.h"\n')
edit('app/main.cpp','''        if (engine.rootObjects().isEmpty())
            return -1;
''','''        if (engine.rootObjects().isEmpty())
            return -1;
        MlxrUiHooks::install(engine);
''')
edit('app/gui/main.qml','    StackView {\n        id: stackView\n','    StackView {\n        id: stackView\n        objectName: "stackView"\n')
PY
    touch "$B/src/.hooked"
fi
mkdir -p "$B/obj"; cd "$B/obj"
[ -f Makefile ] || qmake6 ../src/moonlight-qt.pro CONFIG+=release
nice -n 10 make -j"${JOBS:-3}"
