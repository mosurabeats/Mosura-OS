#!/usr/bin/env python3
"""Bundle the simulator into one self-contained HTML file (wasm inlined),
so it opens from disk or can be hosted as a single page.
    python3 tools/bundle_sim.py  ->  build/mosura-sim.html"""
import base64, os, re
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
S = lambda *p: os.path.join(ROOT, *p)

html = open(S('sim', 'index.html')).read()
audio = open(S('sim', 'audio.js')).read().replace('export class', 'class')
sim = open(S('sim', 'sim.js')).read()
sim = re.sub(r"^import .*?;\n", "", sim, flags=re.M).replace('export async function start', 'async function start')
wasm = base64.b64encode(open(S('sim', 'mosura.wasm'), 'rb').read()).decode()
script = ("<script>window.MOSURA_WASM_B64 = '%s';</script>\n<script type=\"module\">\n%s\n%s\n"
          "start().catch((e) => { const n = document.getElementById('note'); n.textContent = 'Failed to start: ' + e.message; n.classList.add('show'); });\n</script>"
          % (wasm, audio, sim))
html = re.sub(r'<script type="module">.*?</script>', lambda m: script, html, flags=re.S)
os.makedirs(S('build'), exist_ok=True)
open(S('build', 'mosura-sim.html'), 'w', newline='\n').write(html)
print('wrote build/mosura-sim.html (%d KB)' % (len(html) // 1024))

# Artifact flavour: the host adds the document skeleton itself.
import re as _re
art = html
for pat in [r'<!doctype html>\s*', r'<html[^>]*>\s*', r'</html>\s*', r'<head>\s*', r'</head>\s*', r'<body>\s*', r'</body>\s*',
            r'<meta charset="utf-8">\s*', r'<meta name="viewport"[^>]*>\s*']:
    art = _re.sub(pat, '', art, flags=_re.I)
open(S('build', 'mosura-sim-artifact.html'), 'w', newline='\n').write(art)
print('wrote build/mosura-sim-artifact.html')
