# RC3 cleanup20: Employees column cleanup

Remove the Manager cards column from the Employees table. Attendance cards, the Manager role column and all employee editing remain available. The dedicated Manager cards page retains card management; no cards, permissions, API fields or database records are changed.

## Validation

CSS lint and the maintained mobile/page-spacing browser check pass. Server builds with -Wall -Wextra -Werror. The previous full suite passed 54/54 after its documented browser rerun; this presentation-only change does not rerun the full suite. No physical Banana Pi acceptance run.

## Upgrade

Extract the source and run sudo ./install-master.sh for a Primary or sudo ./install-slave.sh for a Secondary. No uninstall or database reset is needed.
