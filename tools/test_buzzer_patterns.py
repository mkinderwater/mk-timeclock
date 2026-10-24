from pathlib import Path
s=Path(__file__).resolve().parents[1].joinpath("src/buzzer_patterns.h").read_text()
for p in ("ok","fail","manager","out","late"):
 assert "buzz_"+p+"_durations" in s
a=Path(__file__).resolve().parents[1].joinpath("src/agent.c").read_text()
assert "play_pattern(buzzer,&buzz_fail)" in a
assert "play_pattern(buzzer,&buzz_ok)" in a
assert "play_pattern(buzzer,&buzz_manager)" in a
assert "beep(buzzer," not in a
print("WAV-derived buzzer pattern checks passed")
