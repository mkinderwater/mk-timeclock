#!/usr/bin/env bash
# Explicit destructive reset only. Never run automatically during install.
set -euo pipefail
if [[ "${1:-}" != '--erase-all-timeclock-data' ]]; then
  echo 'This irreversibly deletes current SQLite timeclock data on this device.' >&2
  echo 'Run explicitly with --erase-all-timeclock-data only on a fresh test deployment.' >&2
  exit 2
fi
if [[ $(id -u) -ne 0 ]]; then echo 'Run as root.' >&2; exit 2; fi
for unit in timeclock-server.service timeclock-agent.service timeclock-mailer.service; do
  systemctl stop "$unit" 2>/dev/null || :
done
shopt -s nullglob
for path in /var/lib/timeclock/*.db /var/lib/timeclock/*.db-wal /var/lib/timeclock/*.db-shm; do
  rm -- "$path"
done
printf '%s\n' 'SQLite state erased. Start/reinitialize primary first, then re-enroll clocks.'
