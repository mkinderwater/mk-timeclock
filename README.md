# mk-timeclock Lite

**A free, DIY RFID employee time clock for the Banana Pi BPI-M2 Zero.** Build your own attendance clock with an RC522 RFID reader, a small OLED display, and a buzzer. Record real IN/OUT punches, manage employees in a web browser, and export timesheets for payroll.

**No subscription. No cloud account required. No proprietary time clock terminal.** The Primary clock runs its own local database and web application. Optional Secondary clocks send punches to it and queue records when offline. Email features need a configured SMTP server.

Built by **iCube Development (Calgary) Ltd.** for practical attendance recording, with source code, a repeatable hardware build, and readable technical documentation. Designed for small businesses, workshops, makers, Raspberry Pi and Banana Pi enthusiasts, IT administrators, and developers who prefer to understand their own systems.

> **Current release:** `1.9.5-RC3-cleanup27` (release candidate). This package contains source and installers, not a ready-to-flash timeclock image. Use the separately published supported OS image described below. Hardware acceptance and full production testing remain the installer's responsibility.

## Why build mk-timeclock?

A basic time clock should do a few things well: recognize a card, record the actual punch, show the employee what happened, and make the records accessible to whoever manages attendance. It should not require a recurring fee or decide your payroll policy.

mk-timeclock follows that approach. An **IN** starts recorded elapsed time. An **OUT** stops it. Missing punches are flagged instead of being fabricated. There are no automatic unpaid breaks, overtime wage rules, or invented payable hours. Managers review and correct exceptions, with a history of changes. Payroll exports describe **recorded attendance**, not a payroll calculation engine.

### At a glance

| | What you get |
| --- | --- |
| **Hardware** | Banana Pi BPI-M2 Zero, MFRC522/RC522 13.56 MHz RFID reader, 128 × 64 SSD1309 OLED, buzzer |
| **User feedback** | Employee name, IN/OUT confirmation, clock time, audible accepted/rejected scan signals |
| **Storage** | Local SQLite database on the Primary; persistent offline punch queue on each reader clock |
| **Management** | Browser-based HTTPS dashboard for employees, cards, schedules, corrections, leave, reports, and settings |
| **Multi-clock** | One authoritative Primary and optional adopted Secondary reader clocks |
| **Reporting** | Daily attendance, employee timesheets, payroll CSV and Excel-compatible workbooks, saved report revisions |
| **Time off** | Vacation requests, manager approvals, holiday calendars, CSV holiday import, annual usage reports |
| **Maintenance** | Local backups/restore, service diagnostics, test suite, shell installer, C source |
| **Cost model** | Free software, self-hosted. No mandatory SaaS or per-employee subscription |

## See how it works

```text
Employee presents RFID card
          |
          v
 RC522 reader on Banana Pi
          |
          +--> OLED shows employee + IN / OUT
          +--> Buzzer confirms valid or rejected card
          +--> Punch is saved to local persistent queue
                            |
                       Network available?
                            |
                            v
                 Primary clock over HTTPS
                            |
                   SQLite attendance records
                            |
                    Manager web dashboard
                      /      |       \
                Timesheets  Leave    Payroll exports
```

A single Primary clock is enough for one entry point. Add Secondary reader clocks for separate entrances or work areas. If a Secondary loses network access, it retains queued punches and uploads them after connection returns. Secondary clocks are **not** automatic backup servers or database replicas.

## Features

### The experience at the clock

- Tap an assigned RFID card or fob to record attendance, without a keyboard, touchscreen, or employee login.
- See the employee's first name and the recorded **IN** or **OUT** on the 128 × 64 OLED.
- Hear distinct, nonblocking feedback for accepted and rejected cards.
- See current time and clock status. Cards and pending punches are cached locally.
- Use a separate manager card to authorize protected operations without adding attendance punches.
- Adjust duplicate-swipe protection and lateness grace on each clock.

### Employees, attendance, and exceptions

- Add, edit, activate, and deactivate employees; assign more than one card, replace a lost card, or disable it.
- Assign an optional company Employee ID, including identifiers with leading zeros.
- Define recurring weekly schedules, dated shifts, or employees with no fixed schedule.
- Flag missing IN/OUT pairs, late arrivals, and early departures; record a reason without silently changing the punch.
- Avoid treating an ordinary lunch return as an early departure: departure checks wait until the scheduled end plus grace and consider returns. Corrected or manually added departures use the same deferred check.
- Make manager-only corrections or manually record a missing punch with audit history.
- Record excused absences separately from actual hours worked.
- Show employee timesheets through private HTTPS links; support period review, approval, and reporting a concern according to employee mode.

### Vacation, holidays, and notifications

- Let employees submit vacation requests from a private link.
- Calculate working days using employee schedules and the holiday calendar; suggest a return-to-work date.
- Allow managers to approve, deny, cancel, or directly enter already-approved and historical vacation.
- Track optional annual allowance and **days used**, including approved future bookings; allowance is informational, not a hard limit.
- Add statutory holidays one at a time or stage and review a batch **CSV import**.
- Send configurable email for relevant attendance, vacation, employee-link, and payroll events through SMTP, with queue/retry visibility.
- Give employees private explanation links for eligible attendance exceptions; record manager notes and actions.

### Reporting for real businesses

- Choose **weekly, biweekly, semi-monthly, or monthly** pay periods.
- Review timesheets, exceptions, and approvals before preparing payroll.
- Export attendance as CSV and create Excel workbooks with **Payroll Summary** and **Daily Detail**.
- Preserve a report's source CSV when exported. Later changes create a **new revision**, not a silent replacement.
- Identify closed pay periods that have changed because of late punches, manager edits, or leave updates.
- Set separate local send times for payroll reports and employee timesheet links.
- Export annual vacation usage and holiday-aware leave reporting.

### Built for maintainability

- Small native C programs for the clock reader, server, and mailer; SQLite rather than a separate database server.
- Python and shell for setup, UI generation, checks, and supporting utilities.
- A single shared installer with clear Primary/Secondary entrypoints.
- Local backup, download, and restore operations, plus optional backup email. Each clock keeps its own backups.
- SSH, systemd, journal logs, a health endpoint, and documented configuration for troubleshooting.
- Source-visible behavior and a maintained automated regression suite.

**Not included:** touchscreen attendance, biometric identification, automatic break deductions, overtime/pay-rate calculations, payment processing, automatic Primary failover, or backup replication. The optional TTP223 touch module has no assigned function or connection in this release.

## Quick start

**1. Assemble the clock.** Use the Banana Pi BPI-M2 Zero, RC522, supported SSD1309 OLED module, compatible buzzer and a stable power supply. A project-specific 3D-printed enclosure is described in the hardware section. Confirm the exact pinout before powering on.

**2. Flash the supported image.** Get [bpi-zero-wbuild 3.13](https://github.com/mkinderwater/bpi-zero-wbuild/releases/tag/3.13), write it to a microSD card, set your Wi-Fi and timezone in `CONFIG.TXT`, and boot the Banana Pi. The installer checks the supported board/image environment.

**3. Copy this repository onto the Pi.** From its extracted source directory:

```bash
python3 rc1-preflight.py
sudo ./install-master.sh
```

The installer installs target dependencies, builds the programs, configures services, and sets up HTTPS. Use `sudo ./install-slave.sh` **instead** for an additional Secondary clock.

**4. Open the setup page:** `https://<clock-ip>/setup`. Create a manager account, pair a manager card, configure time synchronization, HTTPS, employees and cards. Test a genuine IN/OUT pair before relying on the reports.

**5. Run it locally.** Visit `https://<clock-ip>/login` to administer employees, attendance, vacation and payroll exports. A trusted certificate and hostname are recommended before issuing employee links.

See [Installation details](#installation), [Wiring](#pinouts-and-wiring), and [First setup](#first-setup) for the full procedure.

## Who is this for?

- **DIY builders and makers:** a real RFID + OLED + SBC project you can wire, print, test, and modify.
- **Small businesses and workshops:** practical local attendance without mandatory cloud subscriptions.
- **Systems administrators:** self-hosted HTTPS, SQLite, SMTP, time synchronization, service logs, and backups.
- **Developers:** native C, device interfaces, revisioned exports, end-to-end tests, and documented attendance semantics.
- **Schools and learning labs:** a substantial embedded Linux example combining SPI, I²C, GPIO, a web service, and a relational database. Use test data rather than real employee records in classroom deployments.

## Documentation map

| Goal | Start here |
| --- | --- |
| Assemble and wire hardware | [Hardware](#hardware-and-operating-system) and [Pinouts](#pinouts-and-wiring) |
| Install first clock | [Installation](#installation) and [Installation and roles](docs/INSTALLATION.md) |
| Add another reader | [Additional clocks](#additional-clocks) |
| Understand punches and pay periods | [Attendance and payroll](docs/ATTENDANCE_PAYROLL.md) |
| Manage holidays, leave, notifications | [Manager operations](docs/OPERATIONS.md) |
| Understand backups | [Backup and restore](docs/BACKUP_RESTORE.md) |
| Run or extend tests | [Testing](docs/TESTING.md) |
| Inspect SQLite structures | [Schema](docs/SCHEMA.md) |

## Build and install overview

Use the **Timeclock Build and Install Guide** if you have a copy of the illustrated assembly guide. It is not included in this source archive. The instructions below provide the wiring reference and deployment procedure.

1. **Hardware:** print case v8.34 if available, mount the components, convert the supported OLED PCB to I²C, and inspect all 14 jumper connections with power disconnected.
2. **Software:** flash the prepared bpi-zero-wbuild 3.13 image, configure Wi-Fi, transfer this source tree, and install one role.
3. **Finish:** create a manager, test cards and output devices, close the case, and mount the clock.

**Primary** is the first or only clock. **Secondary** is an additional reader. Source code, scripts, and service messages may use the older technical name `slave` for Secondary.

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

Copy this repository or the `mk-timeclock-lite-RC3-cleanup27.zip` source archive to `/root` using SFTP/WinSCP or `scp`, then run on the clock:

```bash
sudo apt-get update
sudo apt-get install -y unzip python3
cd /root
unzip mk-timeclock-lite-RC3-cleanup27.zip
cd mk-timeclock-lite-RC3-cleanup27
```

If using a Git clone or another release, enter its actual source directory instead. The zip filename above refers to the cleanup27 archive, not a required download URL. The prepared image must expose the SPI, I²C, and GPIO devices listed above.

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

Install a Secondary clock with:

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

## Project status, licence, and contributions

**Status:** RC3 release candidate, not certified commercial timekeeping or payroll software. Current automated checks are summarized in [Development and verification](#development-and-verification). Real-hardware, power-loss, network recovery, restore, and production email tests remain important before use with employee records.

**Free and publicly readable source:** This repository is intended to make a self-hosted employee RFID time clock available without a subscription. However, **this supplied archive does not contain a `LICENSE` file**. Until a licence is added, the project should not be described as legally open-source under a specific OSI-approved licence, and reuse/distribution permissions should not be assumed. The repository owner should add the intended licence before inviting public forks or redistribution.

For issues and contributions, include the release version, Primary/Secondary role, exact board/image version, relevant logs with credentials and employee details removed, and steps to reproduce. For code changes, run the maintained tests and describe which on-device checks were performed. Don't publish RFID UIDs, private employee links, SMTP passwords, backups, or payroll data in public issues.

### Discoverability

Topics suitable for this GitHub project: `rfid`, `time-clock`, `employee-attendance`, `time-tracking`, `diy-electronics`, `banana-pi`, `bpi-m2-zero`, `rc522`, `mf-rc522`, `oled`, `ssd1309`, `sqlite`, `self-hosted`, `linux`, `embedded-c`, `payroll-export`, `open-hardware` (only if enclosure design files are publicly released).

**Keywords:** free RFID time clock software, DIY employee time clock, self-hosted attendance system, Banana Pi RFID clock, RC522 time attendance, open source timeclock alternative, employee timesheets, offline RFID time tracking, payroll CSV export, small business time clock.
