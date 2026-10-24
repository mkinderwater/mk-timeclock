# RC3 cleanup13: Overnight shift attribution

A completed shift belongs entirely to the pay period containing its IN date in the clock's local timezone. Sarah Chen's September 15 at 22:00 to September 16 at 06:00 shift contributes eight hours to September 1–15 and none to September 16–30. Do not split hours or manufacture midnight punches. Missing OUTs remain exceptions. Actual elapsed timestamps determine duration across daylight-saving transitions.

This rule is now explicit in README.md and docs/OPERATIONS.md. Existing application logic already follows it; no attendance calculation or database schema changes were needed.

## Cause of the reported failures

Both tests inserted 22:00 UTC while inheriting the host timezone. Under Europe/Amsterdam in September, that IN is 00:00 on the following local date. Excluding it from the previous period was correct. The database retained the eight-hour pair; the test queried the wrong local period. These were timezone-dependent fixture failures, not missing stored punches.

The fixtures now define local wall times, explicitly set the server timezone, and convert punches to UTC before storing or submitting them. The default is America/Edmonton (Calgary). TEST_CLOCK_TZ allows independent UTC and Amsterdam runs. Expected hours, entry counts, correction assertions, closed-period checks and immutable/revised export checks remain intact.

## Validation

Both regression scripts pass in America/Edmonton, Etc/UTC and Europe/Amsterdam, including checks after their formerly failing assertions. The maintained suite passes 51/51 including all four browser scripts; see validation/cleanup13-suite.json. Server compiles with -Wall -Wextra -Werror. Development verification does not replace physical Banana Pi acceptance.

## Upgrade

This release changes documentation, test fixtures and the version identifier. No operational fix is required for the reported test failures. For an ordinary Primary upgrade, extract the source and run sudo ./install-master.sh. No uninstall or database reset is needed.
