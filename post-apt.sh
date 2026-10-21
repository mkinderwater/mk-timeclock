#!/usr/bin/env bash
set -Eeuo pipefail
[[ $EUID -eq 0 ]] || { echo 'Run as root.' >&2; exit 1; }
echo '=== POST-APT: verify dependencies and nginx ==='
packages=(nginx build-essential pkg-config libsqlite3-dev libcurl4-openssl-dev libfreetype-dev libi2c-dev fonts-dejavu-core ca-certificates curl sqlite3 python3 openssl)
for package in "${packages[@]}"; do
  dpkg-query -W -f='${Status}' "$package" 2>/dev/null | grep -qx 'install ok installed' || {
    echo "Required package unavailable: $package" >&2; exit 1;
  }
done
pkg-config --exists freetype2 || { echo 'FreeType pkg-config check failed' >&2; exit 1; }
# Explicitly test with an isolated config: the production site may not exist yet.
nginx_conf=$(mktemp)
nginx_err=$(mktemp)
trap 'rm -f "$nginx_conf" "$nginx_err"' EXIT
cat > "$nginx_conf" <<'EOF'
worker_processes 1;
pid /tmp/timeclock-nginx-install-check.pid;
events { worker_connections 64; }
http { server { listen 127.0.0.1:18080; location / { return 200 "OK"; } } }
EOF
nginx_healthy() {
  local verify
  verify=$(dpkg -V nginx 2>&1 || true)
  if printf '%s\n' "$verify" | grep -Eq '^..5.....[[:space:]]+/usr/sbin/nginx$'; then
    echo 'nginx executable checksum mismatch.' >&2
    return 1
  fi
  if ! nginx -t -c "$nginx_conf" -p /tmp/ >"$nginx_err" 2>&1; then
    cat "$nginx_err" >&2
    return 1
  fi
}
if ! nginx_healthy; then
  echo 'nginx failed integrity or isolated syntax check. Reinstalling once.' >&2
  apt-get install -y --reinstall --no-install-recommends nginx
  nginx_healthy || { echo 'nginx remains unhealthy; aborting installation.' >&2; exit 1; }
fi
echo 'nginx executable and isolated configuration: OK'
