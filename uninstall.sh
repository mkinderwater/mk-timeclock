#!/usr/bin/env bash
set -Eeuo pipefail
[[ $EUID -eq 0 ]] || exit 1
systemctl disable --now timeclock-backup.timer timeclock-tls-apply.path timeclock-ntp-apply.path timeclock-system-action.path || true
systemctl disable --now timeclock-agent.service timeclock-server.service timeclock-mailer.service || true
rm -f /etc/systemd/system/timeclock-backup.service /etc/systemd/system/timeclock-backup.timer
rm -f /etc/systemd/system/timeclock-tls-apply.service /etc/systemd/system/timeclock-tls-apply.path
rm -f /etc/systemd/system/timeclock-ntp-apply.service /etc/systemd/system/timeclock-ntp-apply.path
rm -f /etc/systemd/system/timeclock-system-action.service /etc/systemd/system/timeclock-system-action.path
rm -f /etc/systemd/system/timeclock-agent.service /etc/systemd/system/timeclock-server.service /etc/systemd/system/timeclock-mailer.service
# Remove only the timeclock-owned nginx proxy; preserve keys and other nginx sites.
rm -f /etc/nginx/conf.d/timeclock-tls.conf /etc/nginx/conf.d/timeclock-http-redirect.conf
command -v nginx >/dev/null && nginx -t >/dev/null 2>&1 && systemctl reload nginx.service || true
systemctl daemon-reload
rm -rf /opt/icubedev-timeclock
printf 'Programs removed. Data and credentials PRESERVED: /var/lib/timeclock /etc/timeclock\n'
