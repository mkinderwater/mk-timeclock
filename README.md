# mk-timeclock Lite

A free, self-hosted RFID employee time clock for the **Banana Pi BPI-M2 Zero**, developed by **iCube Development (Calgary) Ltd.**

Record attendance, manage employees and vacation, and export payroll reports from a web browser. No subscription or cloud account is required.

Current release: **1.9.5-RC3-cleanup27**.

## The clock

| Front | Back | Inside |
| --- | --- | --- |
| ![Front of the mk-timeclock](https://raw.githubusercontent.com/mkinderwater/mk-timeclock/main/front.png) | ![Back of the mk-timeclock](https://raw.githubusercontent.com/mkinderwater/mk-timeclock/main/back.png) | ![Internal assembly of the mk-timeclock](https://raw.githubusercontent.com/mkinderwater/mk-timeclock/main/internals.png) |

## Features

### For business owners

- **Keep control of your records:** run the system locally, without a subscription, per-employee software fees or a required cloud account.
- **Track attendance:** card-based IN/OUT punches, employee names on the display, audible confirmation and a view of who is recorded as clocked IN.
- **Manage your team:** search employees by name, use your company's Employee IDs, assign attendance cards and keep manager authorization cards separate.
- **Review exceptions:** weekly or dated schedules, configurable grace, late arrivals, final early departures and missing-punch flags. Managers can correct records and add explanations, with an audit history.
- **Prepare payroll:** weekly, every-two-weeks, twice-monthly or monthly periods; employee timesheet review and approval; manager overrides; CSV and Excel exports with employee summaries and shift detail. Saved revisions preserve earlier reports.
- **Handle vacation:** employee requests, manager approvals, previously approved or historical vacation, annual usage against an optional allowance, holiday calendars and CSV holiday import.
- **Automate communication:** choose when timesheet links and payroll reports are sent. Edit notification templates and use your own SMTP server for employee and manager emails.
- **Recover your data:** daily local backups, optional emailed backup copies, manager-authorized downloads and restore.

### For installers and resellers

- **Deploy one clock or several:** a Primary hosts management and attendance records; approved Secondary clocks upload punches and keep a local queue during network outages.
- **Repeat the installation:** use the prepared OS image and included installer on the supported Banana Pi hardware.
- **Maintain it through the browser:** manage clock names, HTTPS certificates, email delivery, device health and system operations.
- **Supply and customize clocks:** commercial use and redistribution are permitted under AGPL-3.0. See [License](#license) for source-sharing obligations.

### For makers and developers

- **Build with common parts:** RC522 reader, SSD1309 OLED, active buzzer, Wi-Fi antenna and a printed enclosure.
- **Work with accessible tools:** C programs, SQLite storage, Python and shell helpers on Linux.
- **Adapt the system:** source and build scripts are available for modifying the interface, reports, notifications and hardware integration.
- **Develop against real workflows:** the repository includes automated checks for attendance, payroll exports, vacation, notifications and management pages.

An IN starts recorded time; an OUT stops it. Missing punches are flagged. The clock does not invent hours, deduct breaks, calculate overtime pay or decide which hours are payable.

## Estimated build cost

Budget **C$90–150 per clock in 2026**, typically around **C$120**, including electronics, a microSD card, power supply and material for a self-printed enclosure. Software is free. Shipping, tax, tools, assembly labour and extra employee cards are additional.

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

Case v8.34 uses internal M2 × 5 mm fasteners for the OLED and RC522. Use right-angle headers to fit the enclosure.

Print the [front](https://github.com/mkinderwater/mk-timeclock/blob/main/front.stl) and [back](https://github.com/mkinderwater/mk-timeclock/blob/main/back.stl). View the [assembled model](https://github.com/mkinderwater/mk-timeclock/blob/main/assembled.stl) to see how they fit together.

## Wiring

![Wiring guide](https://raw.githubusercontent.com/mkinderwater/mk-timeclock/main/wiring-guide.png)

These are **physical pin numbers** on the Banana Pi's 40-pin header. Disconnect power while wiring. Use 3.3 V for the reader and display.

### Wi-Fi antenna

The internal photo shows a slim rectangular **PCB Wi-Fi antenna** mounted flat against the inside of the front panel, **to the right of the RC522 reader**, with its long edge vertical.

Use a stick-on **2.4 GHz antenna with a thin coax lead and IPEX/u.FL plug**. Connect it to the Banana Pi's small gold antenna socket near the HDMI connector. Route the lead around the lower edge of the assembly, as pictured.

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

Upload the source ZIP using WinSCP, SFTP or scp. Connect over SSH using the clock's IP address:

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

Review and close the pay period, create an export, then send the report when needed. The Excel workbook has two tabs:

- **Payroll Summary:** one line per employee, showing worked hours, vacation days, sick days, stat holidays, other recorded leave, and exceptions or manager notes. Annual vacation shows days used against the employee’s allowance, such as **6 of 15 days**. The allowance is for reference; it does not block requests.
- **Daily Detail:** dated shifts with actual IN and OUT times, worked hours, attendance status, and notes or corrections. Late arrivals and early departures show the affected date and minutes. Approved vacation, stat holidays and recorded absences have their own rows.

Both tabs include Employee ID, using the company’s ID when supplied and the clock’s internal ID otherwise. Daily Detail follows the Summary’s employee order, with alternating shaded blocks. Click a name in the Summary to jump to that employee’s records, or use the detail filters to review a date or employee. Undated reference and override rows stay at the end of the relevant employee’s block.

Late and early flags apply after the configured grace. Actual punch times and recorded hours remain visible, alongside explanations such as an approved departure. Manager corrections and approval overrides appear in the report so payroll can see what was reviewed.

Worked hours and leave days are separate. Payroll determines compensation; the clock does not calculate wages. Summary hours add the rounded shift hours in the CSV. Email period totals use seconds and can differ slightly. A completed overnight shift belongs to the pay period containing its local IN date.

Changes after closing can require a new export revision. Earlier saved revisions remain unchanged, and each emailed workbook is generated from its saved revision.

### Sample payroll reports

These previews use fictional employees for October 12–16, 2026. Click an image to view it full size.

**Payroll Summary** shows totals, vacation used and allowed, and exceptions. Sarah’s late arrival and early departure appear alongside Robert’s manager correction and override.

[![Sample Payroll Summary with attendance exceptions and vacation allowances](payroll-summary.png)](payroll-summary.png)

**Daily Detail** shows the records behind those totals: Thanksgiving, David’s sick day, Elena’s approved vacation, Robert’s corrected OUT and Sarah’s attendance exceptions.

[![Sample Daily Detail with shifts, leave, holidays and manager corrections](payroll-daily-detail.png)](payroll-daily-detail.png)

**Late arrival and early departure, close up:** Sarah arrived 12 minutes late on October 13 and left 60 minutes early on October 16. The report retains her actual times and hours, with “Traffic delay” and “Manager approved departure” beside the affected shifts.

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

Prewritten PDF manuals are provided for employees and managers:

- [Employee guide](Timeclock-Employee-Guide.pdf)
- [Manager guide](Timeclock-Manager-Guide.pdf)

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

