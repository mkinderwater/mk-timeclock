#!/usr/bin/env bash
set -euo pipefail
echo '=== Devices ==='
ls -l /dev/i2c-0 /dev/spidev0.0 /dev/gpiochip0
echo '=== Services ==='
systemctl --no-pager --full status timeclock-server.service || true
systemctl is-enabled timeclock-agent.service || true
systemctl is-active timeclock-agent.service || true
echo '=== Legacy reader (should be inactive) ==='
systemctl is-active timeclock-enroll-reader.service || true
echo '=== Agent logs ==='
journalctl -u timeclock-agent -n 12 --no-pager || true
echo '=== HTTP backend ==='
curl -sS -o /dev/null -w 'HTTP %{http_code}\n' --max-time 4 http://127.0.0.1:8080/login || true
echo '=== Time sync ==='
timedatectl show -p NTPSynchronized || true
echo '=== Data directory ==='
ls -la /var/lib/timeclock/
