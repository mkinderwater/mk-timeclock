#!/usr/bin/env bash
set -Eeuo pipefail
[[ $EUID -eq 0 ]] || { echo 'Run as root.' >&2; exit 1; }
# The clock exclusively owns HTTP port 80. TLS is served separately on 443.
conf=/etc/nginx/conf.d/timeclock-http-redirect.conf
stock=/etc/nginx/sites-enabled/default
removed_stock=0
if [[ -L "$stock" && "$(readlink -f "$stock")" == /etc/nginx/sites-available/default ]]; then
    rm "$stock"
    removed_stock=1
fi
previous=$(mktemp)
had_previous=0
if [[ -f "$conf" ]]; then cp -p "$conf" "$previous"; had_previous=1; fi
cat > "$conf" <<'EOF'
server {
    listen 80 default_server;
    server_name _;
    return 308 https://$host$request_uri;
}
EOF
if ! nginx -t; then
    if [[ $had_previous == 1 ]]; then cp -p "$previous" "$conf"; else rm -f "$conf"; fi
    if [[ $removed_stock == 1 ]]; then ln -s /etc/nginx/sites-available/default "$stock"; fi
    rm -f "$previous"
    echo 'HTTP redirect could not be enabled; previous nginx configuration restored.' >&2
    exit 1
fi
rm -f "$previous"
systemctl reload-or-restart nginx.service
