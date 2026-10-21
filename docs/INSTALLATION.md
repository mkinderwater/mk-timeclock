# Installation and roles

Extract the complete source package onto the target clock. Run `python3 rc1-preflight.py` before installation. The maintained role wrappers call the same installer:

- Primary: `sudo ./install-master.sh`
- Slave: `sudo ./install-slave.sh`

These are the entrypoints present in the accepted benchmark. The cleanup does not add a role picker or authority transfer. Do not run both wrappers on the same device. The installer changes packages, services, nginx and device access, preserves configured data, and does not automatically reset SQLite.

The Primary hosts manager administration, the authoritative attendance database, mailer and server timers. Initial manager creation uses HTTPS `/setup` with a manager card at the clock. The emergency `timeclock-add-manager.sh` utility remains available locally to root; the historical general-purpose `mk-timeclock` CLI is not shipped.

A Slave retains its own offline queue and uses the existing Primary adoption/approval workflow. Local backups remain clock-based; backups are not replication or failover.

`clock.env.example` documents device settings. Supported Banana Pi wiring, RC522, SSD1309 OLED and buzzer behavior are unchanged. The active buzzer uses GPIO offset 6 (PA6, physical pin 7 on the supported image) and GND on physical pin 6. Use the existing driver/voltage arrangement. The boot logo appears first for about two seconds, then the idle clock.

Use `verify-stage.sh` and `validate-stability.sh` for non-destructive on-device observations. Neither a development build nor HTTP tests establish hardware acceptance. Do not deploy to a live clock as part of source verification.

Cleanup4 corrects final TLS directory permissions during upgrades. The backend requires traversal of `/etc/timeclock/tls` (`root:timeclock`, `0710`) to see status markers. Certificate-version directories remain `0700` and keys `0600`; the backend does not need access to them. The earlier root-only final directory setup hid the status markers without stopping nginx from using the certificate. No re-upload or database reset is needed.
