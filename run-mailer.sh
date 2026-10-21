#!/usr/bin/env bash
set -Eeuo pipefail
CONF=/etc/timeclock/timeclock.env
BIN=/opt/icubedev-timeclock/bin
source "$BIN/tc-env.sh"
tc_load_env "$CONF"
unset DEVICE_KEY
exec "$BIN/timeclock-mailer" /var/lib/timeclock/server.db
