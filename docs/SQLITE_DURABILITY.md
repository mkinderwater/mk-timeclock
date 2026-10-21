# SQLite durability policy (v0.9.112)

- `server.db`: WAL + `synchronous=NORMAL` on the server and mailer connections. This is connection-local and must be set by every writer. SQLite WAL protects database consistency against power loss, but recent committed transactions may be lost on sudden power failure.
- `/var/lib/timeclock/queue.db`: WAL + `synchronous=FULL` on both the RFID/main and network worker connections, since it is the only copy of unsent swipes. Do not change this to NORMAL without stronger write-ahead storage guarantees.
- Python administrator scripts retain default SQLite synchronous behavior; changes to them are not part of this release.
- Backups stop writers before archiving the SQLite files; never copy live WAL databases without a SQLite backup API or a coordinated checkpoint.
- Follow-up: test sudden power loss and verify admin edits and acknowledged swipe recovery.

Clock HTTP acknowledgements use FULL for swipe, unknown, and rejected event writes; the server returns to NORMAL afterward. The agent queue stays on FULL.
