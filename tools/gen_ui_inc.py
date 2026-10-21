#!/usr/bin/env python3
"""Generate src/ui.inc (stylesheet as a C string, navigation, page macros) from src/ui.css.

Usage: tools/gen_ui_inc.py src/ui.css src/ui.inc VERSION     (or: make ui UI_VERSION=x.y.z)

NAV below is the single list of manager pages. It produces both the navigation markup
and the CSS that highlights the current page, so a page cannot be in one and not the other.
A page uses its key in UI_HEAD("Title","key"); tests fail if a key is not listed here.
"""
import re
import sys

# Every top-level entry is a dropdown: (group label, [(key, href, label), ...]).
NAV = [
    ('Today', [('dashboard', '/', 'Overview'),
               ('presence', '/presence', 'Who is in'),
               ('unknown', '/rejected-cards', 'Unknown cards')]),
    ('Timesheets', [('corrections', '/punch-corrections', 'Punches and corrections'),
                    ('periods', '/pay-periods', 'Pay periods'),
                    ('review-links', '/review-links', 'Timesheet links'),
                    ('time-off', '/time-off-requests', 'Time off requests'),
                    ('export', '/attendance-export', 'Attendance export')]),
    ('People', [('employees', '/employees', 'Employees'),
                ('cards', '/card-status', 'Card status'),
                ('manager-cards', '/manager-cards', 'Manager cards'),
                ('accounts', '/accounts', 'Manager accounts'),
                ('schedules', '/schedules', 'Schedules')]),
    ('Settings', [('payroll', '/payroll', 'Payroll report'),
                  ('holidays', '/holidays', 'Stat holidays'),
                  ('clock-grace', '/clock-grace', 'Late grace and repeat taps'),
                  ('display', '/display', 'Clock display'),
                  ('templates', '/email-templates', 'Email templates'),
                  ('smtp', '/smtp', 'Outgoing email'),
                  ('queue', '/email-queue', 'Email queue'),
                  ('ntp', '/ntp', 'Time sync'),
                  ('tls', '/tls', 'HTTPS certificate')]),
    ('Device', [('health', '/device-health', 'System health'),
                ('backups', '/backups', 'Backup and restore'),
                ('uploads', '/rejected-uploads', 'Rejected uploads'),
                ('diagnostics', '/diagnostics', 'Diagnostics'),
                ('audit', '/audit', 'Audit log'),
                ('clocks', '/clocks', 'Clock management'),
                ('system', '/system', 'System operations')]),
]


def links():
    """Yield (key, href, label, group) for every page in NAV."""
    for group, items in NAV:
        for k, h, l in items:
            yield (k, h, l, group)


def nav_markup():
    parts = []
    for group, items in NAV:
        inner = ''.join(f"<a href='{h}' data-key='{k}'>{l}</a>" for k, h, l in items)
        parts.append(f"<details class='g-{group.lower()}'><summary>{group}</summary><div>{inner}</div></details>")
    return ''.join(parts)


def active_selectors():
    sels = []
    for key, href, _, group in links():
        sels.append(f"body[data-page={key}] .nav a[href='{href}']")
        sels.append(f"body[data-page={key}] .nav .g-{group.lower()}>summary")
    return sels


# Menu behaviour that <details> lacks on its own: one dropdown open at a time, close on an
# outside click or Escape (focus returns to the menu button), and aria-current on the current
# page link. Single quotes only: the script is embedded in a C string.
NAV_JS = ("<script>(function(){var n=document.querySelector('.nav');if(!n)return;"
          "var ds=Array.prototype.slice.call(n.querySelectorAll('details'));"
          "function shut(keep){ds.forEach(function(d){if(d!==keep)d.open=false;});}"
          "ds.forEach(function(d){d.addEventListener('toggle',function(){if(d.open)shut(d);});});"
          "document.addEventListener('click',function(e){if(!n.contains(e.target))shut(null);});"
          "document.addEventListener('keydown',function(e){if(e.key!=='Escape')return;"
          "var o=ds.filter(function(d){return d.open;})[0];if(o){o.open=false;o.querySelector('summary').focus();}});"
          "var k=document.body.getAttribute('data-page');"
          "var a=k&&n.querySelector('a[data-key='+k+']');if(a)a.setAttribute('aria-current','page');"
          # Issue counts: a red number on each page link that needs attention and on its menu group.
          "function count(el,v,t){var c=document.createElement('span');c.className='count';c.textContent=v;c.title=t;"
          "c.setAttribute('aria-label',t);el.append(c);}"
          "function refreshIssues(){n.querySelectorAll('.count').forEach(function(x){x.remove();});"
          "tc.request('/api/v1/admin/issues',{cache:'no-store'}).then(function(r){return r.ok?r.json():null;}).then(function(j){"
          "if(!j||!j.items)return;var sum=new Map();j.items.forEach(function(it){if(!it.count)return;"
          "var l=n.querySelector('a[data-key='+it.key+']');if(!l)return;count(l,it.count,it.text);"
          "var d=l.closest('details');sum.set(d,(sum.get(d)||0)+it.count);});"
          "sum.forEach(function(v,d){count(d.querySelector('summary'),v,v+(v===1?' item needs':' items need')+' attention');});"
          "}).catch(function(){});}"
          "refreshIssues();window.addEventListener('timeclock:issues-changed',refreshIssues);"
          "})();</script>")

# Shared page helpers, loaded on every page (manager and public) before the page script.
#   tc.err(el,e) / tc.ok(el,text): status line in red Error or green Done style; any later text change clears it.
#   tc.flag(text,level): a badge span. level: problem, check, ok, muted.
#   tc.banner(el,level,label,text): page-level issue banner; empty text hides it.
#   tc.ago(seconds): 45s, 12m, 5h, 3d.
#   tc.health(key,value): [level,label] when a System health reading needs attention, else null.
#   tc.CARD / tc.TIMEOUT: the one wording for manager-card approval prompts and timeouts.
UI_JS = ("<script>window.tc=(function(){"
         "function mark(el,cls,m){if(!el)return;el.textContent=m;el.classList.remove('is-err','is-ok');"
         "if(m){el.classList.add(cls);el.dataset.flagText=el.textContent;}else delete el.dataset.flagText;}"
         "new MutationObserver(function(list){list.forEach(function(r){var el=r.target.nodeType===1?r.target:r.target.parentElement;"
         "if(el&&el.dataset&&el.dataset.flagText!==undefined&&el.textContent!==el.dataset.flagText){"
         "el.classList.remove('is-err','is-ok');delete el.dataset.flagText;}});})"
         ".observe(document.documentElement,{childList:true,characterData:true,subtree:true});"
         "function flag(t,l){var s=document.createElement('span');s.className='flag flag-'+l;s.textContent=t;return s;}"
         "function banner(el,l,label,t){if(!el)return;el.replaceChildren();el.hidden=!t;if(!t)return;"
         "el.className='banner banner-'+l;var x=document.createElement('span');x.textContent=t;el.append(flag(label,l),x);}"
         "function ago(s){s=Math.max(0,Math.floor(Number(s)||0));return s<120?s+'s':s<7200?Math.floor(s/60)+'m':"
         "s<172800?Math.floor(s/3600)+'h':Math.floor(s/86400)+'d';}"
         "function text(e){return e&&e.message!==undefined?e.message:String(e);}"
         "function timeoutError(){return Error('The clock took too long to respond. Check the result before trying again.');}"
         "async function request(url,options){var c=new AbortController(),timedOut=false,timer,rejectDeadline,parent=options&&options.signal;"
         "if(parent&&parent.aborted)throw parent.reason||Error('Request cancelled.');"
         "var deadline=new Promise(function(resolve,reject){rejectDeadline=reject;timer=setTimeout(function(){timedOut=true;c.abort();reject(timeoutError());},15000);});"
         "function cancel(){c.abort();rejectDeadline(parent.reason||Error('Request cancelled.'));}if(parent)parent.addEventListener('abort',cancel,{once:true});"
         "try{return await Promise.race([(async function(){var r=await fetch(url,{...options,signal:c.signal});await r.clone().arrayBuffer();return r;})(),deadline]);}"
         "catch(e){if(timedOut)throw timeoutError();throw e;}finally{clearTimeout(timer);if(parent)parent.removeEventListener('abort',cancel);}}"
         "async function api(url,options){var r=await request(url,{...options,cache:'no-store'}),j=await window.tc.json(r);if(!r.ok)throw Error(j.error||'Request failed');return j;}"

         # Device readings that need attention: [level,label] or null. Shared by System health and Diagnostics.
         "function health(k,v){var n=Number(v),has=v!==null&&v!==undefined&&v!==''&&Number.isFinite(n);"
         "if(k==='temperature_millicelsius')return has&&n>=80000?['problem','Too hot']:has&&n>=70000?['check','Hot']:null;"
         "if(k==='storage_available_bytes')return has&&n<268435456?['problem','Nearly full']:has&&n<1073741824?['check','Low']:null;"
         "if(k==='wifi_connected')return v?null:['problem','No network'];"
         "if(k==='wifi_signal_bars')return has&&n<=1?['check','Weak']:null;"
         "if(k==='failed_email')return has&&n>0?['problem','See Email queue']:null;"
         "if(k==='queued_email')return has&&n>20?['check','Backlog']:null;"
         "if(k==='ntp_indicator')return v==='Synchronized'?null:['problem','Punches refused'];"
         "return null;}"
         "return{json:async function(r){var t=await r.text(),j;try{j=JSON.parse(t);}catch(e){throw Error('The clock could not complete this. Try again.');}if(!j||typeof j!=='object')throw Error('The clock could not complete this. Try again.');return j;},err:function(el,e){mark(el,'is-err',text(e));},ok:function(el,m){mark(el,'is-ok',m);},"
         "request:request,api:api,flag:flag,banner:banner,ago:ago,health:health,"
         "time:function(s){var d=new Date(s);return isNaN(d.getTime())?String(s):d.toLocaleTimeString('en-US',{hour:'numeric',minute:'2-digit'});},"
         "datetime:function(s){var d=new Date(s);if(isNaN(d.getTime()))return String(s);return d.toLocaleString('en-CA',{weekday:'short',month:'short',day:'numeric',year:'numeric',hour:'numeric',minute:'2-digit',timeZoneName:'short',hour12:true}).replace('a.m.','AM').replace('p.m.','PM');},"
         "CARD:'Tap your manager card on the clock within 30 seconds.',TIMEOUT:'Time ran out. Try again.'};})();</script>")


def c_lines(text):
    return '\n'.join(' "%s"' % l for l in text.splitlines() if l)


def main():
    css_path, out_path, version = sys.argv[1:4]
    css = open(css_path, encoding='utf-8').read()
    if '"' in css or '\\' in css:
        sys.exit('ui.css must not contain double quotes or backslashes (embedded as a C string)')
    keys = [k for k, *_ in links()]
    if len(keys) != len(set(keys)):
        sys.exit('duplicate page key in NAV')
    body = re.sub(r'/\*.*?\*/', '', css, flags=re.S)
    lines = [l.strip() for l in body.splitlines() if l.strip()]
    css_min = '\n'.join(lines)
    marker = '.nav .active{'
    if css_min.count(marker) != 1:
        sys.exit('ui.css must contain exactly one ".nav .active{" rule')
    css_min = css_min.replace(marker, '.nav .active,\n' + ',\n'.join(active_selectors()) + '{')
    nav = nav_markup()
    # Split the nav string so no C source line gets very long.
    nav_c = ' \\\n '.join('"%s"' % chunk for chunk in re.findall(r'.{1,150}(?:</a>|</details>|$)', nav) if chunk)
    for name, js in (('NAV_JS', NAV_JS), ('UI_JS', UI_JS)):
        if '"' in js or '\\' in js:
            sys.exit(f'{name} must not contain double quotes or backslashes (embedded as a C string)')
    out = f'''/* GENERATED by tools/gen_ui_inc.py from src/ui.css and its NAV table. Do not edit; run make ui. */
#define UI_CSS_VERSION "{version}"
static const char ui_css[]=
{c_lines(css_min)};
/* Static navigation. CSS highlights the current page from <body data-page='...'>; NAV_JS adds menu behaviour. */
#define UI_NAV "<header class='topbar'><div class='topbar-in'><a class='brand' href='/'>mk-timeclock</a><nav class='nav' aria-label='Main'>" \\
 {nav_c} \\
 "</nav><form method='post' action='/logout' class='signout'><button class='secondary'>Sign out</button></form></div></header>" \\
 "{NAV_JS}"
#define UI_DOC(title) "<!doctype html><html lang='en'><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>" \\
 "<meta name='color-scheme' content='light dark'><title>" title " | Timeclock</title><link rel='stylesheet' href='/ui.css?v=" UI_CSS_VERSION "'></head>"
/* Use: UI_HEAD("Employees","employees") ... page body ... UI_FOOT */
#define UI_JS "{UI_JS}"
#define UI_HEAD(title,page) UI_DOC(title) "<body data-page='" page "'>" UI_JS UI_NAV "<main>"
#define UI_HEAD_LOGIN(title) UI_DOC(title) "<body class='login'>" UI_JS "<main>"
#define UI_FOOT "</main></body></html>"
/* The stylesheet holds no data, so it is served before the login check (the login page uses it).
   Cached for a week; bump UI_CSS_VERSION when ui.css or NAV changes. */
static void send_css(int fd){{
 size_t n=sizeof ui_css-1;char h[256];
 int k=snprintf(h,sizeof h,"HTTP/1.1 200 OK\\r\\nContent-Type: text/css; charset=utf-8\\r\\nContent-Length: %zu\\r\\nCache-Control: public, max-age=604800, immutable\\r\\nX-Content-Type-Options: nosniff\\r\\nConnection: close\\r\\n\\r\\n",n);
 if(k<=0||(size_t)k>=sizeof h||send(fd,h,(size_t)k,0)!=k)return;
 /* Blocking socket with the 2 s SO_SNDTIMEO set in main(); loop over partial sends. */
 for(size_t off=0;off<n;){{ssize_t w=send(fd,ui_css+off,n-off,0);if(w<=0)return;off+=(size_t)w;}}
}}
'''
    open(out_path, 'w', encoding='utf-8', newline='\n').write(out)
    print(f'{out_path}: {len(css_min)} bytes of CSS, {len(keys)} nav pages')


if __name__ == '__main__':
    main()
