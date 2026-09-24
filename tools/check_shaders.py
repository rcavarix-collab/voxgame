#!/usr/bin/env python3
"""Syntax/type-checks the HLSL embedded in render.cpp (and any other file
given) with glslangValidator's HLSL front end, since the real D3DCompile
only runs on Windows. Usage: tools/check_shaders.py [file.cpp ...]
Every `static const char* g_*ShaderSrc = "..." ...;` is extracted and each
of its VSMain / PSMain entry points compiled."""
import os, re, subprocess, sys, tempfile

def extract(path):
    src = open(path, encoding='utf-8').read()
    for m in re.finditer(r'static const char\*\s+(g_\w*[Ss]hader\w*)\s*=(.*?);\s*\n', src, re.S):
        name, body = m.group(1), m.group(2)
        # Drop // comments between literals, then join the literals.
        pieces = re.findall(r'"((?:[^"\\]|\\.)*)"', re.sub(r'//[^\n]*', '', body))
        text = ''.join(pieces).encode().decode('unicode_escape')
        yield name, text

def main():
    files = sys.argv[1:] or [os.path.join(os.path.dirname(__file__), '..', 'render.cpp')]
    failed = 0
    for f in files:
        for name, text in extract(f):
            with tempfile.NamedTemporaryFile('w', suffix='.hlsl', delete=False) as t:
                t.write(text)
            for entry, stage in (('VSMain', 'vert'), ('PSMain', 'frag')):
                if entry not in text:
                    continue
                r = subprocess.run(['glslangValidator', '-D', '-S', stage, '-e', entry, '-V', '-o', os.devnull, t.name],
                                   capture_output=True, text=True)
                ok = r.returncode == 0
                failed += not ok
                print(f"{'ok  ' if ok else 'FAIL'} {name}:{entry}")
                if not ok:
                    print('\n'.join('    ' + l for l in (r.stdout + r.stderr).splitlines() if l.strip() and t.name not in l or 'ERROR' in l))
            os.unlink(t.name)
    sys.exit(1 if failed else 0)

main()
