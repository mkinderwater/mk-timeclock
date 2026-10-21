#!/usr/bin/env bash
set -Eeuo pipefail
request=/var/lib/timeclock/timezone.request
status=/var/lib/timeclock/timezone-status.txt
[[ -f "$request" && ! -L "$request" ]] || exit 0
tz=$(head -c 110 "$request" | tr -d '\r\n')
rm -f "$request"
report(){ printf '%s\n' "$1" > "$status"; chown timeclock:timeclock "$status"; chmod 0640 "$status"; }
[[ "$tz" =~ ^[A-Za-z0-9_+-]+(/[A-Za-z0-9_+-]+)+$ && "$tz" != *..* && -f "/usr/share/zoneinfo/$tz" ]] || { report 'Rejected invalid timezone';exit 1; }
if ! timedatectl set-timezone "$tz"; then report 'Timezone change failed';exit 1;fi
# Apply the timezone to every service that uses localtime_r(). Only restart running units.
if systemctl try-restart timeclock-server.service timeclock-agent.service timeclock-mailer.service; then
 report "Timezone applied: $tz; services restarted"
else
 report "Timezone applied: $tz; service restart failed (check systemctl status)"
 exit 1
fi
