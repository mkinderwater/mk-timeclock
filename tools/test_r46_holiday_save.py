from pathlib import Path
from urllib.parse import urlencode

ROOT=Path(__file__).resolve().parents[1]
SRC=(ROOT/'src/server.c').read_text()
UI=(ROOT/'tools/gen_ui_inc.py').read_text()

def test_holiday_request_limit_accommodates_long_calendar():
    holidays='\n'.join(f'{2000+i//12:04d}-{i%12+1:02d}-01,Alberta Holiday {i+1}' for i in range(180))
    body=urlencode({'skip':'1','calendar':holidays})
    assert len(body)>4096  # regression reproduces rejected legacy request
    assert len(body)<200000
    assert 'holidays ")?200000u' in SRC
    assert 'char csv[160001]' in SRC

def test_empty_list_and_message_handling():
    page=SRC.split('static void holidays_page(int fd){',1)[1].split('/* A system action',1)[0]
    assert "td.textContent='No holidays'" in page
    assert 'if(tr.dataset.empty)continue' in page
    assert 'if(!j.days.length)addRow()' not in page
    assert 'j=await tc.json(r)' in page
    assert 'Use one holiday per line:' not in SRC
    assert 'json:async function(r)' in UI
