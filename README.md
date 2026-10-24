# mk-timeclock Lite

RFID attendance recording, employee vacation requests, and payroll reporting for the **Banana Pi BPI-M2 Zero**.

Developed by **iCube Development (Calgary) Ltd.** The clock combines an RC522 reader, a 128 × 64 OLED, audible swipe feedback, and a browser-based manager interface. A single device can operate as the Primary, or additional Secondary clocks can send attendance to it.

**Attendance rules:** IN starts recorded elapsed time; OUT ends it. Missing punches are flagged. The application does not invent an OUT, deduct breaks, calculate overtime pay, or decide which hours are payable.

This README describes **1.9.5-RC3-cleanup27**. The release contains source code and installation scripts, not an operating-system image.

## Contents

- [Features](#features)
- [Hardware and operating system](#hardware-and-operating-system)
- [Pinouts and wiring](#pinouts-and-wiring)
- [Installation](#installation)
- [First setup](#first-setup)
- [Additional clocks](#additional-clocks)
- [Payroll and vacation reporting](#payroll-and-vacation-reporting)
- [Backups and recovery](#backups-and-recovery)
- [Configuration and services](#configuration-and-services)
- [Troubleshooting](#troubleshooting)
- [Development and verification](#development-and-verification)
- [Further documentation](#further-documentation)

## Features

### Attendance and employees

- RFID attendance using RC522 cards and fobs, with employee names and IN/OUT feedback on the OLED.
- Different audible feedback for accepted and rejected scans, without blocking the reader loop.
- Persistent local swipe queue and cached employee cards for network interruptions. Accepted queued attendance is uploaded when connectivity returns.
- Employee creation, multiple attendance cards, lost-card replacement, and card activation/deactivation.
- Separate manager cards for protected changes. Manager-card scans do not record attendance.
- Recurring weekly schedules, dated shifts, and a no-schedule option.
- Per-clock late grace and repeat-swipe settings, with late-arrival and early-departure notices and private explanation links.
- Manager corrections, recorded absences, and audit history.
- Punch-required and vacation-only employee modes.

### Employee vacation

- Private timesheet links for viewing recorded shifts and leave, approving a timesheet, or reporting a concern.
- Private vacation links with manager approval, denial, cancellation, and email notifications.
- Manager entry of vacation approved verbally or by email, including past vacation before installation, without entry emails.
- Vacation forms contain dates, calculated working days, return date and optional notes. Other absences are recorded separately.
- Optional annual vacation allowance, including zero and half-day allowance values.
- **X of Y days used** on the time-off form and reports. Requests can exceed the allowance; there is no accrual or balance enforcement.
- Annual vacation CSV export and manual or CSV holiday entry.

### Reporting and administration

- Weekly, biweekly, twice-monthly, and monthly pay-period schedules.
- Closed-period exports with preserved CSV revisions and revised-report delivery.
- Excel workbooks with **Payroll Summary** and **Daily Detail** tabs.
- Employee ID columns, employee-grouped detail, alternating block shading, AutoFilter, and Summary links to each employee's detail.
- Independent local send times for payroll reports and timesheet links, including zero-day offsets.
- SMTP sender name/address, editable email templates, delivery queue, and retry status.
- HTTPS certificate installation, time synchronization, local timezone selection, display settings, clock adoption, system health, and diagnostics.
- Local backup/download/restore workflows and optional scheduled backup email.

**Touch input is not implemented in this release.** A TTP223 module is not required and has no assigned connection in the current agent.

## Build and install overview

Use the **Timeclock Build and Install Guide** for the illustrated assembly sequence. This README provides the matching wiring reference, software commands, features, and maintenance details.

1. **Build:** print case v8.34, solder right-angle headers, convert the OLED to I²C, mount the components, then connect and check all 14 wires with power disconnected.
2. **Software:** write the bpi-zero-wbuild 3.13 image, configure Wi-Fi and SSH access, start the clock, copy the software, then install the chosen role.
3. **Finish:** create the first manager, test the display and cards, close the case, and mount it.

**Primary** is the first or only clock and hosts the database and management website. **Secondary** means an additional reader clock. The source, service messages, and installer filename use the term `slave` for this role.

## Hardware and operating system

| Component | Project setup |
| --- | --- |
| Computer | Banana Pi BPI-M2 Zero |
| Operating system | Project-prepared 32-bit Debian/Armbian-based image with systemd and apt |
| Reader | RC522 / MFRC522 SPI module; compatible 13.56 MHz cards or fobs |
| Display | 2.42-inch SSD1309 OLED, 128 × 64, seven-pin PCB converted to I²C as described below |
| Audible feedback | Supported active buzzer or a 3.3 V-compatible buzzer driver |
| Storage | microSD with space for SQLite data, exports, and backups |
| Power | Stable board power supply; the manufacturer's board specification lists 5 V / 2 A |
| Network | Network access for management, time synchronization, SMTP, and Primary/Secondary communication |

The installer currently requires:

- Debian architecture `armhf` and a 32-bit ARM machine type.
- The prepared-image marker `/etc/bpi-zero-wbuild-release`.
- `/dev/spidev0.0`, `/dev/i2c-0`, and `/dev/gpiochip0`.
- Linux GPIO character-device v2 support, used by the buzzer code.

A generic Debian or Armbian image is not automatically a supported installation. Enable the required interfaces using the image's configuration tools before installation. Do not create the marker file merely to bypass the platform check.

### Case and assembly

The illustrated build uses **case v8.34**, right-angle 2.54 mm headers, 14 female-to-female jumper wires, a stick-on IPEX/u.FL Wi-Fi antenna, a two-wire active buzzer with mounting ears, and a USB-C power breakout.

| Fasteners | Quantity | Component |
| --- | ---: | --- |
| M2 × 5 mm self-tapping | 8 | OLED and RC522, four each |
| M2 × 6 mm self-tapping | 6 | BPI, four; USB-C board, two |
| M2 × 8 mm self-tapping | 2 | Buzzer ears |
| M3 × 12 mm countersunk self-tapping | 4 | Rear cover |

Print the OLED fit-test piece before the front and rear cover. The guide specifies a 0.4 mm nozzle, 0.24 mm layers, three walls, 20% infill, no supports, and PETG or PLA. Keep the supplied print orientation. Tighten self-tapping screws gently.

Use right-angle headers so the cover can close. The OLED header points inward over the PCB. Mount its glass from inside using the four internal M2 × 5 mm fasteners. Keep wires clear of the display ribbon and cover rim. Leave the cover off until testing is complete.

## Pinouts and wiring

**Numbers below are physical positions on the BPI-M2 Zero's 40-pin header, not Raspberry Pi BCM GPIO numbers.** Check the board's pin-1 marking before connecting anything. Disconnect power while wiring and use a common ground.

Board mapping reference: [Banana Pi BPI-M2 Zero manufacturer pinout](https://wiki.banana-pi.org/Banana_Pi_BPI-M2_ZERO#GPIO_PIN_define).

### RC522 reader: SPI0

| RC522 label | Physical pin | BPI signal |
| --- | ---: | --- |
| 3.3V | 17 | 3.3 V |
| GND | 20 | Ground |
| SDA / SS / NSS | 24 | PC3, SPI0 chip select |
| SCK | 23 | PC2, SPI0 clock |
| MOSI | 19 | PC0, SPI0 MOSI |
| MISO | 21 | PC1, SPI0 MISO |
| RST | Not connected | As assembled in the illustrated build |
| IRQ | Not connected | Unused |

**The RC522 pin labelled SDA is required. In SPI mode it is chip select, not I²C SDA.** Connect it to pin 24, not pin 3.

The illustrated build leaves both RC522 IRQ and RST unconnected. The driver uses `/dev/spidev0.0` at 250 kHz, performs reader reset over SPI, and does not drive a reset GPIO. The module must hold its reset/power-down input high through its onboard circuitry; an unconnected terminal is not a general instruction to leave the chip input floating. If using a different module, verify its reset circuit. Use 3.3 V power and logic. See the [NXP MFRC522 datasheet](https://www.nxp.com/docs/en/data-sheet/MFRC522.pdf) for electrical and reset details.

### OLED: I²C0

| OLED label | Physical pin | BPI signal |
| --- | ---: | --- |
| VCC | 1 | 3.3 V, for a compatible module |
| GND | 9 | Ground |
| SDA | 3 | PA12, I²C0 data |
| SCL / SCK | 5 | PA11, I²C0 clock |
| RES / RST | Not connected | No external reset wire was fitted |
| DC | Not connected | Configured by the onboard I²C bridges |
| CS | Not connected | Configured by the onboard I²C bridges |

For the project's 2.42-inch SSD1309 PCB, convert SPI to I²C as follows:

| PCB location | Modification |
| --- | --- |
| R8 | Remove the 0 Ω resistor; leave these pads open |
| R9 | Fit the removed resistor here, or solder-bridge the pads |
| R10 | Solder-bridge the pads |
| R11 | Solder-bridge the pads |
| R12 | Solder-bridge the pads |

These component labels apply to this particular OLED PCB, not every SSD1309 module. The converted display was detected at `0x3c` on `/dev/i2c-0`.

**The OLED RES/RST connection was considered but never fitted. Leave it unconnected in this build; do not wire it to 3.3 V or a GPIO.** The illustrated build also leaves the separate RC522 RST terminal unconnected, as described above.

Default device: `/dev/i2c-0`. Default address: `0x3c`. Confirm the module's controller, supply requirements, connector order, and address before connection; OLED modules do not all have the same pin order.

### Buzzer

| Connection | Physical pin | Setting |
| --- | ---: | --- |
| Supported control input / driver input | 7 | PA6, `/dev/gpiochip0` line offset `6` on the supported image |
| Ground | 6 | Common ground |

Use an active buzzer arrangement compatible with the GPIO, or a suitable transistor driver with the buzzer powered according to its rating. Do not use the GPIO as a general-purpose buzzer power supply. Set `BUZZER_OFFSET=none` to disable audible feedback.

### USB-C power input

| USB-C breakout label | Physical pin | BPI signal |
| --- | ---: | --- |
| V / VBUS | 2 | 5 V input |
| G / GND | 39 | Ground |
| D+ and D− | Not connected | Data terminals unused |

The guide uses a **5 V, 2 A or greater USB-A charger and USB-A-to-USB-C cable**. The breakout supplies power only. Do not use a higher-voltage PD trigger. A USB-C-to-C supply requires a breakout with the appropriate CC pull-down resistors.

### Wiring checklist

The illustrated build uses **14 jumper wires**: six RC522, four OLED, two buzzer, and two USB-C power wires. No Y-splice is fitted. OLED RES/DC/CS and RC522 IRQ/RST remain unconnected.

Before applying power, check pin 2 goes only to USB-C V, RC522 power goes to pin 17, OLED power goes to pin 1, and buzzer red/positive goes to pin 7 with black/negative on pin 6. This direct buzzer wiring applies to the compatible buzzer used in the guide. Check every plug is fully seated on one pin, with no shorts or pinched wires.

### Verify interfaces

Before installing:

```bash
ls -l /dev/spidev0.0 /dev/i2c-0 /dev/gpiochip0
uname -m
dpkg --print-architecture
```

Bus numbers and GPIO line offsets can differ on another image. Header position, Allwinner signal name, and Linux line offset are different identifiers.

## Installation

### 1. Prepare the clock

Use the prepared [bpi-zero-wbuild 3.13 image](https://github.com/mkinderwater/bpi-zero-wbuild/releases/tag/3.13), `bpi-zero-wbuild-3.13-trixie-minimal-hw-bpi-m2-zero.img.gz`. Write it directly to a microSD card of at least 16 GB with balenaEtcher. Writing the image erases the selected card. Cancel any Windows prompt to format it afterward.

Reinsert the card and open `CONFIG.TXT` on the `BPIWBUILD` volume. Set the six configuration lines using your own values:

```ini
SSID=OfficeWiFi
PSK=wifi-password-here
COUNTRY=CA
HIDDEN=false
TIMEZONE=America/Edmonton
ROOT_PASSWORD=Choose-A-Strong-Password-26
```

Use a 2.4 GHz Wi-Fi network. Follow the image's configuration notes for allowed values. Keep the root password safe; the image removes it from the configuration file after setup. Eject the card, insert it into the BPI, and apply power only after checking the wiring. Allow about ten minutes for first boot. The OLED remains blank until the timeclock software runs.

Find the address in the router's device list and reserve it. Connect from Terminal or PowerShell:

```bash
ssh root@192.168.1.50
cat /etc/bpi-zero-wbuild-release
```

Replace the example IP with the clock's address. The image marker should identify `3.13-trixie-minimal-hw`. The SSH root password is separate from the manager account created later.

Copy `mk-timeclock-lite-RC3-cleanup6.zip` to `/root` using SFTP/WinSCP or `scp`, then run on the clock:

```bash
sudo apt-get update
sudo apt-get install -y unzip python3
cd /root
unzip mk-timeclock-lite-RC3-cleanup6.zip
cd mk-timeclock-lite-RC3-cleanup6
```

These commands match the guide's cleanup6 source archive. If using a different release, use its actual archive and extracted directory names. The prepared image must expose the SPI, I²C, and GPIO devices listed above.

Run commands from the source directory. If an archive or download has lost executable permissions:

```bash
chmod +x ./*.sh
```

### 2. Check the source package

```bash
python3 rc1-preflight.py
```

This checks that referenced installation files are present. It does not verify hardware or install dependencies.

### 3. Install the Primary

For the main clock that hosts the management application and attendance database:

```bash
sudo ./install-master.sh
```

The installer checks the platform, installs missing dependencies, builds the three programs on the clock, configures services, and creates initial HTTPS credentials.

The Primary and Slave wrappers use the same shared installer. **Bare `sudo ./install.sh` does not install anything:** it requires a role argument. The explicit equivalents are `sudo ./install.sh --master` and `sudo ./install.sh --slave`.

Do not run both role installers on one device. The installer refuses conflicting existing roles and refuses a Secondary installation over an existing Primary database.

### 4. Open the management page

Visit:

```text
https://<clock-ip>/setup
```

A fresh installation uses a temporary self-signed certificate, so the browser initially shows a certificate warning. Replace it with a trusted certificate under **Settings → HTTPS certificate** before distributing employee links.

## First setup

1. Create the first manager with first name, last name, position, email, and a password of at least 12 characters.
2. Scan an unused RFID card at the clock to finish setup. Keep this card for manager authorization rather than attendance.
3. Sign in at `https://<clock-ip>/login` using the manager's email address.
4. Open **Time sync**, choose the local timezone, and confirm the clock is synchronized. The agent refuses attendance scans while the system clock is unsynchronized.
5. Install the trusted HTTPS certificate and intermediate chain, if required. The hostname must match the certificate.
6. Configure **Outgoing email**, including sender name/address and connection security. Queue a test and check **Email queue** for delivery status.
7. Add employees, attendance cards, schedules, and vacation allowances.
8. Set the pay frequency under **Pay periods**, then configure **Payroll report** and **Timesheet links** day/time settings.
9. Enter holidays manually or import a CSV, review the staged rows, and click **Save holidays**.
10. Test a real IN/OUT pair, employee links, notifications, an export, and backup/restore before relying on the clock.

Holiday import format:

```csv
Date,Holiday
2026-12-25,Christmas Day
2027-01-01,New Year's Day
```

Use `YYYY-MM-DD` dates and simple holiday names without commas. Import stages changes for review; selecting a file does not save them.

## Additional clocks

The **Primary** owns the authoritative attendance database and hosts manager administration. Each **Secondary** reads cards, maintains its local queue, and uploads attendance to the Primary.

Install an Secondary clock with:

```bash
sudo ./install-slave.sh
```

For an unadopted Secondary:

1. Install a trusted HTTPS certificate on the Primary.
2. Generate an adoption code under the Primary's **Clock management** page.
3. On a trusted local network, visit `http://<slave-ip>:8090/`.
4. Enter the Primary's HTTPS address and the adoption code, then send the request.
5. Approve the pending request on the Primary using its manager authorization workflow.
6. Check approval on the Slave to complete adoption and start attendance recording.

Keep device credentials private and unique. A Secondary is not an automatic failover Primary, and its queue is not a replica of the full payroll database. Backups are local to each clock; there is no backup synchronization between clocks.

### Test and finish assembly

Before closing the case, confirm the display shows the date, time, and card prompt. Test the manager card for manager feedback and the unregistered kit card for rejection. If the clock reports **CLOCK NOT SYNCED**, resolve time synchronization before testing attendance.

Unplug power, check that no wire is trapped, then fit the rear cover with four M3 × 12 mm countersunk screws. For wall mounting, use the guide's drilling template at **100% / Actual size** and verify its 80 mm reference with a ruler. Keep the clock away from large metal surfaces. Reconnect power and confirm the card prompt returns.

## Payroll and vacation reporting

A completed shift belongs entirely to the pay period containing its IN date in the clock’s local timezone. An OUT after midnight or after the period ends completes the original shift; hours are not split and no midnight punches are added. For example, September 15 at 22:00 through September 16 at 06:00 contributes all eight hours to September 1–15 and none to September 16–30. Elapsed duration uses the actual timestamps, including daylight-saving changes. A missing OUT remains an exception; the system does not invent an end time.

### Recorded time and revisions

IN/OUT pairs produce recorded elapsed hours. Schedule expectations, holiday context, and leave records do not manufacture worked hours. Managers resolve missing punches and review exceptions.

Close a pay period before sending its payroll report. Late uploads and manager corrections can still affect closed periods; the manager interface identifies changes requiring another export. A new export creates a revision rather than overwriting the earlier CSV.

Workbooks are generated from the selected saved CSV revision. A converter update can change workbook presentation, but saved source contents remain preserved. Email attachments use the saved revision, including when a revised send waits for an earlier queued email.

### Workbook layout

| Tab | Contents |
| --- | --- |
| Payroll Summary | One employee summary, worked hours, period leave counts, exceptions, annual vacation usage/allowance, Employee ID, and links into Daily Detail |
| Daily Detail | Source records grouped by employee name and ID, then date and IN time; undated manager records finish that employee's block; alternating shading and AutoFilter |

Daily Detail has no employee subtotal rows or blank separators. Totals remain on Payroll Summary.

Summary hours add the CSV's rounded per-shift hours. Text email period totals use seconds and may differ slightly; the workbook states that distinction.

### Vacation allowance

An employee's annual allowance is optional. Blank means unspecified; zero is an explicit zero allowance. Allowance values support whole or half days.

**X of Y days used** counts distinct approved vacation working dates in the selected calendar year, including future approved bookings. Holidays, pending/denied requests, and cancelled approvals are excluded. Overlapping approved dates count once. Counting follows the existing schedule calendar, with a weekday estimate where applicable.

An employee can request more than the allowance, including when it is zero. There is no accrual, balance enforcement, or automatic request rejection based on the allowance.

Saved payroll revisions retain the annual figures captured at export. The annual vacation CSV uses current settings, so it can change after schedule, holiday, allowance, or approval edits.

### Vacation workflow

Employees use their private link to **Request vacation**: starting date, ending date, suggested return date and optional notes. One-day bookings default the end date to the start date. A blank return date uses the server suggestion. Working days are calculated automatically from current schedules and holidays. There is no leave-type selector or editable day total. The manager approves or denies the request on **Vacation**, and the existing email notifications report the request and decision.

The manager Vacation page starts with **Waiting for approval**. **Vacation history** is split into **Upcoming** (including ongoing leave, earliest start first) and **Past** (most recent start first). Employee and year filters apply to history; waiting requests stay visible independently. Each list has 50-row pages, so older records beyond the previous 300-row cap remain accessible. Year filters include vacation overlapping the selected year.

Managers click **Record vacation** to open a dialog for verbal/email approvals or vacation taken before installation. Select an employee, including an inactive employee if recording historical vacation, then enter dates and optional approval details. The end date defaults to the start date for one-day bookings. Return to work is suggested automatically; accept it as-is, change it, or leave it blank to use the same server suggestion. Past and future dates are accepted. The entry saves directly as Approved, identifies the manager and recording time, and sends no entry email.

Approved vacation, including future bookings, counts toward **X of Y days used**. The allowance is informational and does not block entries. Overlapping pending or approved records are rejected. Cancel an incorrect approval and record a replacement; cancellation retains its existing notification rules. Existing saved revisions remain unchanged, and affected exports are flagged for a new revision.

When vacation spans configured stat holidays, forms and request history show a breakdown such as **4 vacation days + 1 stat holiday**, with each holiday's name and date. Stats never count toward vacation usage. Employee timesheets show the stat once, with **Not counted as vacation**, rather than an additional vacation row. Payroll retains **Stat holiday (not vacation)**.

Stat-only requests are rejected with the holiday date and name. Request history uses the current calendar but retains the originally requested day count; if it differs, the history explains that the calendar changed. Configured stat dates are listed separately even when they fall on an employee's non-working day. These are calendar records, not a calculation of holiday-pay entitlement. Changing holidays updates current counts and flags affected exports for revision without rewriting saved revisions.

For sickness or another missed-attendance explanation, use **Punches and corrections → Recorded absences**. New personal/unpaid/other leave requests are no longer accepted through the vacation form or API. Earlier non-vacation records remain in history and keep their original type. Existing links and internal route names remain valid.

### Send times

| Setting | Meaning of zero days | Time control |
| --- | --- | --- |
| Payroll report | On payday | Independent local send time |
| Timesheet links | On the period-end day | Independent local send time |

Use the day offsets to place both on a common processing date, then choose, for example, **08:00 for links** and **15:00 for payroll**. The report still requires closure before it can queue.

Times use the clock's local timezone and default to `00:00` until changed. Automatic jobs queue at or after their cutoff. Existing catch-up windows and duplicate protection apply after restarts and daylight-saving changes. Manual **Send report** and **Create and email** remain immediate.

SMTP **None** is supported only across a trusted network or VPN. STARTTLS and SSL/TLS remain available. An email marked sent means the SMTP server accepted it, not that the recipient read it.

## Backups and recovery

Use **Backup and restore** for local backup creation, authorized downloads, and full-data restoration.

- Primary installation enables a daily backup timer at **02:15 local time**, with up to five minutes of randomized delay.
- Optional scheduled email sends the archive to a selected active manager. SMTP None, STARTTLS, and SSL/TLS use the shared outgoing-mail configuration.
- Backup email is disabled by default and rejects archives above **15 MiB**. A mail failure does not invalidate the local archive.
- Manager GUI download and restore actions require the applicable manager-card authorization. Restore also requires explicit confirmation.
- A Secondary installation does not automatically enable the Primary's backup timer.

Local commands:

```bash
sudo /opt/icubedev-timeclock/bin/timeclock-backup backup
sudo /opt/icubedev-timeclock/bin/timeclock-backup list
sudo /opt/icubedev-timeclock/bin/timeclock-backup verify /var/backups/timeclock/timeclock-EXAMPLE.tar.gz
```

Archives contain sensitive personnel, credential, and card data and are not encrypted at rest by this release. Protect stored copies and destination mailboxes. Back up before upgrades, and test restoration on a separate device before using it on a live clock.

### Upgrade

Extract the complete new source, run preflight, and rerun the installer for the device's **existing role**. Current RC3 upgrades preserve configuration and data; a database reset is not required. Arbitrary historical schema migrations are not supported.

Uninstalling is not part of the normal upgrade procedure. `sudo ./uninstall.sh` removes program files and selected service/proxy configuration while preserving `/var/lib/timeclock` and `/etc/timeclock`. It is not a password reset or factory reset. Do not delete those directories to fix a login issue.

## Configuration and services

Hardware and device configuration is stored in `/etc/timeclock/timeclock.env`. The installer creates real credentials; [clock.env.example](clock.env.example) is a reference, not a file to copy over existing credentials.

| Setting | Typical value |
| --- | --- |
| `TIMECLOCK_ROLE` | `primary` or `slave` |
| `CLOCK_ID` | Unique clock identifier |
| `SERVER_URL` | Primary: `http://127.0.0.1:8080`; Slave: trusted Primary HTTPS URL |
| `DEVICE_KEY` | Unique private credential generated/provisioned for the clock |
| `SPI_DEV` | `/dev/spidev0.0` |
| `GPIOCHIP` | `/dev/gpiochip0` |
| `BUZZER_OFFSET` | `6`, or `none` to disable |
| `I2C_DEV` | `/dev/i2c-0` |
| `I2C_ADDR` | `0x3c` |
| `TTF_FONT` | `/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf` |

Attendance, schedules, email, reports, and most manager-facing settings are managed through the GUI. Copying an example environment file is not a substitute for configuring those settings.

| Path | Purpose |
| --- | --- |
| `/opt/icubedev-timeclock/bin` | Installed programs and helpers |
| `/var/lib/timeclock/server.db` | Primary database |
| `/var/lib/timeclock/queue.db` | Local reader queue/cache |
| `/etc/timeclock` | Device configuration and TLS material |
| `/var/backups/timeclock` | Local backup archives |

| Service / listener | Purpose |
| --- | --- |
| `timeclock-server.service` | Primary web backend, loopback port 8080 by default |
| `timeclock-agent.service` | Reader, OLED, buzzer, and local queue |
| `timeclock-mailer.service` | Primary email queue and payroll dispatch |
| `nginx.service` | HTTPS entry point on port 443 |
| `mk-timeclock-setup.service` | Temporary unadopted-Slave setup on port 8090 |
| `timeclock-backup.timer` | Scheduled local backups |
| `timeclock-attendance-reminders.timer` | Attendance reminder checks |
| `timeclock-missing-digest.timer` | Missing-punch digest checks |

Role determines which services run. Keep the loopback backend private and use trusted HTTPS for remote clocks and employee links.

## Troubleshooting

| Symptom | Check |
| --- | --- |
| Installer exits immediately | Use the Primary/Secondary wrapper; check image marker, architecture, and device nodes |
| Cannot reach the GUI | Check the clock IP, nginx, and server service logs |
| Login rejected after reinstall | Existing accounts/passwords are preserved; reinstalling does not reset them |
| Card scans refused with time warning | Check NTP synchronization and network access; changing timezone does not synchronize the clock |
| Reader fails verification | Check 3.3 V power, common ground, SPI wiring, and especially SDA/SS to pin 24; some clone revisions are deliberately rejected |
| OLED blank | Check module pin order, 3.3 V compatibility, I²C address, and device node |
| No buzzer | Check `BUZZER_OFFSET`, PA6 mapping, active-buzzer compatibility, and driver wiring |
| Certificate works in browser but GUI says none | On affected older installs, restore TLS status-directory permissions as shown below; cleanup4 and later fix the installer |
| Email remains pending | Check SMTP configuration, recipient, template, queue error, and mailer logs |
| Annual vacation figures seem high | Future approved bookings count toward the selected year; the displayed allowance does not restrict requests |
| Pay-period page times out | Measure with representative data on the Pi; the serial backend rebuilds historical reports and page requests have deadlines |

Useful checks:

```bash
sudo systemctl status timeclock-server.service timeclock-agent.service timeclock-mailer.service nginx.service
sudo journalctl -u timeclock-agent.service -n 50 --no-pager
sudo journalctl -u timeclock-server.service -n 50 --no-pager
sudo journalctl -u timeclock-mailer.service -n 50 --no-pager
curl --max-time 5 http://127.0.0.1:8080/health
timedatectl show -p NTPSynchronized
```

TLS status repair for an affected existing installation:

```bash
sudo chown root:timeclock /etc/timeclock/tls
sudo chmod 0710 /etc/timeclock/tls
```

Refresh the certificate page afterward. This does not replace the installed certificate or expose the root-only private-key directories.

A root-only local account utility is available at `/opt/icubedev-timeclock/bin/timeclock-add-manager.sh`. Run without arguments to create an initial manager when none exists, or use `--reset-password` followed by the manager's employee ID for password recovery. After local provisioning, register a manager card through the Manager cards page.

## Development and verification

The programs are C with SQLite, libcurl, libcrypt, Linux device interfaces, and FreeType. Supporting tools use Python 3 and shell. The XLSX converter uses Python's standard library.

The target installer installs the build dependencies. With those dependencies available:

```bash
make all
make lint
python3 tests/run_current.py
```

After editing the stylesheet, regenerate the embedded UI with a new cache version:

```bash
make ui UI_VERSION=your-new-css-version
```

Browser checks require Playwright and Chromium. Set `PLAYWRIGHT_MODULE` and `CHROMIUM_PATH` to their installed locations, then run:

```bash
python3 tests/run_current.py --browser
```

The maintained suite is listed in `tests/current_suite.json`. Historical tests remain for coverage review and can carry obsolete assumptions; unrestricted test discovery is not the current acceptance contract.

Cleanup27 passes **54/54 maintained non-browser checks**, including deferred corrected/manual lunch departures, preserved paired OUT flags, consecutive overnight arrivals and overdue upload settling. Browser checks were not run for this release. Cleanup21 previously passed 56/56 including browsers. Blank-ID reservation and historical approval regressions remain maintained. Cleanup13 corrected the timezone-dependent overnight test fixtures. Both boundary tests pass in Calgary (America/Edmonton), UTC and Amsterdam time without application logic changes; see [cleanup13 release notes](RELEASE_NOTES_cleanup13.md). Vacation workflow, notifications, history retention and export checks pass. The historical cleanup4 run passed 44/44. Development checks do not establish physical RFID, OLED, buzzer, on-device performance, power-loss, or installed restore acceptance. See [Testing](docs/TESTING.md) for the remaining device checks.

Known limits include a serial web backend, report rebuilding on historical period lists, and UID-based cards that do not distinguish physical clones. Review payroll output and complete a device acceptance check before production use.

## Further documentation

- [Installation and roles](docs/INSTALLATION.md)
- [Manager operations, schedules, and email](docs/OPERATIONS.md)
- [Attendance and payroll rules](docs/ATTENDANCE_PAYROLL.md)
- [Backup and restore](docs/BACKUP_RESTORE.md)
- [SQLite durability](docs/SQLITE_DURABILITY.md)
- [Schema ownership](docs/SCHEMA.md)
- [Testing and current limitations](docs/TESTING.md)
- [Cleanup history and validation notes](CLEANUP_REPORT.md)
