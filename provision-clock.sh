#!/usr/bin/env bash
set -Eeuo pipefail
[[ ${EUID} -eq 0 ]] || { echo 'Run as root on primary' >&2; exit 1; }
ID=${1:-}
[[ $ID =~ ^[A-Za-z0-9_-]{1,48}$ ]] || { echo 'Usage: ./provision-clock.sh CLOCK-002' >&2; exit 2; }
DB=/var/lib/timeclock/server.db
[[ -f $DB ]] || { echo 'Primary database not initialized' >&2; exit 2; }
command -v openssl >/dev/null && command -v sqlite3 >/dev/null || { echo 'Need openssl and sqlite3' >&2; exit 2; }
KEY=$(openssl rand -hex 32)
# CREATE only. Never silently rotate a live device's key.
if ! sqlite3 "$DB" "INSERT INTO registered_clocks(clock_id,device_key,active) VALUES('$ID','$KEY',1);"; then
 echo 'Registration failed. Existing clock keys must not be overwritten.' >&2; exit 1
fi
printf 'CLOCK_ID=%s\nDEVICE_KEY=%s\n' "$ID" "$KEY"
echo 'Copy these settings into the CLOCK device config securely. This key will not be shown again.' >&2
