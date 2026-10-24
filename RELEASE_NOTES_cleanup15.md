# RC3 cleanup15: Vacation backlog flags and dialog close

- Pending requests whose ending date is before today show an amber Dates have passed flag alongside Waiting. The clock's local date determines this, not the manager browser's timezone. Requests ending today, ongoing requests and future requests are not flagged. Old requests retain Approve and Deny until a manager decides them; nothing expires automatically.
- Keep the top Close button and remove the duplicate bottom Cancel button from Record vacation. Escape and successful-save closure remain available. Reopening resets the form as before.
- Reuse the shared flag and action styles. No CSS rules, schema changes, new tables, automatic decisions or notification changes.

## Validation

All 52 maintained checks pass, including four browser scripts. See validation/cleanup15-suite.json. HTTP checks cover past, future and ending-today flags. Browser checks at 320/390/1280px check the past flag, no future flag, the single Close control, approval leaving Waiting, save, reset and Escape. Shared CSS lint and the all-page spacing sweep remain part of the maintained suite. Server builds with -Wall -Wextra -Werror. No physical Banana Pi acceptance run.

## Upgrade

Extract the source and run sudo ./install-master.sh for a Primary or sudo ./install-slave.sh for a Secondary. No uninstall or database reset is needed.
