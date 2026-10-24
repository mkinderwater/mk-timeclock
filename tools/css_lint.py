#!/usr/bin/env python3
"""Consistency checks for the shared GUI and source files. Usage: tools/css_lint.py [project root]

Exits non-zero with a list of problems. Run by tests/test_v036_css.py.
"""
import re
import subprocess
import sys
import tempfile
from collections import Counter, defaultdict
from pathlib import Path

TEXT_SUFFIXES = {'.c', '.h', '.inc', '.css', '.py', '.sh', '.md', '.service', '.path', '.example', '.txt'}
TEXT_NAMES = {'Makefile', '.editorconfig', '.gitattributes'}
COLOR = re.compile(r'#[0-9a-fA-F]{3,8}\b|\brgba?\(|\bhsla?\(|\b(white|black)\b')
SPACING_OK = re.compile(r'^(0|1px|2px|auto|\d+vh|var\(--s\d\)|calc\([^;]*var\(--s\d\)[^;]*\))$')


def parse_rules(css):
    """Return (context, selector, [(prop, value)]) for every rule; context is the enclosing @-rule chain."""
    body = re.sub(r'/\*.*?\*/', '', css, flags=re.S)
    rules, stack, i, n = [], [], 0, len(body)
    while i < n:
        j = min([x for x in (body.find('{', i), body.find('}', i)) if x != -1], default=-1)
        if j == -1:
            break
        if body[j] == '}':
            if stack:
                stack.pop()
            i = j + 1
            continue
        head = ' '.join(body[i:j].split())
        close = body.find('}', j + 1)
        inner_open = body.find('{', j + 1)
        if head.startswith('@') and not head.startswith('@font-face'):
            stack.append(re.sub(r'\s*([():,])\s*', r'\1', head))
            i = j + 1
            continue
        if inner_open != -1 and inner_open < close:
            stack.append(re.sub(r'\s*([():,])\s*', r'\1', head))
            i = j + 1
            continue
        decls = []
        for d in body[j + 1:close].split(';'):
            if d.strip():
                prop, _, val = d.partition(':')
                decls.append((prop.strip(), ' '.join(val.split())))
        rules.append((' / '.join(stack), head, decls))
        i = close + 1
    return rules


def split_values(val):
    """Split a CSS value on spaces that are not inside parentheses."""
    parts, depth, cur = [], 0, ''
    for ch in val:
        depth += (ch == '(') - (ch == ')')
        if ch == ' ' and depth == 0:
            if cur:
                parts.append(cur)
            cur = ''
        else:
            cur += ch
    if cur:
        parts.append(cur)
    return parts


def lint_css(css):
    problems = []
    if '"' in css or '\\' in css:
        problems.append('ui.css: contains a double quote or backslash')
    if '!important' in css:
        problems.append('ui.css: !important is not allowed; fix the rule order instead')
    if 'data-page' in re.sub(r'/\*.*?\*/', '', css, flags=re.S):
        problems.append('ui.css: data-page selectors are generated from NAV; remove them from ui.css')
    rules = parse_rules(css)
    exact = Counter()
    setters = defaultdict(list)
    for ctx, sel, decls in rules:
        if ctx.startswith('@keyframes'):
            continue
        exact[(ctx, sel)] += 1
        for one in (s.strip() for s in sel.split(',')):
            for prop, _ in decls:
                setters[(ctx, one, prop)].append(sel)
        for prop, n in Counter(p for p, _ in decls).items():
            if n > 1 and not prop.startswith('-webkit-appearance'):
                problems.append(f'ui.css: "{sel}" sets {prop} {n} times')
        in_root = sel.startswith(':root')
        for prop, val in decls:
            if prop.startswith('--'):
                if not in_root:
                    problems.append(f'ui.css: custom property {prop} defined outside :root in "{sel}"')
                continue
            if COLOR.search(val) and not in_root:
                problems.append(f'ui.css: raw color in "{sel}" {prop}:{val}; use a token')
            if prop == 'font-size' and not re.fullmatch(r'var\(--fs-[a-z]+\)', val):
                problems.append(f'ui.css: font-size {val} in "{sel}"; use a --fs-* token')
            if prop == 'font' or (prop == 'font-family' and val not in ('inherit', 'var(--font)', 'var(--mono)')):
                problems.append(f'ui.css: {prop}:{val} in "{sel}"; use var(--font), var(--mono) or inherit')
            if prop == 'font-weight' and not re.fullmatch(r'var\(--w-[a-z]+\)', val):
                problems.append(f'ui.css: font-weight {val} in "{sel}"; use a --w-* token')
            if prop == 'border-radius' and not re.fullmatch(r'var\(--r(-[a-z]+)?\)|50%', val):
                problems.append(f'ui.css: border-radius {val} in "{sel}"; use a --r* token')
            if prop in ('padding', 'margin', 'gap') or re.fullmatch(r'(padding|margin)-(top|right|bottom|left)', prop):
                for part in split_values(val):
                    if not SPACING_OK.match(part):
                        problems.append(f'ui.css: {prop} value {part} in "{sel}"; use --s1..--s6, 0, 1px, 2px or auto')
    for (ctx, sel), n in exact.items():
        if n > 1:
            problems.append(f'ui.css: rule "{sel}" appears {n} times ({ctx or "top level"}); merge them')
    for (ctx, one, prop), sels in setters.items():
        if len(sels) > 1:
            problems.append(f'ui.css: {prop} for "{one}" is set in {len(sels)} rules ({ctx or "top level"}): '
                            + ' | '.join(sels) + '; keep one')
    return problems


def nav_keys(root):
    sys.path.insert(0, str(root / 'tools'))
    import gen_ui_inc
    return {k: h for k, h, _, _ in gen_ui_inc.links()}


def lint_pages(root):
    problems = []
    # Pages also live in feature includes. Decode C-escaped quotes so both
    # attribute quote styles receive the same markup checks.
    src = {p.name: p.read_text(encoding='utf-8').replace(r'\"', '"')
           for p in sorted((root / 'src').iterdir())
           if p.suffix in ('.c', '.h', '.inc') and p.name != 'ui.inc'}
    keys = nav_keys(root)
    for f, text in src.items():
        if '<style' in text:
            problems.append(f'{f}: page contains a <style> block; use src/ui.css')
        for m in re.finditer(r'''style=(['"])(.*?)\1''', text):
            problems.append(f"{f}: inline style='{m.group(2)}'; use a class in src/ui.css")
        for key in re.findall(r'UI_HEAD\("[^"]*","([\w-]+)"\)', text):
            if key not in keys:
                problems.append(f'{f}: page key "{key}" is not in the NAV table (tools/gen_ui_inc.py)')
        # Buttons in page markup must sit in an .actions row (buttons built in JS for table rows are exempt).
        for m in re.finditer(r'<button\b', text):
            rows = list(re.finditer(r'''<div class=(['"])actions\1>''', text[:m.start()]))
            opened = rows[-1].start() if rows else -1
            if opened == -1 or '</div>' in text[opened:m.start()]:
                line = text.count('\n', 0, m.start()) + 1
                problems.append(f"{f}:{line}: <button> outside <div class='actions'>; wrap it so spacing is consistent")
    all_src = ''.join(src.values())
    for path in re.findall(r'!strcmp\(path,"(/[a-z-]*)"\)\)\{\w+_page\(fd\)', all_src):
        if path not in keys.values() and path not in ('/login','/my-timesheet','/setup'):
            problems.append(f'route {path} has a page but no NAV entry')
    return problems


def lint_files(root):
    problems = []
    for p in sorted(root.rglob('*')):
        rel = p.relative_to(root)
        if any(part in ('__MACOSX', '__pycache__', '.git') for part in rel.parts):
            if '__MACOSX' in rel.parts:
                problems.append(f'{rel}: macOS archive metadata; remove')
            continue
        if p.name in ('.DS_Store', 'Thumbs.db', 'desktop.ini') or p.name.startswith('._'):
            problems.append(f'{rel}: OS metadata file; remove')
            continue
        if not p.is_file() or not (p.suffix in TEXT_SUFFIXES or p.name in TEXT_NAMES):
            continue
        data = p.read_bytes()
        if data.startswith(b'\xef\xbb\xbf'):
            problems.append(f'{rel}: UTF-8 byte order mark (BOM); save without BOM')
        if b'\r' in data:
            problems.append(f'{rel}: Windows (CRLF) or old Mac (CR) line endings; convert to LF')
        if p.suffix in ('.c', '.h', '.inc', '.css', '.py', '.sh') and b'\t' in data and p.name != 'Makefile':
            problems.append(f'{rel}: tab characters; indent with spaces')
    return problems


def lint_generated(root):
    inc = (root / 'src/ui.inc').read_text(encoding='utf-8')
    m = re.search(r'#define UI_CSS_VERSION "([^"]+)"', inc)
    if not m:
        return ['src/ui.inc: missing UI_CSS_VERSION']
    with tempfile.TemporaryDirectory() as t:
        out = Path(t) / 'ui.inc'
        subprocess.run([sys.executable, '-I', str(root / 'tools/gen_ui_inc.py'), str(root / 'src/ui.css'), str(out), m.group(1)],
                       check=True, stdout=subprocess.DEVNULL)
        if out.read_text(encoding='utf-8') != inc:
            return ['src/ui.inc is out of date with src/ui.css or NAV; run make ui']
    return []


def run(root):
    root = Path(root).resolve()
    return (lint_css((root / 'src/ui.css').read_text(encoding='utf-8')) + lint_pages(root)
            + lint_files(root) + lint_generated(root))


if __name__ == '__main__':
    found = run(sys.argv[1] if len(sys.argv) > 1 else Path(__file__).resolve().parents[1])
    for line in found:
        print(line)
    print(f'{len(found)} problem(s)' if found else 'css_lint: clean')
    sys.exit(1 if found else 0)
