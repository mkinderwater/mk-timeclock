from pathlib import Path
css = (Path(__file__).resolve().parents[1] / "src/ui.css").read_text()
assert ".diag-table{border-collapse:separate;border-spacing:0;table-layout:fixed}" in css
assert ".diag-table tr>th,.diag-table tr>td{border-bottom:1px solid var(--line)}" in css
assert ".diag-table tr:last-child>th,.diag-table tr:last-child>td{border-bottom:1px solid var(--line)}" in css
