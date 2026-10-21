#!/usr/bin/env bash
set -Eeuo pipefail
[[ $EUID -eq 0 ]] || { echo 'Run as root.' >&2; exit 1; }
echo '=== PRE-APT: platform and package manager ==='
if ! command -v apt-get >/dev/null || ! command -v dpkg-query >/dev/null; then
  echo 'Debian apt/dpkg unavailable' >&2; exit 1
fi
[[ $(dpkg --print-architecture) == armhf ]] || { echo 'Expected Debian armhf' >&2; exit 1; }
[[ $(uname -m) == armv7l || $(uname -m) == armhf ]] || { echo 'Expected 32-bit ARM image' >&2; exit 1; }
[[ -e /etc/bpi-zero-wbuild-release ]] || { echo 'BPI image marker missing' >&2; exit 1; }
for d in /dev/spidev0.0 /dev/i2c-0 /dev/gpiochip0; do
  [[ -e $d ]] || { echo "Missing $d" >&2; exit 1; }
done
if [[ -e /var/lib/dpkg/lock-frontend ]]; then
  echo 'Checking dpkg health (do not start a second apt operation concurrently).'
fi
dpkg --audit
printf 'Architecture: %s; kernel: %s\n' "$(dpkg --print-architecture)" "$(uname -r)"
