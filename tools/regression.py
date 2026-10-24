#!/usr/bin/env python3
from pathlib import Path
root=Path(__file__).resolve().parents[1]
s=(root/'src/agent.c').read_text()
assert 'pthread_create(&net_thread' in s
assert 'static _Thread_local sqlite3 *db=NULL;' in s
assert 'static void beep_tick(void)' in s
main=s.split('int main(int argc,')[1]
assert 'curl_easy_perform' not in main
assert 'flush(base,key)' not in main
assert 'refresh_cache(base,key)' not in main
assert 'publish_oled(base,key,clock)' not in main
assert 'capture_frame();' in main
assert 'beep_tick();' in main
assert '-pthread' in (root/'Makefile').read_text()
assert 'LISTEN_PORT' in (root/'check-config.sh').read_text()
print('PASS nonblocking paths and configurable port assertions')

assert 'first_presentation(uid,scan_now)' in main
assert 'presented' not in main
assert 'int queued=pending_count()>0' not in main
assert 'SAVED LOCALLY' in s
assert 'static void process_scan(' in s
assert 'strcmp(argv[7],"none")' in main
rc=(root/'src/rc522.c').read_text()
assert 'static int health(int fd)' in rc
assert 'if(verbose){uint8_t crc[2]' in rc
dis=(root/'src/display_ui.c').read_text()
assert 'shift_frame(d,d->shift);' in dis
assert '9+d->shift,8,"OFF"' not in dis
print('PASS reader recovery, UID anti-repeat, OLED and buzzer optional assertions')
