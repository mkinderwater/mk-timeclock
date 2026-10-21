#!/usr/bin/env bash
set -Eeuo pipefail
F=${1:-/etc/timeclock/timeclock.env}
HERE=$(cd "$(dirname "$0")" && pwd)
# shellcheck source=tc-env.sh
source "$HERE/tc-env.sh"
[[ -r $F ]] || { echo "Cannot read $F" >&2; exit 1; }
val(){ tc_val "$F" "$1"; }
[[ $(val CLOCK_ID) =~ ^[A-Za-z0-9_-]{1,48}$ ]] || { echo 'Invalid CLOCK_ID' >&2; exit 1; }
port=$(val LISTEN_PORT); port=${port:-8080}
[[ $port =~ ^[0-9]{1,5}$ ]] && ((10#$port > 0 && 10#$port <= 65535)) || { echo "Invalid LISTEN_PORT" >&2; exit 1; }
server_url=$(val SERVER_URL)
if [[ ${TIMECLOCK_ROLE:-$(val TIMECLOCK_ROLE)} == slave ]]; then
  [[ $server_url =~ ^https://[^/]+/?$ ]] || { echo "Clock-only role requires a trusted HTTPS server URL" >&2; exit 1; }
else
  [[ $server_url == "http://127.0.0.1:$port" ]] || { echo "Local role requires local backend URL" >&2; exit 1; }
fi
dk=$(val DEVICE_KEY)
[[ ${#dk} -ge 16 ]] || { echo 'DEVICE_KEY must be at least 16 characters' >&2; exit 1; }
[[ $(val BUZZER_OFFSET) == none || $(val BUZZER_OFFSET) == CHANGE_ME || $(val BUZZER_OFFSET) =~ ^[0-9]+$ ]] || { echo 'Configure BUZZER_OFFSET' >&2; exit 1; }
for k in SPI_DEV GPIOCHIP I2C_DEV; do [[ -e $(val "$k") ]] || { echo "Missing device $k" >&2; exit 1; }; done
[[ -f $(val TTF_FONT) ]] || { echo 'TTF_FONT not found' >&2; exit 1; }
