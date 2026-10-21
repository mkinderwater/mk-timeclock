#!/usr/bin/env bash
set -u
printf '%s\n' '=== Timeclock non-destructive checks ==='
for dev in /dev/spidev0.0 /dev/i2c-0; do if [ -e "$dev" ]; then echo "OK $dev"; else echo "MISSING $dev"; fi; done
for svc in timeclock-server timeclock-agent timeclock-mailer; do printf '%s: ' "$svc"; systemctl is-active "$svc" 2>/dev/null || true; done
if command -v curl >/dev/null 2>&1; then curl --max-time 5 -s -o /dev/null -w 'Backend HTTP %{http_code}\n' http://127.0.0.1:8080/login || true; fi
if command -v sqlite3 >/dev/null 2>&1 && [ -f /var/lib/timeclock/server.db ]; then echo 'Server SQLite integrity:'; sqlite3 -readonly /var/lib/timeclock/server.db 'PRAGMA quick_check;' || true; fi
if command -v timedatectl >/dev/null 2>&1; then timedatectl show -p NTPSynchronized || true; fi
printf 'Current timezone: '; date +%Z\ %z
printf '%s\n' 'No services restarted; no files changed.'
