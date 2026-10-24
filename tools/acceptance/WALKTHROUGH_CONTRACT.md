# Pay-period walkthrough acceptance contract

Scenario: October 1-15, 2026, Sam, Priya, Lee, Alex and manager Dana. Reuse existing faketime and Playwright runner when available.

- Real card taps: IN/OUT pairs, late IN, duplicate taps at 0, 15, 90 and 130 seconds; actual OUT later.
- Sick absence, approved vacation, Thanksgiving; no mistaken missing-IN notification.
- Missed OUT: manager notified despite employee template off; employee only if enabled; once per day.
- Late reason link: authorized recipient, valid link, saved reason appears in review and payroll.
- Time off: private link, calculated working days, approve/deny notifications, history.
- Review links: sent after period, employee confirmation, correction invalidates confirmation, manager override with reason, close.
- Emails: recipient, trigger, exactly once, suppression, dates readable to humans, links usable.
- Payroll: correct hours, holiday eligibility, two fixed tabs, employee blocks, row styles/status, XLSX totals and report.
- Negative checks: unauthenticated routes return 401, unadopted clocks denied, duplicate sends not generated.

The remaining planned features (correction review link, pre-close reminder, readable email dates) must not be asserted as existing until implemented.
