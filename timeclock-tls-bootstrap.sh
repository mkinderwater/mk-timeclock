#!/usr/bin/env bash
set -Eeuo pipefail
umask 077
# Only bootstrap a new installation. Never replace an existing active certificate.
[[ $EUID -eq 0 ]] || exit 1
[[ -e /etc/timeclock/tls/active ]] && exit 0
[[ -e /etc/nginx/conf.d/timeclock-tls.conf ]] && { echo 'Existing nginx TLS configuration found; manual review required' >&2; exit 1; }
command -v nginx >/dev/null || { echo 'nginx is not installed' >&2; exit 1; }
install -d -m 0700 /etc/timeclock/tls/versions
chown root:timeclock /etc/timeclock/tls
chmod 0710 /etc/timeclock/tls
host=$(hostname -s | tr -cd 'A-Za-z0-9.-')
[[ -n $host ]] || host=timeclock
version=/etc/timeclock/tls/versions/bootstrap
install -d -m 0700 "$version"
openssl req -x509 -nodes -newkey rsa:2048 -sha256 -days 365 \
 -subj "/CN=$host" \
 -addext "subjectAltName=DNS:$host,DNS:localhost,IP:127.0.0.1" \
 -keyout "$version/key.pem" -out "$version/fullchain.pem" >/dev/null 2>&1
chmod 0600 "$version/key.pem"
chmod 0644 "$version/fullchain.pem"
port=$(sed -nE 's/^LISTEN_PORT=([0-9]+)$/\1/p' /etc/timeclock/timeclock.env | head -1)
port=${port:-8080}
[[ $port =~ ^[0-9]{2,5}$ ]] || exit 1
cat > /etc/nginx/conf.d/timeclock-tls.conf <<EOF
server {
    listen 443 ssl;
    server_name _;
    ssl_certificate $version/fullchain.pem;
    ssl_certificate_key $version/key.pem;
    ssl_protocols TLSv1.2 TLSv1.3;
    ssl_session_tickets off;
    client_max_body_size 32k;
    location /api/v1/admin/backups/upload {
        client_max_body_size 64m;
        proxy_read_timeout 150s;
        proxy_send_timeout 150s;
        proxy_pass http://127.0.0.1:$port;
        proxy_set_header Host \$http_host;
    }
    location / {
        proxy_pass http://127.0.0.1:$port;
        proxy_http_version 1.1;
        proxy_set_header Host \$http_host;
        proxy_set_header X-Forwarded-Proto https;
        proxy_set_header X-Real-IP \$remote_addr;
    }
}
EOF
if ! nginx -t >/dev/null 2>&1 || ! systemctl enable --now nginx.service || ! systemctl reload-or-restart nginx.service; then
 rm -f /etc/nginx/conf.d/timeclock-tls.conf
 echo 'HTTPS bootstrap failed. Application services have not been started.' >&2
 exit 1
fi
touch /etc/timeclock/tls/active /etc/timeclock/tls/temporary
chmod 0644 /etc/timeclock/tls/active /etc/timeclock/tls/temporary
not_after=$(date -d "$(openssl x509 -in "$version/fullchain.pem" -noout -enddate | cut -d= -f2)" +%s 2>/dev/null || true)
if [[ $not_after =~ ^[0-9]+$ ]]; then printf '%s\n' "$not_after" > /etc/timeclock/tls/not_after; chmod 0644 /etc/timeclock/tls/not_after; fi
printf 'HTTPS is on with a temporary self-signed certificate. Install a trusted certificate under Settings > HTTPS certificate.\n'
