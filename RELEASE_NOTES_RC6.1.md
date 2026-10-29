# RC6.1

Slave heartbeats now report route-selected local IP address and running software version. Primary Clock management displays both alongside status and retains the last report while offline. Empty fields show Not reported. The table scrolls horizontally on narrow screens. Update Primary and Slaves.

Two current health metadata columns are added; attendance data and the RC6 vacation report are unchanged. Reports are authenticated through the existing per-clock key and identity checks. IPv4/IPv6 and version values are validated; GUI values use text nodes.

Validation passed: production publisher through local HTTP/libcurl; production receiver and manager list through real SQLite; production GUI script with DOM fixtures; RC6 vacation acceptance; setup key/recovery; axis spacing and generator checks. Isolated C harnesses compile with warnings treated as errors. Full application compilation, visual browser layout and hardware acceptance remain unverified because SQLite/FreeType development dependencies and Chromium are unavailable here.
