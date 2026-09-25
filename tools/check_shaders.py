#!/usr/bin/env python3
"""Syntax/type-checks the HLSL embedded in render.cpp (and any other file
given) with glslangValidator's HLSL front end, since the real D3DCompile
only runs on Windows. Usage: tools/check_shaders.py [file.cpp ...]
Every `static const char* g_*ShaderSrc = "..." ...;` is extracted and each
of its VSMain / PSMain entry points compiled."""
import os, re, subprocess, sys, tempfile

def extract(path):
    src = open(path, encoding='utf-8').read()
    for m in re.finditer(r'static const char\*\s+(g_\w*(?:[Ss]hader|atmosphere)\w*)\s*=(.*?);\s*\n', src, re.S):
        name, body = m.group(1), m.group(2)
        # Walk literals and C++ comments together, so a // inside an HLSL
        # string isn't mistaken for a C++ comment (and vice versa).
        pieces = [m.group(1) for m in re.finditer(r'"((?:[^"\\]|\\.)*)"|//[^\n]*', body) if m.group(1) is not None]
        text = ''.join(pieces).encode().decode('unicode_escape')
        yield name, text

def main():
    files = sys.argv[1:] or [os.path.join(os.path.dirname(__file__), '..', 'render.cpp')]
    failed = 0
    for f in files:
        shaders = list(extract(f))
        # Shaders marked "// uses atmosphere" get g_atmosphereSrc prepended,
        # exactly as render.cpp composes them at compile time.
        prelude = dict(shaders).get('g_atmosphereSrc', '')
        for name, text in shaders:
            if name == 'g_atmosphereSrc':
                continue
            if '// uses atmosphere' in text:
                text = prelude + text
            with tempfile.NamedTemporaryFile('w', suffix='.hlsl', delete=False) as t:
                t.write(text)
            if 'VSMain' not in text and 'PSMain' not in text:
                print(f"FAIL {name}: no VSMain/PSMain found (extraction problem?)")
                failed += 1
            variants = [('', [])]
            if '#ifndef NO_SHADOWS' in text:
                variants.append((' [NO_SHADOWS]', ['-DNO_SHADOWS']))
            for entry, stage in (('VSMain', 'vert'), ('PSMain', 'frag')):
              for label, defines in variants:
                if entry not in text:
                    continue
                r = subprocess.run(['glslangValidator', '-D', *defines, '-S', stage, '-e', entry, '-V', '-o', os.devnull, t.name],
                                   capture_output=True, text=True)
                ok = r.returncode == 0
                failed += not ok
                print(f"{'ok  ' if ok else 'FAIL'} {name}:{entry}{label}")
                if not ok:
                    print('\n'.join('    ' + l for l in (r.stdout + r.stderr).splitlines() if l.strip() and t.name not in l or 'ERROR' in l))
            os.unlink(t.name)
    sys.exit(1 if failed else 0)

main()
