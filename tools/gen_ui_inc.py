#!/usr/bin/env python3
"""Generate src/ui.inc (stylesheet as a C string, navigation, page macros) from src/ui.css.

Usage: tools/gen_ui_inc.py src/ui.css src/ui.inc VERSION     (or: make ui UI_VERSION=x.y.z)

NAV below is the single list of manager pages. It produces both the navigation markup
and the CSS that highlights the current page, so a page cannot be in one and not the other.
A page uses its key in UI_HEAD("Title","key"); tests fail if a key is not listed here.
"""
import re
import json
import sys

# One page catalog: Work renders as direct links; utility groups render in Settings.
NAV = [('Work', [('dashboard', '/', 'Overview'), ('corrections', '/punch-corrections', 'Attendance'), ('employees', '/employees', 'Employees'), ('periods', '/pay-periods', 'Pay periods'), ('time-off', '/time-off-requests', 'Vacation')]), ('Company', [('settings', '/settings', 'Settings'), ('period-schedule', '/pay-periods#periodSettings', 'Attendance period schedule'), ('schedules', '/schedules', 'Bulk schedules'), ('payroll', '/payroll', 'Report delivery'), ('holidays', '/holidays', 'Stat holidays'), ('review-links', '/review-links', 'Employee emails and links'), ('accounts', '/accounts', 'Manager accounts'), ('manager-cards', '/manager-cards', 'Manager cards')]), ('Email', [('templates', '/email-templates', 'Email templates'), ('smtp', '/smtp', 'Outgoing email'), ('queue', '/email-queue', 'Email queue')]), ('Clock', [('clock-grace', '/clock-grace', 'Late grace and repeat taps'), ('display', '/display', 'Clock display'), ('ntp', '/ntp', 'Time sync'), ('tls', '/tls', 'HTTPS certificate'), ('health', '/device-health', 'System health'), ('backups', '/backups', 'Backup and restore'), ('clocks', '/clocks', 'Clock management'), ('system', '/system', 'System operations')]), ('Audit', [('unknown', '/rejected-cards', 'Unknown cards'), ('uploads', '/rejected-uploads', 'Rejected uploads'), ('export', '/attendance-export', 'Raw swipe export'), ('diagnostics', '/diagnostics', 'Diagnostics'), ('audit', '/audit', 'Audit log')])]


def links():
    """Yield (key, href, label, group) for every page in NAV."""
    for group, items in NAV:
        for k, h, l in items:
            yield (k, h, l, group)


def nav_markup():
    items = NAV[0][1] + [('settings', '/settings', 'Settings')]
    return ''.join(f"<a href='{h}' data-key='{k}'>{l}</a>" for k,h,l in items)


def active_selectors():
    return [f"body[data-page={key}] .nav a[href='{href if group == 'Work' else '/settings'}']" for key,href,_,group in links()]


# Page catalog also powers contextual issue links and the Settings hub.
PAGE_MAP = {k: {'href':h, 'label':label, 'group':group} for k,h,label,group in links()}
# Menu behaviour, part of /ui.js. UI_NAV calls tc.nav() right after the menu markup.
NAV_JS = ("function nav(){var n=document.querySelector('.nav');if(!n)return;"
          "var key=document.body.dataset.page,link=n.querySelector('a[data-key='+key+']')||n.querySelector('a[data-key=settings]');"
          "if(link)link.setAttribute('aria-current','page');"
          "function refresh(force){issues(force).then(function(j){n.querySelectorAll('.count').forEach(function(x){x.remove();});var counts=new Map();"
          "(j.items||[]).forEach(function(it){if(!it.count)return;var a=n.querySelector('a[data-key='+it.key+']')||n.querySelector('a[data-key=settings]');counts.set(a,(counts.get(a)||0)+it.count);});"
          "counts.forEach(function(count,a){if(!a)return;var b=document.createElement('span');b.className='count';b.textContent=count;b.setAttribute('aria-label',count+' items need attention');a.append(b);});"
          "}).catch(function(){});}refresh();window.addEventListener('timeclock:issues-changed',function(){refresh(true);});}")

# Shared page helpers, loaded on every page (manager and public) before the page script.
#   tc.err(el,e) / tc.ok(el,text): status line in red Error or green Done style; any later text change clears it.
#   tc.flag(text,level): a badge span. level: problem, check, ok, muted.
#   tc.banner(el,level,label,text): page-level issue banner; empty text hides it.
#   tc.ago(seconds): 45s, 12m, 5h, 3d.
#   tc.health(key,value): [level,label] when a System health reading needs attention, else null.
#   tc.hm(minutes): 8:02 AM from minutes after local midnight (wraps past midnight).
#   tc.strip({from,to,label,marks:[{s,e,c,t}]}): a shift strip. c is sched, work, open, long, late, due,
#     miss or now; s/e are minutes after local midnight, clipped to from..to; t is the hover text.
#   tc.range(minutes,lo,hi): [from,to] covering the minutes and lo..hi, snapped to the ruler step.
#   tc.axis(from,to): the hour ruler drawn above a column of strips.
#   tc.legend([[c,label],...]): the strip marks with words.
#   tc.issues(force): the shared /api/v1/admin/issues reply. Calls within 5 seconds share one request;
#     force (or the timeclock:issues-changed event) fetches again. Nav counts and page warnings use it.
#   tc.date(value,style): one date format. style: undefined 2026-10-29, 'day' Thu, Oct 29, 'short' Oct 29,
#     'long' Thursday, October 29, 2026. A bare YYYY-MM-DD is read as that local date.
#   tc.ask({title,text,ok,danger}) -> Promise<boolean>; tc.reason({title,text,label,min,value,ok,danger})
#     -> Promise<string|null>. One in-page dialog for confirmations and required reasons (no browser popups).
#   tc.CARD / tc.TIMEOUT: the one wording for manager-card approval prompts and timeouts.
#   tc.card(url,token): poll a manager-card status endpoint every 700 ms; resolves with the final
#     reply, rejects with tc.TIMEOUT when it expires or after 45 polls (about 32 seconds).
UI_JS = ("window.tc=(function(){"
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
         "async function card(url,token){for(var i=0;i<45;i++){await new Promise(function(r){setTimeout(r,700);});"
         "var x=await api(url,{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams({token:token})});"
         "if(x.status==='expired')break;if(x.status!=='waiting')return x;}throw Error(window.tc.TIMEOUT);}"

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
         # Shift strip: one day drawn against an hour ruler. Single quotes only (embedded in C).
         "function hm(m){m=((Math.round(m)%1440)+1440)%1440;var h=Math.floor(m/60),x=m%60;"
         "return (h%12||12)+':'+String(x).padStart(2,'0')+(h<12?' AM':' PM');}"
         "function hour(m){var h=((Math.round(m/60)%24)+24)%24;return (h%12||12)+(h<12?' AM':' PM');}"
         "function strip(o){var d=document.createElement('div'),span=Math.max(1,o.to-o.from);d.className='strip';"
         "d.setAttribute('role','img');d.setAttribute('aria-label',o.label||'');"
         "(o.marks||[]).forEach(function(k){var x=document.createElement('span'),s=Math.max(k.s,o.from),e=k.c==='now'?s:Math.min(k.e,o.to);"
         "if(k.c==='now'?(k.s<o.from||k.s>o.to):e<=s)return;x.className='strip-'+k.c;x.style.left=((s-o.from)/span*100)+'%';"
         "if(k.c!=='now')x.style.width=((e-s)/span*100)+'%';if(k.t)x.title=k.t;d.append(x);});return d;}"
         "function step(span){return span>720?180:span>420?120:60;}"
         "function range(list,lo,hi){var a=Math.min.apply(null,list.concat([lo])),b=Math.max.apply(null,list.concat([hi])),k=step(b-a);"
         "return [Math.max(0,Math.floor(a/k)*k),Math.min(1440,Math.ceil(b/k)*k)];}"
         "function axis(from,to){var d=document.createElement('div'),span=Math.max(1,to-from),k=step(span);"
         "d.className='strip-axis';d.setAttribute('aria-hidden','true');"
         "for(var m=Math.ceil(from/k)*k;m<=to;m+=k){var x=document.createElement('span');x.textContent=hour(m);x.style.left=((m-from)/span*100)+'%';"
         "if(m===from)x.className='is-start';else if(m===to)x.className='is-end';d.append(x);}"
         "function fit(){var labels=Array.from(d.children);labels.forEach(function(x){x.hidden=false;});if(!d.clientWidth||!labels.length)return;"
         "var boxes=labels.map(function(x){return x.getBoundingClientRect();}),stride=1;"
         "for(;stride<labels.length;stride++){var right=-Infinity,ok=true;"
         "for(var i=0;i<labels.length;i+=stride){if(boxes[i].left<right+8){ok=false;break;}right=boxes[i].right;}if(ok)break;}"
         "labels.forEach(function(x,i){x.hidden=i%stride!==0;});}"
         "requestAnimationFrame(fit);var observer=new ResizeObserver(function(){if(!d.isConnected){observer.disconnect();return;}fit();});observer.observe(d);"
         "if(document.fonts)document.fonts.ready.then(fit);return d;}"
         "function legend(items){var d=document.createElement('div');d.className='strip-legend';"
         "items.forEach(function(it){var s=document.createElement('span'),i=document.createElement('i');i.className='strip-'+it[0];"
         "i.setAttribute('aria-hidden','true');s.append(i,it[1]);d.append(s);});return d;}"
         "function el(tag,text,cls){var e=document.createElement(tag);if(cls)e.className=cls;if(text!==undefined&&text!==null)e.append(text);return e;}"
         "function date(v,style){if(v===undefined||v===null||v==='')return '';var d=v instanceof Date?v:new Date(typeof v==='string'&&v.length===10&&v[4]==='-'?v+'T12:00:00':v);"
         "if(isNaN(d.getTime()))return String(v);var o=style==='day'?{weekday:'short',month:'short',day:'numeric'}:style==='short'?{month:'short',day:'numeric'}:"
         "style==='long'?{weekday:'long',month:'long',day:'numeric',year:'numeric'}:undefined;return d.toLocaleDateString('en-CA',o);}"
         "var issuesPromise=null,issuesAt=0;function issues(force){var now=Date.now();if(force||!issuesPromise||now-issuesAt>5000){issuesAt=now;"
         "issuesPromise=api('/api/v1/admin/issues').catch(function(e){issuesPromise=null;throw e;});}return issuesPromise;}"
         "function ask(o,input){o=o||{};return new Promise(function(resolve){var d=el('dialog',null,'tc-ask'),f=el('form'),msg=el('p'),box=null,"
         "bar=el('div',null,'actions'),no=el('button',o.cancel||'Cancel','secondary'),yes=el('button',o.ok||'OK',o.danger?'danger':'primary'),done=false;"
         "msg.setAttribute('role','status');no.type='button';yes.type='submit';f.append(el('h2',o.title||'Confirm'));if(o.text)f.append(el('p',o.text));"
         "if(input){box=el('textarea');box.maxLength=o.max||240;box.rows=3;if(o.value)box.value=o.value;var l=el('label',o.label||'Reason');l.append(box);f.append(l);}"
         "bar.append(no,yes);f.append(msg,bar);d.append(f);document.body.append(d);"
         "function finish(v){if(done)return;done=true;d.close();d.remove();resolve(v);}"
         "no.onclick=function(){finish(input?null:false);};d.addEventListener('cancel',function(e){e.preventDefault();finish(input?null:false);});"
         "f.onsubmit=function(e){e.preventDefault();if(!input){finish(true);return;}var t=box.value.trim(),min=o.min||0;"
         "if(t.length<min){mark(msg,'is-err','Enter at least '+min+' characters.');box.focus();return;}finish(t);};"
         "d.showModal();(box||yes).focus();});}"
         "var PAGES="+json.dumps(PAGE_MAP,separators=(',',':'))+";"+NAV_JS+
         "return{json:async function(r){var t=await r.text(),j;try{j=JSON.parse(t);}catch(e){throw Error('The clock could not complete this. Try again.');}if(!j||typeof j!=='object')throw Error('The clock could not complete this. Try again.');return j;},err:function(el,e){mark(el,'is-err',text(e));},ok:function(el,m){mark(el,'is-ok',m);},"
         "request:request,api:api,card:card,flag:flag,banner:banner,ago:ago,health:health,hm:hm,strip:strip,axis:axis,range:range,legend:legend,"
         "date:date,issues:issues,nav:nav,ask:function(o){return ask(o,false);},reason:function(o){return ask(o,true);},pages:PAGES,"
         "time:function(s){var d=new Date(s);return isNaN(d.getTime())?String(s):d.toLocaleTimeString('en-US',{hour:'numeric',minute:'2-digit'});},"
         "datetime:function(s){var d=new Date(s);if(isNaN(d.getTime()))return String(s);return d.toLocaleString('en-CA',{weekday:'short',month:'short',day:'numeric',year:'numeric',hour:'numeric',minute:'2-digit',timeZoneName:'short',hour12:true}).replace('a.m.','AM').replace('p.m.','PM');},"
         "CARD:'Tap your manager card on the clock within 30 seconds.',TIMEOUT:'Time ran out. Try again.'};})();")


def c_lines(text):
    return '\n'.join(' "%s"' % l for l in text.splitlines() if l)


def js_lines(text, width=160):
    """Emit adjacent C literals without splitting JavaScript quoted strings.

    Width is a target, not a limit: an entire JS string stays on one C line.
    Escapes are consumed with their following character.
    """
    chunks = []
    start = 0
    quote = None
    escaped = False
    for i, char in enumerate(text):
        if quote:
            if escaped:
                escaped = False
            elif char == '\\':
                escaped = True
            elif char == quote:
                quote = None
        elif char in ("'", '"', '`'):
            quote = char
        if quote is None and char in ';},' and i + 1 - start >= width:
            chunks.append(text[start:i + 1])
            start = i + 1
    if start < len(text):
        chunks.append(text[start:])
    return '\n'.join(' ' + json.dumps(chunk, ensure_ascii=False) for chunk in chunks)


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
    js_c = js_lines(UI_JS)
    out = f'''/* GENERATED by tools/gen_ui_inc.py from src/ui.css and its NAV table. Do not edit; run make ui. */
#define UI_CSS_VERSION "{version}"
static const char ui_css[]=
{c_lines(css_min)};
/* Shared page script (tc helpers, page catalog, menu behaviour), served as /ui.js with the stylesheet's caching. */
static const char ui_js[]=
{js_c};
/* Static navigation. CSS highlights the current page from <body data-page='...'>; tc.nav() adds menu behaviour. */
#define UI_NAV "<header class='topbar'><div class='topbar-in'><a class='brand' href='/'>mk-timeclock</a><nav class='nav' aria-label='Main'>" \\
 {nav_c} \\
 "</nav><form method='post' action='/logout' class='signout'><button class='secondary'>Sign out</button></form></div></header>" \\
 "<script>tc.nav()</script>"
#define UI_DOC(title) "<!doctype html><html lang='en'><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>" \\
 "<meta name='color-scheme' content='light dark'><title>" title " | Timeclock</title><link rel='stylesheet' href='/ui.css?v=" UI_CSS_VERSION "'></head>"
/* Use: UI_HEAD("Employees","employees") ... page body ... UI_FOOT */
#define UI_JS "<script src='/ui.js?v=" UI_CSS_VERSION "'></script>"
#define UI_HEAD(title,page) UI_DOC(title) "<body data-page='" page "'>" UI_JS UI_NAV "<main>"
#define UI_HEAD_LOGIN(title) UI_DOC(title) "<body class='login'>" UI_JS "<main>"
#define UI_FOOT "</main></body></html>"
/* The stylesheet and shared script hold no data, so they are served before the login check (the login
   and employee pages use them). Cached for a week; bump UI_CSS_VERSION when ui.css, NAV or UI_JS changes. */
static void send_static(int fd,const char*type,const char*body,size_t n){{
 char h[256];
 int k=snprintf(h,sizeof h,"HTTP/1.1 200 OK\\r\\nContent-Type: %s\\r\\nContent-Length: %zu\\r\\nCache-Control: public, max-age=604800, immutable\\r\\nX-Content-Type-Options: nosniff\\r\\nConnection: close\\r\\n\\r\\n",type,n);
 if(k<=0||(size_t)k>=sizeof h||send(fd,h,(size_t)k,0)!=k)return;
 /* Blocking socket with the 2 s SO_SNDTIMEO set in main(); loop over partial sends. */
 for(size_t off=0;off<n;){{ssize_t w=send(fd,body+off,n-off,0);if(w<=0)return;off+=(size_t)w;}}
}}
static void send_css(int fd){{send_static(fd,"text/css; charset=utf-8",ui_css,sizeof ui_css-1);}}
static void send_js(int fd){{send_static(fd,"text/javascript; charset=utf-8",ui_js,sizeof ui_js-1);}}
'''
    open(out_path, 'w', encoding='utf-8', newline='\n').write(out)
    print(f'{out_path}: {len(css_min)} bytes of CSS, {len(UI_JS)} bytes of JS, {len(keys)} nav pages')


if __name__ == '__main__':
    main()
