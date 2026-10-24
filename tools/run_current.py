#!/usr/bin/env python3
"""Run the explicit current contract without importing historical tests."""
import argparse,json,subprocess,sys,time
from pathlib import Path
root=Path(__file__).resolve().parents[1]
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--browser',action='store_true',help='Require the Playwright phone checks too')
parser.add_argument('--output',type=Path,help='Write a JSON report')
args=parser.parse_args()
manifest=json.loads((root/'tests/current_suite.json').read_text())
scripts=manifest['scripts']+(['test_rc3_mobile_browser.py','test_rc3_manager_timeoff_browser.py','test_rc3_vacation_stat_browser.py','test_rc3_vacation_history_browser.py'] if args.browser else [])
results=[]
for name in scripts:
    started=time.monotonic()
    try:
        p=subprocess.run([sys.executable,str(root/'tests'/name)],cwd=root,capture_output=True,text=True,timeout=90)
        passed=p.returncode==0;details=(p.stdout+p.stderr).strip()
    except subprocess.TimeoutExpired:
        passed=False;details='Timed out after 90 seconds.'
    result={'script':name,'passed':passed,'seconds':round(time.monotonic()-started,3)}
    if not passed:result['error']=details[-6000:]
    results.append(result)
    print(('PASS ' if passed else 'FAIL ')+name,flush=True)
    if not passed:print(result['error'],flush=True)
report={'passed':sum(r['passed'] for r in results),'total':len(results),'browser_requested':args.browser,'results':results}
if args.output:args.output.write_text(json.dumps(report,indent=2)+'\n')
print(f"{report['passed']}/{report['total']} checks passed.",flush=True)
if not args.browser:print('Browser not run; use --browser with Playwright/Chromium configured.')
sys.exit(0 if report['passed']==report['total'] else 1)
