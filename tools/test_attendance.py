import subprocess, tempfile, sqlite3, time, urllib.request, json, os, datetime
base=os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
with tempfile.TemporaryDirectory() as tmp:
    db=tmp+'/server.db'; port=18761; key='temporary-test-device-key'
    srv=subprocess.Popen([base+'/timeclock-server',db,str(port)],env={**os.environ,'DEVICE_KEY':key,'TZ':'America/Edmonton'},stdout=subprocess.DEVNULL,stderr=subprocess.PIPE)
    try:
        for i in range(25):
            if os.path.exists(db): break
            time.sleep(.1)
        con=sqlite3.connect(db)
        con.execute("INSERT INTO employees(id,name,email) VALUES(1,'Test Person','worker@example.test')")
        con.execute("INSERT INTO cards(uid,employee_id) VALUES('01020304',1)")
        con.execute("INSERT INTO schedule_profiles(employee_id,pay_type,start_time,end_time,weekday_mask) VALUES(1,'salary','08:00','16:00',127)")
        con.execute("INSERT INTO employees(id,name,email) VALUES(2,'Test Manager','manager@example.test')")
        con.execute("INSERT INTO employee_roles(employee_id,role) VALUES(2,'manager')")
        con.commit()
        # Test local 08:07 and 16:00 on same day; UTC conversion
        for uid,local in [('swipe1','2026-10-12T08:07:00'),('swipe2','2026-10-12T16:00:00')]:
            from zoneinfo import ZoneInfo
            t=datetime.datetime.fromisoformat(local).replace(tzinfo=ZoneInfo('America/Edmonton')).astimezone(datetime.timezone.utc).strftime('%Y-%m-%dT%H:%M:%SZ')
            req=urllib.request.Request('http://127.0.0.1:'+str(port)+'/api/v1/swipes',data=json.dumps({'event_id':uid,'clock_id':'test','card_uid':'01020304','swiped_at':t}).encode(),headers={'X-Device-Key':key,'Content-Type':'application/json'})
            with urllib.request.urlopen(req, timeout=4) as r: print(uid,r.read().decode())
        # Repeat the same event ID; there must be no duplicate notice.
        t=datetime.datetime.fromisoformat('2026-10-12T08:07:00').replace(tzinfo=ZoneInfo('America/Edmonton')).astimezone(datetime.timezone.utc).strftime('%Y-%m-%dT%H:%M:%SZ')
        req=urllib.request.Request('http://127.0.0.1:'+str(port)+'/api/v1/swipes',data=json.dumps({'event_id':'swipe1','clock_id':'test','card_uid':'01020304','swiped_at':t}).encode(),headers={'X-Device-Key':key,'Content-Type':'application/json'})
        with urllib.request.urlopen(req,timeout=4) as r: assert r.status==200
        print('EVENTS',con.execute('SELECT event_id,kind,late_minutes,exception,paired_in_event FROM attendance_events ORDER BY event_id').fetchall())
        print('QUEUED',con.execute("SELECT event_id,recipient FROM email_queue WHERE event_id LIKE 'late:%' ORDER BY event_id").fetchall())
        assert con.execute("SELECT late_minutes FROM attendance_events WHERE event_id='swipe1'").fetchone()[0]==7
        assert con.execute("SELECT kind,paired_in_event FROM attendance_events WHERE event_id='swipe2'").fetchone()==('OUT','swipe1')
        assert con.execute("SELECT count(*) FROM email_queue WHERE event_id LIKE 'late:%'").fetchone()[0]==2
        con.close()
    finally:
        srv.terminate()
        try:srv.communicate(timeout=3)
        except subprocess.TimeoutExpired:srv.kill()
