from pathlib import Path

root = Path(__file__).resolve().parents[1]
agent = (root / "src/agent.c").read_text()
ui = (root / "src/display_ui.c").read_text()

def test_boot_logo_is_first_content_frame():
    start = ui.index("int display_open(")
    stop = ui.index("void display_boot_logo(", start)
    opening = ui[start:stop]
    assert "display_boot_logo(d);" in opening
    assert "display_idle(d);" not in opening

def test_logo_then_idle_without_boot_device_info():
    start = agent.index("/* display_open() has already sent the boot logo")
    stop = agent.index('fprintf(stderr,"Clock started:', start)
    startup = agent[start:stop]
    assert "j<20" in startup
    assert "display_idle_status(&disp,0,0);" in startup
    assert "display_diagnostics" not in startup
    assert "startup_ipv4" not in agent

if __name__ == "__main__":
    test_boot_logo_is_first_content_frame()
    test_logo_then_idle_without_boot_device_info()
    print("boot-first regression passed")
