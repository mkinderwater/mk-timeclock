# RC3 cleanup14: CSS and spacing checks

- Audit all 29 manager pages at 320px, 390px and 1280px: 87 page/viewport checks pass, with no viewport overflow, buttons outside action/table rows, crowded action rows or browser script errors.
- Extend the maintained mobile browser check to repeat this full navigation sweep. Existing checks retain word/date wrapping, table buttons, employee editor alignment, primary actions, status borders, vacation controls and employee portal coverage. Vacation browser tests retain dialog and history checks.
- Extend CSS lint from four hardcoded source files to all C/header/include page sources, excluding generated ui.inc. Check both attribute quote styles, including escaped C quotes. Add the shared lint check to the maintained suite.
- Remove an unused inline order style from employee vacation history. That section uses normal document flow; the existing pending-history DOM ordering remains unchanged. Shared styling remains in ui.css, with no visual redesign.
- No schema, payroll calculation or attendance behavior changes.

## Validation

All 52 maintained checks pass, including four browser scripts. See validation/cleanup14-ui-spacing.json and validation/cleanup14-suite.json. The Record vacation dialog was visually reviewed at 390px. Source preflight and CSS lint pass; server builds with -Wall -Wextra -Werror. Browser checks use headless Chromium, not physical phone or Banana Pi acceptance.

## Upgrade

Extract the source and run sudo ./install-master.sh for a Primary or sudo ./install-slave.sh for a Secondary. No uninstall or database reset is needed.
