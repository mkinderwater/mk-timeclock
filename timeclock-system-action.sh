#!/usr/bin/env bash
set -Eeuo pipefail
request=/var/lib/timeclock/system-action.request
[ -f "$request" ] || exit 0
[ ! -L "$request" ] || exit 1
action=$(head -c 160 "$request" | tr -d '\r\n')
rm -f "$request"
case "$action" in
 reboot) systemctl reboot ;;
 shutdown) systemctl poweroff ;;
 restart)
   source /opt/icubedev-timeclock/bin/tc-env.sh
   if [[ $(tc_val /etc/timeclock/timeclock.env TIMECLOCK_ROLE) == slave ]]; then
     systemctl try-restart timeclock-agent.service
   else
     systemctl try-restart timeclock-agent.service timeclock-mailer.service timeclock-server.service
   fi
   ;;
 backup) /opt/icubedev-timeclock/bin/timeclock-backup backup ;;
 restore:*)
   filename=${action#restore:}
   [[ "$filename" =~ ^timeclock-[A-Za-z0-9.-]+\.tar\.gz$ ]] || exit 1
   [[ "$filename" != *..* ]] || exit 1
   exec /opt/icubedev-timeclock/bin/timeclock-backup restore "/var/backups/timeclock/$filename" --yes
   ;;
 *) exit 1 ;;
esac
