# mk-timeclock Lite

A free, self-hosted RFID employee time clock for the **Banana Pi BPI-M2 Zero**, developed by **iCube Development (Calgary) Ltd.**

Tap a card to clock IN or OUT. Manage attendance and vacation in your browser. Export payroll to Excel or CSV. **No subscription, per-employee fees or required cloud account.**

Attendance is stored locally. Email needs access to your SMTP server; Secondary clocks need a network connection to upload queued punches to the Primary.

Current release: **1.9.5-RC3-cleanup27**.

**[Download the latest mk-timeclock](https://github.com/mkinderwater/mk-timeclock/releases)**

## The clock

| Front | Back | Inside |
| --- | --- | --- |
| ![Front of the mk-timeclock](https://raw.githubusercontent.com/mkinderwater/mk-timeclock/main/front.png) | ![Back of the mk-timeclock](https://raw.githubusercontent.com/mkinderwater/mk-timeclock/main/back.png) | ![Internal assembly of the mk-timeclock](https://raw.githubusercontent.com/mkinderwater/mk-timeclock/main/internals.png) |

## The management interface

Screenshots use fictional employees. Click an image to view it full size.

### Review attendance

**See who arrived late or left early, and by how many minutes.** Overview shows today's punches and the clock display, with a link to corrections. Lunch OUT/IN punches remain in the record.

[![Overview showing employee punches, a late arrival and an early departure](overview.png)](overview.png)

### Manage vacation

**Waiting requests come first.** Approve or deny them, record vacation approved elsewhere, and filter upcoming or past bookings by employee and year. Stat holidays are counted separately. Download the annual report as an Excel-compatible CSV.

[![Vacation management showing waiting requests, approved bookings, holiday exclusions and annual reporting](vacation.png)](vacation.png)

## Features

### For business owners

- **Attendance:** RFID IN/OUT, names on the display, audible confirmation and a Who is in view.
- **Exceptions:** weekly or dated schedules, grace, late arrivals, early departures and missing punches. Manager corrections and notes are audited.
- **Employees:** name search, company Employee IDs, attendance cards and separate manager cards.
- **Payroll:** weekly, biweekly, twice-monthly or monthly periods; timesheet approval, manager overrides and saved CSV/Excel revisions.
- **Vacation:** requests, approvals, historical entries, annual usage and allowances. Add holidays manually or import CSV.
- **Email:** scheduled timesheet links and payroll reports, editable templates and your SMTP server.
- **Backups:** daily local archives, optional emailed copies, download and restore.

### For installers and resellers

- A Primary hosts management and records. Approved Secondary clocks queue punches during outages and upload when reconnected.
- Install from the prepared OS image and source ZIP.
- Manage clock names, HTTPS certificates, email and device health through the browser.
- Commercial use and redistribution are permitted under [AGPL-3.0](#license).

### For makers and developers

- Common modules, a printable enclosure and documented wiring.
- C, SQLite, Python and shell on Linux.
- Source, build scripts and automated workflow checks included.

**IN starts recorded time. OUT stops it.** Missing punches are flagged, not filled in. There are no automatic break deductions, overtime pay calculations or wage calculations. Payroll decides what is payable.

## Estimated build cost

Budget **C$90–150 per clock**, typically **C$120** at estimated 2026 prices. This includes electronics, microSD, power supply and printed-case material. Shipping, tax, tools, labour and extra cards are additional.

## What you need

| Component | Specification |
| --- | --- |
| Computer | Banana Pi BPI-M2 Zero |
| Storage | microSD card, 16 GB or larger |
| RFID reader | RC522 / MFRC522 with compatible 13.56 MHz cards or fobs |
| Display | 2.42-inch SSD1309 OLED, 128 × 64, configured for I²C |
| Buzzer | Compatible two-wire active buzzer |
| Power | USB-C power breakout and regulated 5 V, 2 A supply |
| Wi-Fi antenna | Stick-on 2.4 GHz PCB antenna with coax lead and IPEX/u.FL plug |
| Network | 2.4 GHz Wi-Fi |
| Assembly | Jumper wires, headers, fasteners and optional printed case |

### Component photos

Use these images to identify parts. Check your board markings against the wiring tables before connecting power.

| RFID reader and credentials | 2.42-inch OLED display |
| :---: | :---: |
| [![RC522 RFID reader, card, fob and eight-pin header](rfid.png)](rfid.png) | [![Front and rear of the seven-pin SSD1309 OLED display](oled.png)](oled.png) |
| **RC522 / MFRC522, 13.56 MHz.** Reader, card, fob and header. | **SSD1309, 128 × 64.** Seven-pin board configured for I²C. |

| USB-C power breakout | Active buzzer |
| :---: | :---: |
| [![USB-C breakout board showing G, positive D, negative D and V pads](usb-c.png)](usb-c.png) | [![Two views of a wired active buzzer showing its housing and voltage marking](buzzer.png)](buzzer.png) |
| **USB-C breakout.** Connect `V` and `G` for power. | **Two-wire active buzzer.** Red and black leads. |

PCB layouts vary. Use the [wiring tables](#wiring) and the OLED resistor labels below to identify connections.

### Print the enclosure

**PLA or ASA. ASA recommended. No supports required.**

Case v8.34 uses internal M2 × 5 mm fasteners for the OLED and RC522. Use right-angle headers to fit the enclosure.

Print the [front](https://github.com/mkinderwater/mk-timeclock/blob/main/front.stl) and [back](https://github.com/mkinderwater/mk-timeclock/blob/main/back.stl). View the [assembled model](https://github.com/mkinderwater/mk-timeclock/blob/main/assembled.stl) to see how they fit together.

## Wiring

![Wiring guide](https://raw.githubusercontent.com/mkinderwater/mk-timeclock/main/wiring-guide.png)

These are **physical pin numbers** on the Banana Pi's 40-pin header. Disconnect power while wiring. Use 3.3 V for the reader and display.

### Wi-Fi antenna

Use a **2.4 GHz PCB antenna with an IPEX/u.FL lead**. Connect it to the gold socket near the HDMI connector. Mount it vertically inside the front panel, to the right of the RC522, as shown in the internal photo. Route the lead around the lower edge.

### RC522: SPI0

| RC522 | BPI pin | Signal |
| --- | ---: | --- |
| 3.3V | 17 | 3.3 V |
| GND | 20 | Ground |
| SDA / SS | 24 | PC3, chip select |
| SCK | 23 | PC2 |
| MOSI | 19 | PC0 |
| MISO | 21 | PC1 |

The RC522's SDA pin is SPI chip select in this setup.

### OLED: I²C0

| OLED | BPI pin | Signal |
| --- | ---: | --- |
| VCC | 1 | 3.3 V |
| GND | 9 | Ground |
| SDA | 3 | PA12 |
| SCL | 5 | PA11 |

For the project's seven-pin SSD1309 PCB, configure I²C:

| Location | Change |
| --- | --- |
| R8 | Remove the 0 Ω resistor |
| R9 | Move the resistor here, or bridge the pads |
| R10, R11, R12 | Bridge the pads |

These labels apply to the project's OLED PCB. The display uses address `0x3c`.

### Buzzer

| Buzzer | BPI pin | Signal |
| --- | ---: | --- |
| Positive / control | 7 | PA6 |
| Ground | 6 | Ground |

### USB-C power breakout

| Breakout | BPI pin | Signal |
| --- | ---: | --- |
| VBUS | 2 | 5 V |
| GND | 39 | Ground |

## Install

### 1. Write the image

Download the prepared [bpi-zero-wbuild 3.13 image](https://github.com/mkinderwater/bpi-zero-wbuild/releases/tag/3.13):

`bpi-zero-wbuild-3.13-trixie-minimal-hw-bpi-m2-zero.img.gz`

Write it to the microSD card with balenaEtcher.

### 2. Set your credentials

Reinsert the card. Open `CONFIG.TXT` on the `BPIWBUILD` volume and enter your Wi-Fi and SSH credentials:

```ini
SSID=OfficeWiFi
PSK=your-wifi-password
COUNTRY=CA
HIDDEN=false
TIMEZONE=America/Edmonton
ROOT_PASSWORD=your-ssh-password
```

Insert the card into the clock and power it on. Allow about ten minutes for first boot, then find its IP address in your router.

### 3. Upload and install

[Download the latest mk-timeclock ZIP](https://github.com/mkinderwater/mk-timeclock/releases), then upload it using WinSCP, SFTP or scp. Connect over SSH using the clock's IP address:

```bash
ssh root@192.168.1.50
```

Replace the example IP with your clock's address. Use the `ROOT_PASSWORD` entered in `CONFIG.TXT`.

Unzip the archive and run the installer:

```bash
unzip mk-timeclock-lite-RC3-cleanup27.zip
cd mk-timeclock-lite-RC3-cleanup27
./install-master.sh
```

The installer handles dependencies, compilation and service setup.

### 4. Open the clock

Visit `https://<clock-ip>/setup` and create your manager account. Scan a card to register it for manager authorization.

## Set up your business

- Add employees, company Employee IDs and attendance cards.
- Assign weekly or dated schedules.
- Configure outgoing email and choose which notifications to send.
- Set the pay schedule and the send times for payroll reports and timesheet links.
- Add holidays and optional annual vacation allowances.

Holiday CSV format:

```csv
Date,Holiday
2026-12-25,Christmas Day
2027-01-01,New Year's Day
```

Use `YYYY-MM-DD` dates. Review imported rows and click **Save holidays**.

## Daily use

Employees tap to clock IN and OUT. Managers use **Punches and corrections** to resolve missing punches, correct times and add notes.

Late arrivals are checked against the scheduled start and grace. Early departures are checked after the scheduled finish plus grace, allowing for lunch and other returns.

Employees request vacation through a private link. Managers approve requests or record earlier approvals, including verbal, emailed and historical vacation. Annual usage shows **X of Y days**. The allowance is a reference, not a limit.

## Payroll

Review and close the pay period, create an export, then send the report when needed. The Excel workbook has two tabs:

- **Payroll Summary:** employee hours, vacation, sick days, stat holidays, other recorded leave and exceptions. Annual vacation shows days used against the allowance, such as **6 of 15 days**.
- **Daily Detail:** dates, actual IN/OUT times, hours, attendance flags and notes. Leave and holidays have separate rows.

Both tabs use the company’s Employee ID, or the clock’s internal ID if none is supplied. Daily Detail groups each employee’s records in a shaded block, with undated rows at the end. Click a Summary name to jump to their detail, or filter by employee or date.

Late and early flags retain the actual times, hours and notes. Corrections and approval overrides show what the manager reviewed.

Worked hours and leave days are separate. Summary hours add the rounded CSV shift hours; email totals use seconds and may differ slightly. An overnight shift belongs entirely to the pay period containing its local IN date.

Changes after closing may require a new export. Saved revisions stay unchanged; emailed workbooks use the saved revision.

### Sample payroll reports

These previews use fictional employees for October 12–16, 2026. Click an image to view it full size.

**Payroll Summary** shows totals, vacation used and allowed, and exceptions. Sarah’s late arrival and early departure appear alongside Robert’s manager correction and override.

[![Sample Payroll Summary with attendance exceptions and vacation allowances](payroll-summary.png)](payroll-summary.png)

**Daily Detail** shows the records behind those totals: Thanksgiving, David’s sick day, Elena’s approved vacation, Robert’s corrected OUT and Sarah’s attendance exceptions.

[![Sample Daily Detail with shifts, leave, holidays and manager corrections](payroll-daily-detail.png)](payroll-daily-detail.png)

**Exceptions in detail:** Sarah arrived 12 minutes late on October 13 and left 60 minutes early on October 16. Her actual times, hours and explanations remain beside each shift.

[![Daily Detail showing Sarah’s late arrival and approved early departure](payroll-exceptions.png)](payroll-exceptions.png)

## Additional clocks

Install a Secondary with:

```bash
./install-slave.sh
```

Generate an adoption code on the Primary, enter it on the Secondary's setup page and approve the request on the Primary. Secondary clocks queue punches while offline and upload them when reconnected.

## Backups and upgrades

The Primary keeps the latest **30 local backups**, created daily around **02:15**. Manual backups count toward the same limit. Use **Backup and restore** to download or restore an archive.

For an upgrade, upload and extract the new ZIP, then rerun `./install-master.sh` from its directory. Use `install-slave.sh` on a Secondary. Existing configuration and data are preserved.

## Documentation

PDF manuals and templates:

- [Employee guide](Timeclock-Employee-Guide.pdf)
- [Manager guide](Timeclock-Manager-Guide.pdf)
- [Build and install guide](Timeclock-Build-and-Install-Guide.pdf)
- [Wall mounting template](Timeclock-Wall-Mounting-Template.pdf)

Technical documentation:

- [Installation and roles](docs/INSTALLATION.md)
- [Manager operations](docs/OPERATIONS.md)
- [Attendance and payroll rules](docs/ATTENDANCE_PAYROLL.md)
- [Backup and restore](docs/BACKUP_RESTORE.md)
- [Development and testing](docs/TESTING.md)

## License

mk-timeclock Lite is free software licensed under the **GNU Affero General Public License, version 3 (AGPL-3.0)**. See [LICENSE](LICENSE) for the full terms.

Personal, internal business and commercial use are permitted. You may inspect, modify and redistribute the software under the license.

When redistributing covered software, retain required notices and provide Corresponding Source as the license requires. If you modify the program and let users interact with it over a network, prominently offer those users the modified version's Corresponding Source at no charge.

Employee information, attendance records, payroll exports and backups do not become public merely because you use this software. Third-party components retain their own licenses.

AGPL was chosen to keep the clock free software and make modified network versions' source available to their users. The software is supplied without warranty, to the extent permitted by law.

## Contributions

Issues and contributions are welcome. Include the version, clock role and steps to reproduce a problem. Remove employee details, passwords, card IDs and private links from public reports.

