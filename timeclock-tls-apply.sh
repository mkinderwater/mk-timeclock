#!/usr/bin/env bash
set -Eeuo pipefail
umask 077
stage=/var/lib/timeclock/tls-stage
request=$stage/request
[[ -d $stage && ! -L $stage && -f $request && ! -L $request ]] || exit 0
[[ $(cat "$request") == apply ]] || { rm -f "$request"; exit 1; }
rm -f "$request"
status() { printf '%s\n' "$1" > "$stage/status.tmp"; chmod 0644 "$stage/status.tmp"; mv -f "$stage/status.tmp" "$stage/status"; }
status 'Validating uploaded certificate'
for file in leaf.pem chain.pem key.pem hostname; do
  [[ -f "$stage/$file" && ! -L "$stage/$file" ]] || { status 'Failed: missing upload'; exit 1; }
  [[ $(stat -c %s "$stage/$file") -le 9000 ]] || { status 'Failed: PEM too large'; exit 1; }
done
hostname=$(cat "$stage/hostname")
[[ $hostname =~ ^[A-Za-z0-9][A-Za-z0-9._-]{0,248}$ ]] || { status 'Failed: invalid hostname'; exit 1; }
work=$(mktemp -d /etc/timeclock/tls-check.XXXXXXXX)
cleanup() { rm -rf "$work"; rm -f "$stage/leaf.pem" "$stage/chain.pem" "$stage/key.pem" "$stage/hostname"; }
trap cleanup EXIT
cp -- "$stage/leaf.pem" "$work/leaf.pem"
cp -- "$stage/chain.pem" "$work/chain.pem"
cp -- "$stage/key.pem" "$work/key.pem"
chmod 0600 "$work"/*.pem
openssl x509 -in "$work/leaf.pem" -noout >/dev/null 2>&1 || { status 'Failed: invalid leaf certificate'; exit 1; }
openssl pkey -in "$work/key.pem" -passin pass: -noout </dev/null >/dev/null 2>&1 || { status 'Failed: invalid or encrypted private key'; exit 1; }
openssl x509 -in "$work/leaf.pem" -checkend 604800 -noout >/dev/null 2>&1 || { status 'Failed: certificate expires within 7 days'; exit 1; }
openssl x509 -in "$work/leaf.pem" -ext subjectAltName -noout 2>/dev/null | grep -q -E 'DNS:|IP Address:' || { status 'Failed: certificate lacks SAN'; exit 1; }
if [[ "$hostname" =~ ^[0-9]+\.[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
  openssl x509 -in "$work/leaf.pem" -checkip "$hostname" -noout 2>/dev/null | grep -q 'does match' || { status 'Failed: IP address not in SAN'; exit 1; }
else
  openssl x509 -in "$work/leaf.pem" -checkhost "$hostname" -noout 2>/dev/null | grep -q 'does match' || { status 'Failed: hostname does not match SAN'; exit 1; }
fi
cert_pub=$(openssl x509 -in "$work/leaf.pem" -pubkey -noout | openssl pkey -pubin -outform DER | openssl dgst -sha256)
key_pub=$(openssl pkey -in "$work/key.pem" -passin pass: -pubout -outform DER 2>/dev/null | openssl dgst -sha256)
[[ "$cert_pub" == "$key_pub" ]] || { status 'Failed: private key does not match certificate'; exit 1; }
if [[ -s $work/chain.pem ]]; then
  openssl crl2pkcs7 -nocrl -certfile "$work/chain.pem" | openssl pkcs7 -print_certs -noout >/dev/null 2>&1 || { status 'Failed: invalid intermediate chain'; exit 1; }
  openssl verify -purpose sslserver -untrusted "$work/chain.pem" -CApath /etc/ssl/certs "$work/leaf.pem" >/dev/null 2>&1 || { status 'Failed: certificate chain is not trusted'; exit 1; }
else
  openssl verify -purpose sslserver -CApath /etc/ssl/certs "$work/leaf.pem" >/dev/null 2>&1 || { status 'Failed: missing or untrusted CA chain'; exit 1; }
fi
command -v nginx >/dev/null || { status 'Failed: nginx is not installed'; exit 1; }
mkdir -p -m 0700 /etc/timeclock/tls/versions
port=$(sed -nE 's/^LISTEN_PORT=([0-9]+)$/\1/p' /etc/timeclock/timeclock.env | head -1)
port=${port:-8080}
[[ $port =~ ^[0-9]{2,5}$ ]] || { status 'Failed: invalid backend port'; exit 1; }
version=$(date -u +%Y%m%dT%H%M%SZ)-$$
archive=/etc/timeclock/tls/versions/$version
install -d -m 0700 "$archive"
# Explicit boundaries prevent concatenating END/BEGIN markers when uploads lack final newlines.
{
  cat "$work/leaf.pem"
  printf '\n'
  cat "$work/chain.pem"
  printf '\n'
} > "$archive/fullchain.pem"
# Parse each PEM block; a valid first certificate alone is not enough.
awk -v out="$work" '
  /^-----BEGIN CERTIFICATE-----[ \t]*$/ { n++; inside=1; file=sprintf("%s/assembled-%d.pem",out,n) }
  inside { print > file }
  /^-----END CERTIFICATE-----[ \t]*$/ { inside=0; close(file); finished++ }
  END { if (inside || n==0 || n!=finished) exit 1 }
' "$archive/fullchain.pem" || { status 'Failed: assembled certificate chain has invalid PEM boundaries'; exit 1; }
expected=$(grep -c '^-----BEGIN CERTIFICATE-----[[:blank:]]*$' "$work/leaf.pem" || true)
chain_count=$(grep -c '^-----BEGIN CERTIFICATE-----[[:blank:]]*$' "$work/chain.pem" || true)
expected=$((expected + chain_count))
actual=$(find "$work" -maxdepth 1 -name 'assembled-*.pem' | wc -l)
[[ $actual -eq $expected && $actual -ge 1 ]] || { status 'Failed: assembled certificate chain is incomplete'; exit 1; }
for pem in "$work"/assembled-*.pem; do
  openssl x509 -in "$pem" -noout >/dev/null 2>&1 || { status 'Failed: assembled certificate chain contains invalid certificate'; exit 1; }
done
openssl crl2pkcs7 -nocrl -certfile "$archive/fullchain.pem" | openssl pkcs7 -print_certs -noout >/dev/null 2>&1 || { status 'Failed: assembled certificate chain is invalid'; exit 1; }
install -m 0600 "$work/key.pem" "$archive/key.pem"
chmod 0644 "$archive/fullchain.pem"
config=/etc/nginx/conf.d/timeclock-tls.conf
previous=$work/previous.conf
if [[ -f $config ]]; then cp -p "$config" "$previous"; fi
cat > "$work/config" <<EOF
server {
    listen 443 ssl;
    server_name $hostname;
    ssl_certificate $archive/fullchain.pem;
    ssl_certificate_key $archive/key.pem;
    ssl_protocols TLSv1.2 TLSv1.3;
    ssl_session_tickets off;
    add_header Strict-Transport-Security "max-age=86400" always;
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
install -m 0644 "$work/config" "$config"
nginx_error=''
nginx_failed=0
if ! nginx -t >"$work/nginx-test.log" 2>&1; then
  nginx_failed=1
  nginx_error=$(tail -n 4 "$work/nginx-test.log" | tr '\n' ' ' | cut -c 1-350)
elif ! systemctl reload-or-restart nginx.service >"$work/nginx-reload.log" 2>&1; then
  nginx_failed=1
  nginx_error=$(tail -n 4 "$work/nginx-reload.log" | tr '\n' ' ' | cut -c 1-350)
fi
if [[ $nginx_failed -eq 1 ]]; then
  [[ -n $nginx_error ]] || nginx_error='nginx validation/reload returned failure without diagnostic output' 
  if [[ -f $previous ]]; then cp -p "$previous" "$config"; else rm -f "$config"; fi
  nginx -t >/dev/null 2>&1 && systemctl reload-or-restart nginx.service || true
  status "Failed: TLS configuration rejected; previous configuration restored. $nginx_error"
  printf 'timeclock TLS apply failed: %s\n' "$nginx_error" >&2
  exit 1
fi
# Let the backend traverse only the TLS status directory. Keep versions and keys root-only.
chown root:timeclock /etc/timeclock/tls
chmod 0710 /etc/timeclock/tls
touch /etc/timeclock/tls/active
chmod 0644 /etc/timeclock/tls/active
rm -f /etc/timeclock/tls/temporary
# Expiry time for the GUI warning (Unix seconds).
not_after=$(date -d "$(openssl x509 -in "$work/leaf.pem" -noout -enddate | cut -d= -f2)" +%s 2>/dev/null || true)
if [[ $not_after =~ ^[0-9]+$ ]]; then printf '%s\n' "$not_after" > /etc/timeclock/tls/not_after.tmp; chmod 0644 /etc/timeclock/tls/not_after.tmp; mv -f /etc/timeclock/tls/not_after.tmp /etc/timeclock/tls/not_after; fi
status "Active: certificate for $hostname"
systemctl try-restart timeclock-server.service || true
