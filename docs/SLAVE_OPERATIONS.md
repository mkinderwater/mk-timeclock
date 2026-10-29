# Slave operations in RC5.16

## Install and update

Use sudo ./install-slave.sh on the existing supported BPI image. The shared installer builds only the hardware agent for Slave role. It installs the RFID/OLED/buzzer agent, adoption setup, configuration validation, NTP/timezone and local system-action helpers, and the local backup tool/timer. It does not install the Primary attendance server, mailer, report/reminder tools, nginx or Primary TLS/backup-upload services. Earlier unused timeclock Primary components and their nginx configurations are retired during Slave upgrade. Other nginx sites are not removed.

A fresh Slave serves adoption at http://CLOCK-IP:8090 on the trusted LAN. The Primary HTTPS certificate must be trusted by the Slave. Use the existing code/request/manager approval flow. Adoption enables the agent and local backup timer, then shuts down setup. There is no local attendance management GUI. An already adopted Slave preserves its environment, credentials and queue and resumes the agent/local backups. Role mismatch and a Primary database block Slave installation. Installer refuses to race an active backup/restore and stops application services before replacing executables.

Update the Primary and all Slaves. Older agents do not send the new heartbeat and will be flagged after the grace period. Normal brief installation interruptions queue on other running clocks; the clock being updated cannot scan until its agent restarts. Verify hardware after every installed update.

## Connectivity and manager warnings

The agent sends a heartbeat every 30 seconds without needing an OLED preview. Primary records receipt time, pending count, oldest pending timestamp, unreported local rejection count, queue-read health and time synchronization. Manager Clock management shows these fields. An offline count is last reported, not a live count. Wi-Fi signal strength alone does not prove Primary connectivity.

Primary startup and the transition to confirmed time synchronization each begin a fresh three-minute monitoring window. Unsynchronized time cannot trigger outage emails. A fresh heartbeat is required to close a genuine pre-existing outage. After 180 seconds without a heartbeat, the next Primary minute tick queues one outage email for each active manager email address. New clocks receive the same first-heartbeat grace period. Recovery closes its outage episode even if its email cannot be queued; the preparation warning stays visible and a later outage receives a new identity. One recovery email per address follows a new heartbeat; it states whether uploads are still draining. Email timestamps use clock-local time with numeric UTC offsets, including oldest pending swipe time. Existing SMTP settings, editable templates, durable queue and delivery retries apply. Disabled templates suppress their email; GUI warnings remain. Failed preparation is visible in Email queue. Revoked clocks are excluded. No extra employee setting or per-clock timing setting is added.

Overview and manager Attendance/Pay periods/email pages flag incomplete clock data. Clock management keeps pending/recording warnings after reconnect until healthy and drained. Upload exceptions include rejected and out-of-order swipes retained for manual reconciliation. Recent out-of-order issues are highlighted for seven days; exception records remain available. No timestamps, OUTs or manager corrections are manufactured.

The Primary cannot email about its own power loss, stopped server or loss of all outbound mail connectivity. SMTP acceptance is not proof that a manager read an email. Heartbeat measures agent reporting, not an independent electrical test of the reader. A reader failure that terminates the agent becomes a missed-heartbeat outage. Storage/time errors with a functioning heartbeat appear in GUI health; only connectivity outage/recovery automatically emails.

## Offline operation

Previously cached eligible cards can record locally using actual synchronized timestamps. Unknown/new uncached cards cannot. Remote revocations and attendance-mode changes do not reach an offline cache. Lost time synchronization blocks attendance. Failed local saves produce SAVE FAILED and refusal feedback. Unclassified saved swipes retain SAVED / CONFIRMING feedback; IN/OUT classification comes from Primary.

Queue writes use SQLite WAL/FULL. Only the expected durable acceptance response removes a pending swipe. Retryable transport/auth/server errors retain it; permanent rejections are retained separately. SD failure before upload can still lose the only copy. Cross-clock use during an outage may deliver an older event after a newer event: out-of-order rows remain excluded from effective attendance and need an audited manager correction. The clock does not silently rebase reviewed attendance.

## Local backups and recovery

Only adopted Slaves enable the local backup timer, at 02:15 plus up to five minutes randomized delay. Missed runs are skipped, with no backup catch-up at boot. The Slave backup service invokes the backup helper with agent-only service control and no email option. It uses SQLite online backup to capture committed queue data while the agent keeps scanning. Live WAL/SHM files are excluded from the archive; the snapshot is self-contained. The reader is not stopped for a Slave backup. Backups remain in /var/backups/timeclock on that device, with existing retention of 30 archives. There is no backup synchronization, remote copy, replication or failover.

Manual backup: sudo /opt/icubedev-timeclock/bin/timeclock-backup backup
List: sudo /opt/icubedev-timeclock/bin/timeclock-backup list
Verify: sudo /opt/icubedev-timeclock/bin/timeclock-backup verify /var/backups/timeclock/ARCHIVE.tar.gz
Restore on the same configured Slave: sudo /opt/icubedev-timeclock/bin/timeclock-backup restore /var/backups/timeclock/ARCHIVE.tar.gz --yes

Archives contain queue/cache/rejection data and copies of /etc/timeclock/timeclock.env, adopted and role-selected under data/slave-recovery. Slave archives and checksum files are root-only (0600). They include the device key in timeclock.env: treat them as credentials when copying or handling them. Existing adoption credentials are not automatically replaced during data restore. Restore refuses live unsent/unreported rejected records, cross-role archives, or queued punches belonging to another clock ID. It starts only the Slave agent and verifies the restored queue/service; a data restore does not restore OS packages, firmware, system trust store or the entire SD card.

For replacement hardware, keep the old clock powered off. Restore the matching recovery configuration deliberately as root before attempting data recovery, reinstall any required CA trust and confirm the Primary still recognizes the credential. Never run two clocks with the same identity. If the credential was revoked, use manager-controlled adoption and reconcile retained punches rather than editing queue clock IDs. Local backups on a failed SD card may also be lost.

## Deployment acceptance

Check physical card/OLED/buzzer operation; adopt a fresh Slave; update an adopted Slave with pending data; disconnect network while scanning cached cards; confirm the manager warning after threshold; reconnect and verify exact uploaded timestamps and backlog drain; test a cross-clock out-of-order case; and create/verify a real local backup. Container tests do not replace these installed hardware checks.

## Local communications reminder

After three minutes without successful Primary communication, a Slave chirps once for 40 ms, then once every five minutes while disconnected. Timing is monotonic; NTP clock changes cannot trigger it. Swipe feedback, active confirmation screens and recent card presentations take priority, so a due reminder waits for a quiet moment. It never blocks attendance. Recovery stops the reminder and starts a fresh three-minute grace for a future outage. Existing BUZZER_OFFSET=none disables sound. Primary agents do not chirp for this condition.

The idle screen says OFFLINE and shows saved swipes. The Primary alone holds SMTP credentials and sends manager outage/recovery emails; Slaves have no mailer. Local warning needs neither SMTP nor a functioning Primary. Real audibility depends on the fitted active buzzer; volume cannot be set in software.

## Shared OLED appearance

In Settings > Clock display, select the Primary to set the font and 12/24-hour time format for all clocks. A Slave cannot override these choices. Brightness schedules remain per clock. Online agents fetch settings every 30 seconds; font loading may take another five seconds. The last received font and format are retained locally for offline use, including after a restart. Update both Primary and Slave software for 24-hour support. This affects idle time and IN/OUT confirmation time, not recorded UTC timestamps or report calculations.

### Slave address and software version

Clock management shows each Slave's IP address and software version beside its status. The authenticated 30-second health heartbeat reports the local IPv4 or IPv6 address selected by the route to the configured Primary, plus the running agent build version. It is a reported local address, not the public NAT address, and need not be reachable from the manager's browser. Changes appear after the next successful heartbeat and GUI refresh (every 15 seconds, or Refresh).

The Primary stores the last received values. Offline and revoked clocks retain them, with their existing contact/status indicators. Missing values display “Not reported”. An older agent does not supply these values; update both the Primary and Slaves to RC6.1. The Primary's own row does not receive Slave metadata. Address lookup and reporting run in the agent's network thread, independently of RFID input and OLED preview.
