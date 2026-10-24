CC ?= cc
CFLAGS ?= -O2 -Wall -Wextra -Werror -std=c11 -D_POSIX_C_SOURCE=200809L
FREETYPE_CFLAGS := $(shell pkg-config --cflags freetype2 2>/dev/null)
FREETYPE_LIBS := $(shell pkg-config --libs freetype2 2>/dev/null)

.PHONY: all clean check-deps check-i2c ui lint
all: timeclock-server timeclock-agent timeclock-mailer

timeclock-server: src/server.c src/version.h src/payroll_timesheet.h src/employee_number.h src/period_approval.h src/vacation_usage.h src/send_schedule.h src/schedule.inc src/payroll.inc src/payroll_dates.h src/attendance.inc src/email_render.h src/admin_ops.inc src/reconciliation.inc src/account_review.inc src/time_off.inc src/issues.inc src/note_link.inc src/ui.inc src/oled.h src/wifi_signal.h
	$(CC) $(CFLAGS) -o $@ src/server.c -lsqlite3 -lcrypt

check-deps:
	@pkg-config --exists freetype2 || { echo 'Missing FreeType headers: install libfreetype-dev (Debian)'; exit 1; }

timeclock-agent: src/version.h src/boot_logo.h src/buzzer_patterns.h src/agent.c src/rc522.c src/rc522.h src/oled.c src/oled.h src/oled_ttf.c src/oled_ttf.h src/oled_bitmap.c src/oled_bitmap.h src/display_ui.c src/display_ui.h src/wifi_signal.h
	@$(MAKE) --no-print-directory check-deps
	$(CC) $(CFLAGS) $(FREETYPE_CFLAGS) -o $@ src/agent.c src/rc522.c src/oled.c src/oled_ttf.c src/oled_bitmap.c src/display_ui.c -lsqlite3 -lcurl -pthread $(FREETYPE_LIBS)

check-i2c:
	$(CC) $(CFLAGS) -c src/oled.c -o /tmp/timeclock-oled.o
	@rm -f /tmp/timeclock-oled.o

clean:
	rm -f timeclock-server timeclock-agent timeclock-mailer

timeclock-mailer: src/mailer.c src/payroll_mail.inc src/payroll_timesheet.h src/employee_number.h src/period_approval.h src/vacation_usage.h src/send_schedule.h src/payroll_dates.h src/email_render.h
	$(CC) $(CFLAGS) -o $@ src/mailer.c -lsqlite3 -lcurl


# Regenerate src/ui.inc after editing src/ui.css or the NAV table in tools/gen_ui_inc.py (bump UI_VERSION so browsers refetch).
UI_VERSION ?= 1.9.5-RC3-cleanup10
ui:
	python3 tools/gen_ui_inc.py src/ui.css src/ui.inc $(UI_VERSION)

# Style, layout and line-ending consistency checks (also run by the test suite).
lint:
	python3 tools/css_lint.py .
