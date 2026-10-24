from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def test_manager_override_badges_are_appended_as_nodes():
    source = (ROOT / 'src/reconciliation.inc').read_text()
    assert "tc.flag('Approved','ok'),tc.flag('Not confirmed by employee','check')" in source
    assert "td.append(...[].concat(v))" in source
    assert 'for(let v of cells){let td=document.createElement(\'td\');td.append(v);' not in source
