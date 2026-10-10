#!/bin/bash
# Import the client identity and paired hosts from Moonlight Qt's config
# (flatpak: ~/.var/app/com.moonlight_stream.Moonlight/config/Moonlight Game Streaming Project/Moonlight.conf)
# into ~/.config/moonlight-xr (or $MLXR_CONFIG_DIR). Read-only on the Qt side.
#   seed-from-moonlight-qt.sh [--conf FILE] [--dest DIR] [--force]
# Importing the identity keeps the pairing Sunshine already has with Moonlight Qt
# (the host pins the client certificate), so no new PIN is needed. GPLv3.
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
exec python3 -I "$here/seed_moonlight_qt.py" "$@"
