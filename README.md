# mk-timeclock Lite

A free, self-hosted RFID employee time clock for the **Banana Pi BPI-M2 Zero**, developed by **iCube Development (Calgary) Ltd.**

Record attendance, manage employees and vacation, and export payroll reports from a web browser. No subscription or cloud account is required.

Current release: **1.9.5-RC3-cleanup27**.

## The clock

| Front | Back | Inside |
| --- | --- | --- |
| ![Front of the mk-timeclock](https://raw.githubusercontent.com/mkinderwater/mk-timeclock/main/front.png) | ![Back of the mk-timeclock](https://raw.githubusercontent.com/mkinderwater/mk-timeclock/main/back.png) | ![Internal assembly of the mk-timeclock](https://raw.githubusercontent.com/mkinderwater/mk-timeclock/main/internals.png) |

## Features

- RFID IN/OUT punches with employee names, OLED feedback and distinct buzzer sounds.
- Browser management for employees, cards, schedules and attendance corrections.
- Weekly and dated schedules, late-arrival grace and final early-departure checks.
- Editable employee and manager email notifications.
- Employee timesheet links and vacation requests.
- Manager-recorded vacation, annual allowances and holiday CSV import.
- Payroll CSV and two-tab Excel workbooks, with saved report revisions.
- Local backups and restore.
- Optional Secondary clocks with offline punch queues.

An IN starts recorded time; an OUT stops it. Missing punches are flagged. The clock does not invent hours, deduct breaks, calculate overtime pay or decide which hours are payable.

## What you need

| Component | Specification |
| --- | --- |
| Computer | Banana Pi BPI-M2 Zero |
| Storage | microSD card, 16 GB or larger |
| RFID reader | RC522 / MFRC522 with compatible 13.56 MHz cards or fobs |
| Display | 2.42-inch SSD1309 OLED, 128 × 64, configured for I²C |
| Buzzer | Compatible two-wire active buzzer |
| Power | USB-C power breakout and regulated 5 V, 2 A supply |
| Network | 2.4 GHz Wi-Fi |
| Assembly | Jumper wires, headers, fasteners and optional printed case |

Case v8.34 uses internal M2 × 5 mm fasteners for the OLED and RC522. Use right-angle headers to fit the enclosure.

## Wiring

These are **physical pin numbers** on the Banana Pi's 40-pin header. Disconnect power while wiring. Use 3.3 V for the reader and display.

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

Upload the source ZIP using WinSCP, SFTP or scp. Connect over SSH, then unzip it and run the installer:

```bash
unzip mk-timeclock-lite-RC3-cleanup27.zip
cd mk-timeclock-lite-RC3-cleanup27
sudo ./install-master.sh
```

The installer handles dependencies, compilation and service setup.

### 4. Open the clock

Visit `https://<clock-ip>/setup` and create your manager account. Scan a card to register it for manager authorization.

A fresh installation uses a self-signed HTTPS certificate. You can upload your own through Settings.

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

Employees tap their cards to clock IN and OUT. Managers use **Punches and corrections** to resolve missing punches, correct times and record explanations.

Late arrivals are checked against the scheduled start and grace. Early departures are checked after the scheduled finish plus grace, allowing for lunch and other returns.

Employees can request vacation through their private link. Managers approve requests or record vacation approved verbally, by email or before the clock was installed. Annual usage appears as **X of Y days**; the allowance is informational and does not block requests.

## Payroll

Review and close the pay period, create an export, then send the report when needed.

- **Payroll Summary** shows employee totals and notes.
- **Daily Detail** groups records by employee, with filters and links from the Summary.
- A completed shift belongs to the pay period containing its local IN date, including overnight shifts.
- Corrections can require a new export revision. Earlier saved revisions remain unchanged.

## Additional clocks

Install a Secondary with:

```bash
sudo ./install-slave.sh
```

Generate an adoption code on the Primary, enter it on the Secondary's setup page and approve the request on the Primary. Secondary clocks queue punches while offline and upload them when reconnected.

## Backups and upgrades

The Primary keeps the latest **30 local backups**, created daily around **02:15**. Manual backups count toward the same limit. Use **Backup and restore** to download or restore an archive.

For an upgrade, upload and extract the new ZIP, then rerun `sudo ./install-master.sh` from its directory. Use `install-slave.sh` on a Secondary. Existing configuration and data are preserved.

## Documentation

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

