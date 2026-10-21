# Backup management, v0.9.107

Each device backs up its own `/var/lib/timeclock` to `/var/backups/timeclock`. There is **no backup sync between clocks**.

## GUI

- **Backup now** opens the existing system-action flow and requires a registered manager card, then invokes the privileged local backup command. Expect brief service disruption.
- **Scheduled** daily backup via `timeclock-backup.timer` at 02:15 local time with up to 5 minutes of randomized delay. The GUI can opt into sending the archive as an email attachment to a selected active manager with an email address. This uses the existing configured SMTP server, honours its encryption setting (None, STARTTLS or SSL/TLS), and refuses files above 15 MiB. Local backup still succeeds if delivery fails; review `journalctl -u timeclock-backup.service`. Email is opt-in and disabled by default.

SMTP encryption **None** is supported only across a trusted network or VPN.
- **Download** lists archives in the local directory. Click Download, present **the signed-in manager's own** registered card within 30 seconds. A one-use token expires in 60 seconds. Download is audited by manager ID, filename and time. No archive is exposed through a public web directory.
- **Restore** is available for local archives in the manager GUI, with fresh manager RFID approval and explicit RESTORE confirmation. It is full-data restoration only, not settings-only. The helper validates the archive, refuses unresolved offline punches, preserves pre-restore data, checks service health and records restore outcome. Test on a nonproduction device before live use.

Archive contains confidential personnel, credential and card data. **Emailing archives transfers a copy to the selected manager's mailbox**; select a trusted mailbox with MFA and adequate retention controls. There is no encryption at rest provided by this release. SMTP TLS protects the transmission, not the destination mailbox.

## Local CLI

```
sudo timeclock-backup backup
sudo timeclock-backup list
sudo timeclock-backup verify /var/backups/timeclock/timeclock-EXAMPLE.tar.gz
sudo timeclock-backup restore /var/backups/timeclock/timeclock-EXAMPLE.tar.gz --yes
```

Restore may lose newer punches; export and reconcile offline queues before restoring. A pre-restore local copy is preserved. SHA256 detects corruption rather than intentional tampering.

## Installation

This is a source release; compile all three binaries on the target with `make all` before running `sudo ./install.sh`. The agent requires FreeType development headers. Do not use binaries from an earlier release.
