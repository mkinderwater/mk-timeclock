# RC5.23

Slave setup reads the installed sibling ui.css, or canonical src/ui.css when running from source. The root duplicate is removed.

Adoption writes its completion marker only after agent and backup services succeed. Check approval can retry service activation using locally saved credentials after a partial failure or setup restart, without redeeming the Primary one-time key again. An in-memory received key also survives a failed configuration write until setup restarts.

Primary requests and service activation no longer hold the page state lock. GET remains responsive. Concurrent setup actions receive a visible 409 response instead of racing.

Update Slaves for these changes. Primary behavior, OLED settings and ruler fixes are unchanged. No hardware deployment performed.
