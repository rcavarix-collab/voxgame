#!/usr/bin/env python3
"""Syntax/type-checks the HLSL in shaders.h with glslangValidator's HLSL
front end (the real D3DCompile only runs on Windows).
Usage: tools/check_shaders.py [file ...]
Every `static const char* g_*ShaderSrc = R"( ... )";` is extracted and each
VSMain / PSMain entry point compiled."""
import os, re, subprocess, sys, tempfile

def extract(path):
    src = open(path, encoding='utf-8').read()
    for m in re.finditer(r'static const char\*\s+(g_\w*[Ss]hader\w*)\s*=\s*R"\((.*?)\)";', src, re.S):
        yield m.group(1), m.group(2)

def main():
    files = sys.argv[1:] or [os.path.join(os.path.dirname(__file__), '..', 'shaders.h')]
    failed = found = 0
    for f in files:
        for name, text in extract(f):
            found += 1
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
                    print('\n'.join('    ' + l for l in (r.stdout + r.stderr).splitlines() if l.strip()))
            os.unlink(t.name)
    if not found:
        print("FAIL no shaders found (extraction problem?)")
        failed += 1
    sys.exit(1 if failed else 0)

main()
