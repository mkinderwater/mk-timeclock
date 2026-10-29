# RC5.24

- Check approval authenticates one heartbeat before activating services and writing the adoption marker. An explicit 401/403 clears the rejected key and enables Send request. Network, TLS and server failures retain credentials for retry.
- Ruler labels use a uniform stride from the first label, without forcing an uneven final label.
- Re-adoption: if a revoked Slave is returned to setup with its adoption marker removed, select Check approval, then generate a new Primary code and Send request after rejection. Deleting the marker alone does not authorize the old key.

Validation: setup rejection/retry tests, partial-service recovery tests, production JavaScript geometry fixtures and UI generator encoding checks passed. Browser layout and hardware were not tested. Full compilation was blocked by missing SQLite and FreeType development dependencies in this environment.
