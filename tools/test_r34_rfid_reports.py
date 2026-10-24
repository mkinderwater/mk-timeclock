from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]

def test_rfid_is_in_both_reporting_formats():
    src=(ROOT/'src'/'payroll_timesheet.h').read_text()
    export=(ROOT/'src'/'admin_ops.inc').read_text()
    assert 'Employee,RFID ID,Date' in src
    assert 'RFID ID: %s' in src
    assert 'FROM swipes s WHERE s.event_id=i.id' in src
    assert 'card_uid,first_name,last_name,position' in export
