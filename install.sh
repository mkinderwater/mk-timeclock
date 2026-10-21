#!/usr/bin/env bash
set -Eeuo pipefail
[[ $EUID -eq 0 ]] || { echo 'Run as root: ./install.sh' >&2; exit 1; }
# Internal shared installer. Choose an explicit role-specific entrypoint.
case "${1:-}" in
  --master) ROLE=primary ;;
  --slave) ROLE=slave ;;
  *) echo 'Use sudo ./install-master.sh or sudo ./install-slave.sh' >&2; exit 2 ;;
esac
HERE=$(cd "$(dirname "$0")" && pwd)
ENV=/etc/timeclock/timeclock.env
MARKER=/etc/timeclock/role-selected
if [[ -f "$MARKER" ]]; then
  old=$(cat "$MARKER")
  [[ "$old" == "$ROLE" ]] || { echo "Existing role '$old' differs from requested '$ROLE'. Authority transfer is required." >&2; exit 2; }
fi
if [[ -f "$ENV" ]]; then
  old=$(sed -n 's/^TIMECLOCK_ROLE=//p' "$ENV" | tail -n 1)
  [[ -z "$old" || "$old" == "$ROLE" ]] || { echo "Existing device configuration belongs to $old. Refusing role change." >&2; exit 2; }
fi
# A Slave must not adopt a populated authoritative database.
if [[ "$ROLE" == slave && -e /var/lib/timeclock/server.db ]]; then
  echo 'Primary database exists. Refusing Slave install to protect attendance records.' >&2; exit 2
fi

# Verify packaged dependencies before touching installed services or binaries.
for required in timeclock-backup timeclock-payroll-xlsx.py timeclock-attendance-reminders.py timeclock-missing-digest.py clock-setup.py timeclock-system-action.sh; do
  [[ -f "$HERE/$required" ]] || { echo "Required installation source missing: $required" >&2; exit 1; }
done
"$HERE/pre-apt.sh"
export DEBIAN_FRONTEND=noninteractive
required_packages=(nginx build-essential pkg-config libsqlite3-dev libcurl4-openssl-dev libfreetype-dev libi2c-dev fonts-dejavu-core ca-certificates curl sqlite3 python3 openssl)
missing_packages=()
for package in "${required_packages[@]}"; do
  if ! dpkg-query -W -f='${Status}' "$package" 2>/dev/null | grep -qx 'install ok installed'; then
    missing_packages+=("$package")
  fi
done
apt_updated=0
if (( ${#missing_packages[@]} )); then
  apt-get update
  apt_updated=1
  apt-get install -y --no-install-recommends "${missing_packages[@]}"
else
  echo 'Build dependencies already installed; skipping apt package operations.'
fi
# The agent refuses swipes until the system clock is NTP-synchronized. Keep an existing
# time daemon (chrony, ntpsec, ntp, openntpd); otherwise install systemd-timesyncd.
have_time_daemon=0
for p in systemd-timesyncd chrony ntpsec ntp openntpd; do
  dpkg-query -W -f='${Status}' "$p" 2>/dev/null | grep -q 'install ok installed' && have_time_daemon=1
done
if [[ $have_time_daemon -eq 0 ]]; then
  if [[ $apt_updated -eq 0 ]]; then apt-get update; apt_updated=1; fi
  apt-get install -y --no-install-recommends systemd-timesyncd
  systemctl enable --now systemd-timesyncd.service || true
fi
"$HERE/post-apt.sh"
make -C "$HERE" clean all
id timeclock >/dev/null 2>&1 || useradd --system --home /var/lib/timeclock --shell /usr/sbin/nologin timeclock
install -d -m 0750 -o timeclock -g timeclock /var/lib/timeclock
install -d -m 0755 /opt/icubedev-timeclock/bin
install -m 0755 "$HERE/clock-setup.py" /opt/icubedev-timeclock/bin/clock-setup.py
install -m 0644 "$HERE/mk-timeclock-setup.service" /etc/systemd/system/mk-timeclock-setup.service
install -m 0755 "$HERE/timeclock-agent" /opt/icubedev-timeclock/bin/
install -m 0755 "$HERE/timeclock-server" /opt/icubedev-timeclock/bin/

install -m 0755 "$HERE/timeclock-mailer" /opt/icubedev-timeclock/bin/
install -m 0755 "$HERE/timeclock-payroll-xlsx.py" /opt/icubedev-timeclock/bin/
install -m 0755 "$HERE/timeclock-attendance-reminders.py" "$HERE/timeclock-missing-digest.py" /opt/icubedev-timeclock/bin/
install -m 0644 "$HERE/timeclock-attendance-reminders.service" /etc/systemd/system/
install -m 0644 "$HERE/timeclock-attendance-reminders.timer" "$HERE/timeclock-missing-digest.timer" "$HERE/timeclock-missing-digest.service" /etc/systemd/system/
install -m 0755 "$HERE/run-mailer.sh" /opt/icubedev-timeclock/bin/
install -m 0755 "$HERE/run-agent.sh" /opt/icubedev-timeclock/bin/
install -m 0755 "$HERE/run-server.sh" /opt/icubedev-timeclock/bin/
install -m 0755 "$HERE/check-config.sh" /opt/icubedev-timeclock/bin/
install -m 0755 "$HERE/timeclock-ntp-apply.sh" "$HERE/timeclock-timezone-apply.sh" /opt/icubedev-timeclock/bin/
install -m 0755 "$HERE/timeclock-system-action.sh" /opt/icubedev-timeclock/bin/
# The manager-authorized backup action invokes this executable.
[[ -f "$HERE/timeclock-backup" ]] || { echo "Required backup helper missing" >&2; exit 1; }
install -m 0755 "$HERE/timeclock-backup" /opt/icubedev-timeclock/bin/timeclock-backup
install -m 0755 "$HERE/timeclock-backup-upload.py" /opt/icubedev-timeclock/bin/timeclock-backup-upload.py
install -d -m 0700 -o timeclock -g timeclock /var/lib/timeclock/backup-inbox
install -m 0644 "$HERE/timeclock-backup-upload.service" "$HERE/timeclock-backup-upload.path" /etc/systemd/system/

# Backup exports remain root owned, readable only by the timeclock server group.
install -d -m 0750 -o root -g timeclock /var/backups/timeclock
find /var/backups/timeclock -maxdepth 1 -type f \( -name "timeclock-*.tar.gz" -o -name "timeclock-*.tar.gz.sha256" \) -exec chown root:timeclock {} + -exec chmod 0640 {} +
install -m 0755 "$HERE/timeclock-tls-apply.sh" /opt/icubedev-timeclock/bin/
install -m 0755 "$HERE/timeclock-tls-bootstrap.sh" /opt/icubedev-timeclock/bin/
install -m 0644 "$HERE/tc-env.sh" /opt/icubedev-timeclock/bin/
install -m 0750 "$HERE/timeclock-add-manager.sh" /opt/icubedev-timeclock/bin/
install -m 0750 "$HERE/timeclock-register-primary.sh" /opt/icubedev-timeclock/bin/
install -m 0750 "$HERE/timeclock-primary-manager-status.sh" /opt/icubedev-timeclock/bin/
install -d -m 0750 -o root -g timeclock /etc/timeclock
# Existing certificates may predate the readable status marker fix.
if [[ -d /etc/timeclock/tls ]]; then
  chown root:timeclock /etc/timeclock/tls
  chmod 0710 /etc/timeclock/tls
fi
if [[ ! -f /etc/timeclock/timeclock.env ]]; then
  umask 077
  key=$(od -An -N32 -tx1 /dev/urandom | tr -d ' \n')
  cat > /etc/timeclock/timeclock.env <<EOF
TIMECLOCK_ROLE=$ROLE
CLOCK_ID=CLOCK-001
SERVER_URL=http://127.0.0.1:8080
DEVICE_KEY=$key
LISTEN_PORT=8080
SPI_DEV=/dev/spidev0.0
GPIOCHIP=/dev/gpiochip0
BUZZER_OFFSET=6
ATTENDANCE_ENABLED=yes
I2C_DEV=/dev/i2c-0
TTF_FONT=/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf
I2C_ADDR=0x3c
# Set these to activate SMTP. Verify provider credentials.
SMTP_URL=
# none for trusted internal relay, starttls for port 587, implicit for smtps port 465
SMTP_TLS_MODE=starttls
SMTP_USER=
SMTP_PASSWORD=
SMTP_FROM=
EOF
  echo 'New installation: BPI header pin 7 (PA6) buzzer configured as /dev/gpiochip0 line 6; physical pin 9 is GND.'
  echo 'Requires an active 3.3V-compatible buzzer or GPIO-controlled transistor driver.'
  echo 'Set BUZZER_OFFSET=none to disable the buzzer.'
  echo 'Generated device credentials in /etc/timeclock/timeclock.env'
else
  echo 'Preserved /etc/timeclock/timeclock.env (and all credentials)'
fi
chown root:timeclock /etc/timeclock/timeclock.env
chmod 0640 /etc/timeclock/timeclock.env
install -m 0644 "$HERE/timeclock-backup.service" /etc/systemd/system/
install -m 0644 "$HERE/timeclock-backup.timer" /etc/systemd/system/
install -m 0644 "$HERE/timeclock-server.service" /etc/systemd/system/
install -m 0644 "$HERE/timeclock-agent.service" /etc/systemd/system/
install -m 0644 "$HERE/timeclock-mailer.service" /etc/systemd/system/
install -m 0644 "$HERE/timeclock-ntp-apply.service" /etc/systemd/system/
install -m 0644 "$HERE/timeclock-ntp-apply.path" "$HERE/timeclock-timezone-apply.path" "$HERE/timeclock-timezone-apply.service" /etc/systemd/system/
install -m 0644 "$HERE/timeclock-system-action.service" /etc/systemd/system/
install -m 0644 "$HERE/timeclock-system-action.path" /etc/systemd/system/
install -m 0644 "$HERE/timeclock-tls-apply.service" /etc/systemd/system/
install -m 0644 "$HERE/timeclock-tls-apply.path" /etc/systemd/system/

systemctl disable --now timeclock-missing-in.timer >/dev/null 2>&1 || true
systemctl daemon-reload
# New installations begin with a temporary self-signed HTTPS certificate.
# Upgrades preserve existing HTTPS credentials unchanged.
/opt/icubedev-timeclock/bin/timeclock-tls-bootstrap.sh
# Allow large archive uploads only at the authenticated upload endpoint.
# Keep the normal 32k limit for all other web routes.
python3 - <<'PY_TLS_UPLOAD'
from pathlib import Path
p=Path('/etc/nginx/conf.d/timeclock-tls.conf')
if p.exists():
    s=p.read_text()
    if 'location /api/v1/admin/backups/upload {' not in s:
        marker='    location / {'
        addition='''    location /api/v1/admin/backups/upload {
        client_max_body_size 64m;
        proxy_read_timeout 150s;
        proxy_send_timeout 150s;
        proxy_pass http://127.0.0.1:8080;
        proxy_set_header Host $http_host;
    }
'''
        # Respect the configured upstream port, including non-default installations.
        import re
        match=re.search(r'proxy_pass http://127\.0\.0\.1:(\d+);',s)
        if not match: raise SystemExit('Existing HTTPS config does not expose an expected local upstream')
        addition=addition.replace('127.0.0.1:8080', '127.0.0.1:'+match.group(1))
        if marker not in s: raise SystemExit('HTTPS config has no proxy location')
        p.write_text(s.replace(marker,addition+marker,1))
PY_TLS_UPLOAD
nginx -t >/dev/null || exit 1
systemctl reload-or-restart nginx.service
# Redirect all ordinary HTTP requests to the existing HTTPS listener.
"$HERE/timeclock-http-redirect.sh"
# Role-specific startup occurs once dependencies and units are installed.
# Keep all application services stopped during installation.
systemctl disable --now timeclock-agent.service timeclock-server.service timeclock-mailer.service timeclock-backup.timer timeclock-attendance-reminders.timer timeclock-missing-digest.timer mk-timeclock-setup.service >/dev/null 2>&1 || true
systemctl disable --now timeclock-enroll-reader.service >/dev/null 2>&1 || true
systemctl enable --now timeclock-ntp-apply.path
systemctl enable --now timeclock-timezone-apply.path
install -d -m 0700 -o timeclock -g timeclock /var/lib/timeclock/tls-stage
# Keep status markers visible to the backend on upgrades; versions/keys stay root-only.
install -d -m 0710 -o root -g timeclock /etc/timeclock/tls
systemctl enable --now timeclock-tls-apply.path
systemctl enable --now timeclock-system-action.path
systemctl enable --now timeclock-backup-upload.path
# Existing configuration may predate ATTENDANCE_ENABLED; absent means disabled.
if grep -q '^BUZZER_OFFSET=CHANGE_ME$' /etc/timeclock/timeclock.env; then
  sed -i 's/^BUZZER_OFFSET=CHANGE_ME$/BUZZER_OFFSET=none/' /etc/timeclock/timeclock.env
fi
if ! grep -q '^ATTENDANCE_ENABLED=' /etc/timeclock/timeclock.env; then
  printf '\nATTENDANCE_ENABLED=no\n' >> /etc/timeclock/timeclock.env
fi
# Persist the role before starting any role-dependent service.
if grep -q '^TIMECLOCK_ROLE=' "$ENV"; then
  sed -i "s/^TIMECLOCK_ROLE=.*/TIMECLOCK_ROLE=$ROLE/" "$ENV"
else
  printf '\nTIMECLOCK_ROLE=%s\n' "$ROLE" >> "$ENV"
fi
chown root:timeclock "$ENV"; chmod 0640 "$ENV"

if [[ "$ROLE" == primary ]]; then
  # Server creates schema; registration must succeed before agent starts.
  systemctl enable --now timeclock-server.service
  if ! /opt/icubedev-timeclock/bin/timeclock-register-primary.sh; then
    echo 'Primary registration failed. RFID agent left stopped; inspect server journal.' >&2
    exit 1
  fi
  systemctl enable --now timeclock-mailer.service timeclock-backup.timer timeclock-attendance-reminders.timer timeclock-missing-digest.timer
  systemctl enable --now timeclock-agent.service
  echo 'Create the first manager through https://<clock-ip>/setup using a dedicated RFID card.'
  echo 'Master installed. Management GUI: https://<clock-ip>/login'
else
  # No server, mailer, reminders, or backup timer on unadopted Slave.
  if [[ -f /etc/timeclock/adopted ]]; then
    systemctl enable --now timeclock-agent.service
    echo 'Adopted Slave installed; local RFID agent started.'
  else
    systemctl enable --now mk-timeclock-setup.service
    echo 'Slave installed. Adopt at http://<clock-ip>:8090/ on trusted LAN.'
  fi
fi
printf '%s\n' "$ROLE" > /etc/timeclock/role-selected
chmod 0600 /etc/timeclock/role-selected
printf 'Installed role: %s\n' "$ROLE"
