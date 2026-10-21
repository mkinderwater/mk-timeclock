#!/usr/bin/env bash
set -Eeuo pipefail
request=/var/lib/timeclock/ntp.request
status=/var/lib/timeclock/ntp-status.txt
server=/var/lib/timeclock/ntp-server.txt
[ -f "$request" ] || exit 0
# Only process a regular request, and consume it exactly once.
[ ! -L "$request" ] || exit 1
host=$(head -c 210 "$request" | tr -d '\r\n')
rm -f "$request"
report(){ printf '%s\n' "$1" > "$status"; chown timeclock:timeclock "$status"; chmod 0640 "$status"; }
if [[ ! "$host" =~ ^[a-zA-Z0-9][a-zA-Z0-9.:-]{0,198}[a-zA-Z0-9]$ && ! "$host" =~ ^[a-zA-Z0-9]$ ]]; then report 'Rejected invalid server'; exit 1; fi
if ! dpkg-query -W -f='${Status}' systemd-timesyncd 2>/dev/null | grep -qx 'install ok installed'; then report 'Not applied: systemd-timesyncd is unavailable; configure the installed time daemon'; exit 0; fi
if systemctl is-active --quiet chrony.service || systemctl is-active --quiet ntpsec.service || systemctl is-active --quiet ntp.service || systemctl is-active --quiet openntpd.service; then
 report 'Not applied: a different NTP service is active';exit 0
fi
mkdir -p /etc/systemd/timesyncd.conf.d
printf '[Time]\nNTP=%s\n' "$host" > /etc/systemd/timesyncd.conf.d/90-icubedev-timeclock.conf
chmod 0644 /etc/systemd/timesyncd.conf.d/90-icubedev-timeclock.conf
printf '%s\n' "$host" > "$server";chown timeclock:timeclock "$server";chmod 0640 "$server"
if systemctl restart systemd-timesyncd.service; then report "Applied NTP server: $host (waiting for synchronization)"; else report 'NTP configuration written, but service restart failed';exit 1;fi
