# Test policy

Ten areas: installation; security and devices; clock input; schedules and time; attendance, time off and late reasons; payroll; reviews; reliability and local backup; physical hardware/UI; notifications.

Each automated test checks observable behavior. Do not add production code for outdated test fixtures. Safety and durability source tests are **replace before retiring**, never delete first.

Use fresh SQLite and isolated temporary directories. Authenticate and register clocks in HTTP tests. Assert 401/409 denial paths too. Verify recipient, trigger, once-only behavior, suppression, links and human dates in notification tests.

The accepted walkthrough is a dev-machine integration test requiring faketime and Playwright, not a Pi CI test. Do not claim coverage that has not been implemented.
