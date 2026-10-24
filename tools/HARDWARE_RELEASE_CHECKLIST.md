# Physical device release smoke test

Run on a real clock, not in CI. Record firmware build, device identifier and result.

- [ ] Cold boot: logo then idle display, correct time and IP
- [ ] RFID: valid card recognized; invalid/revoked refused; no repeated OUT within 2 minutes after last tap
- [ ] OLED: name, direction, status and display diagnostics legible
- [ ] Buzzer: distinct IN/OUT/invalid feedback, nonblocking
- [ ] Touch: 3-second hold shows device ID, IP, version; no short-tap action
- [ ] Offline: records card swipe and later syncs once when connected
- [ ] Primary/slave: adoption and permissions enforced
- [ ] Restart: persisted events remain; no duplicated punches
