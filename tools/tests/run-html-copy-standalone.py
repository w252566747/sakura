#!/usr/bin/env python3
"""Test production HTML-copy traversal with portable doubles; no Win32 GUI claim."""
from pathlib import Path
import argparse
import os
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--source', type=Path, default=root / 'sakura_core/cmd/CViewCommander_Clipboard.cpp')
parser.add_argument('--address-sanitizer', action='store_true', help='Requires a host supporting AddressSanitizer')
args = parser.parse_args()
source = args.source.read_text(encoding='utf-8-sig')
start = source.index('static bool AppendHTMLColor(')
end = source.index('\n/*!', source.index('void CViewCommander::Command_COPY_COLOR_HTML(bool bLineNumber)', start))
fixture = Path(__file__).with_name('html-copy-standalone.cpp').read_text(encoding='utf-8-sig')
with tempfile.TemporaryDirectory(prefix='sakura-html-copy-') as tmp:
    cpp = Path(tmp) / 'test.cpp'
    exe = Path(tmp) / 'test'
    cpp.write_text(fixture.replace('// PRODUCTION_BODIES', source[start:end]))
    subprocess.run([os.environ.get('CXX', 'clang++'), '-std=c++17', '-Wall', '-Wextra', '-Werror', '-fsanitize=' + ('address,undefined' if args.address_sanitizer else 'undefined'), '-fno-sanitize-trap=all', '-g', str(cpp), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
