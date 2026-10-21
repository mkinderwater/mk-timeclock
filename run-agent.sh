#!/usr/bin/env bash
set -Eeuo pipefail
CONF=/etc/timeclock/timeclock.env
BIN=/opt/icubedev-timeclock/bin
"$BIN/check-config.sh" "$CONF"
source "$BIN/tc-env.sh"
tc_load_env "$CONF"
unset SMTP_USER SMTP_PASSWORD
# DEVICE_KEY is read from the environment ("-"), so it never appears in ps output.
exec "$BIN/timeclock-agent" \
 /var/lib/timeclock/queue.db "$CLOCK_ID" "$SERVER_URL" - \
 "$SPI_DEV" "$GPIOCHIP" "$BUZZER_OFFSET" "$I2C_DEV" "$TTF_FONT" "$I2C_ADDR"
