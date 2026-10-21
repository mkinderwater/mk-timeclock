#!/usr/bin/env bash
set -Eeuo pipefail
CONF=/etc/timeclock/timeclock.env
BIN=/opt/icubedev-timeclock/bin
[[ -f $CONF ]] || { echo "Missing $CONF" >&2; exit 1; }
source "$BIN/tc-env.sh"
tc_load_env "$CONF"
unset SMTP_USER SMTP_PASSWORD
[[ ${#DEVICE_KEY} -ge 16 ]] || { echo 'Missing credentials' >&2; exit 1; }
# DEVICE_KEY is read from the environment, not argv.
exec "$BIN/timeclock-server" /var/lib/timeclock/server.db "${LISTEN_PORT:-8080}"
