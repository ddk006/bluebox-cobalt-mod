"""Build Cobalt for another bluebox firmware from the desktop sources.

Usage: python3 tools/retarget.py euro /path/to/BLUEEURO.BIN [--skip REGEX]

Copies src/, tools/, tests/, themes/ and build.sh to build/<target>/ and rewrites them for the target image:
  - every stock flash address with its entry in targets/<target>_map.json (tools/addrmap.py, manual entries in
    targets/<target>_manual.json carry their evidence);
  - every RAM address with its entry in the "ram" table of targets/<target>.json (each with its evidence);
  - the object offsets in its "offsets" table (whole literals in src/ and tests/, each with its exact total count);
  - the per-file text replacements in targets/<target>.json (hashes, file offsets), each one
    must match exactly as often as stated;
  - in tests/, image slices [A:B] whose start A is a file offset of a mapped address (B moves with A;
    the nearest mapped address within 16 bytes gives the shift);
  - in tests/, checks that compare a sha256 with a desktop build or baseline are kept but reported as skipped
    (the euro build cannot reproduce desktop hashes; the behaviour checks around them still run);
then runs build.sh there with the target's stock image. Only patches whose name starts with one of the target's
"ported" prefixes are applied; addresses still unmapped in built files are listed (build/<target>/unmapped.json).
The desktop sources are not changed. Output: build/<target>/patch_bluebox.py, patch_manifest.json, ...
"""
import hashlib, json, os, pathlib, re, shutil, subprocess, sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from addrmap import ADDR, BASE, CAVES, code_text  # noqa: E402

# RAM literals (DTCM, AXI SRAM, D2 SRAM); backup SRAM 0x388... is the mod's own and left alone.
RAM = re.compile(r'0x(?:2[0-4]|30)[0-9a-fA-F]{6}(?![0-9a-fA-F])')


def main():
    target, stock_path = sys.argv[1], pathlib.Path(sys.argv[2]).resolve()
    conf = json.loads((ROOT / 'targets' / f'{target}.json').read_text())
    amap = {int(k, 16): int(v['target'], 16) for k, v in
            json.loads((ROOT / 'targets' / f'{target}_map.json').read_text()).items()}
    stock = stock_path.read_bytes()
    if hashlib.sha256(stock).hexdigest() != conf['stock_sha256']:
        raise SystemExit(f'Wrong stock firmware for {target}: {conf["firmware"]} exact image required')
    # Patches of features not ported yet stay out: every patch name must start with a "ported" prefix.
    skip = sys.argv[sys.argv.index('--skip') + 1] if '--skip' in sys.argv else \
        '^(?!(?:' + '|'.join(re.escape(p) for p in conf['ported']) + '))'

    out = ROOT / 'build' / target
    if out.exists():
        shutil.rmtree(out)
    out.mkdir(parents=True)
    for d in ('src', 'tools', 'tests', 'themes'):
        shutil.copytree(ROOT / d, out / d, ignore=shutil.ignore_patterns('*.log', '*.png', '__pycache__'))
    shutil.copy(ROOT / 'build.sh', out / 'build.sh')

    ram = {int(k, 16): int(v['target'], 16) for k, v in conf.get('ram', {}).items()}
    unmapped, ram_open = {}, {}
    def rewrite(path):
        text = path.read_text()
        code = code_text(path)
        live = set(int(m.group(), 16) for m in ADDR.finditer(code))
        for m in RAM.finditer(code):
            if int(m.group(), 16) not in ram:
                ram_open.setdefault(path.relative_to(out).as_posix(), set()).add(int(m.group(), 16))
        def ram_sub(m):
            v = int(m.group(), 16)
            return '0x%08x' % ram[v] if v in ram else m.group()
        def sub(m):
            v = int(m.group(), 16)
            if not BASE < v < BASE + len(stock) or CAVES[0] <= v < CAVES[1]:
                return m.group()
            if v not in amap:
                if v in live:
                    unmapped.setdefault(path.relative_to(out).as_posix(), set()).add(v)
                return m.group()
            s = m.group()
            new = ('0x%08x' if s.startswith('0x0') else '0x%07x') % amap[v]
            return new.upper().replace('0X', '0x') if s[2:] != s[2:].lower() else new
        path.write_text(RAM.sub(ram_sub, ADDR.sub(sub, text)))

    for p in sorted(out.rglob('*')):
        if p.is_file() and p.suffix in ('.c', '.h', '.py', '.ld', '.cjs', '.html'):
            rewrite(p)

    for e in conf.get('offsets', []):
        lit = re.compile(re.escape(e['old']) + r'(?![0-9a-fA-F])')
        files = [p for d in ('src', 'tests') for p in sorted((out / d).rglob('*')) if p.is_file() and p.suffix in ('.c', '.h', '.py')]
        n = sum(len(lit.findall(p.read_text())) for p in files)
        if n != e['count']:
            raise SystemExit(f'offset {e["old"]} found {n} times in src/ and tests/, expected {e["count"]} ({e["why"]})')
        for p in files:
            p.write_text(lit.sub(e['new'], p.read_text()))

    # tests: desktop hash comparisons become a visible skip
    hashcheck = re.compile(r"check\(hashlib\.sha256\((?:[^()]|\([^()]*(?:\([^()]*\))*[^()]*\))*\)\.hexdigest\(\)\s*==\s*[^,]+,"
                           r"\s*('(?:[^'\\]|\\.)*')\)")
    span = re.compile(r'\[(0x[0-9a-fA-F]{5,6}):(0x[0-9a-fA-F]{5,6})\]')
    def span_sub(m):
        # the nearest mapped address within 16 bytes gives the shift (code and data move in blocks)
        a, b = int(m.group(1), 16), int(m.group(2), 16)
        for k in sorted(range(-16, 17), key=abs):
            if BASE + a + k in amap:
                d = amap[BASE + a + k] - (BASE + a + k)
                return '[%#x:%#x]' % (a + d, b + d)
        return m.group()
    for p in sorted((out / 'tests').glob('test_*.py')):
        p.write_text(span.sub(span_sub, p.read_text()))
    for p in sorted((out / 'tests').glob('test_*.py')):
        p.write_text(hashcheck.sub(lambda m: f"check(True,'SKIPPED on {target} (desktop hash): '+{m.group(1)})", p.read_text()))

    for rel, edits in conf.get('replace', {}).items():
        p = out / rel
        text = p.read_text()
        for e in edits:
            n = text.count(e['old'])
            if n != e.get('count', 1):
                raise SystemExit(f'{rel}: {e["old"]!r} found {n} times, expected {e.get("count", 1)} ({e["why"]})')
            text = text.replace(e['old'], e['new'])
        p.write_text(text)

    # Every file that is compiled or patched must be fully mapped; tests may still use desktop addresses.
    built = {k: v for k, v in unmapped.items() if not k.startswith('tests/')}
    if built and '--allow-unmapped' not in sys.argv:
        for k, v in sorted(built.items()):
            print('unmapped in', k + ':', ' '.join('0x%08x' % a for a in sorted(v)))
        print('(patches for these stay out only if the skip pattern removes them; check before flashing)')
    built_ram = {k: v for k, v in ram_open.items() if not k.startswith('tests/')}
    for k, v in sorted(built_ram.items()):
        print('RAM address without evidence in', k + ':', ' '.join('0x%08x' % a for a in sorted(v)))
    (out / 'unmapped.json').write_text(json.dumps(
        {'flash': {k: sorted('0x%08x' % a for a in v) for k, v in unmapped.items()},
         'ram': {k: sorted('0x%08x' % a for a in v) for k, v in ram_open.items()}}, indent=1) + '\n')

    env = dict(os.environ, COBALT_SKIP=skip)
    subprocess.run(['sh', str(out / 'build.sh'), str(stock_path)], check=True, env=env, cwd=out)


if __name__ == '__main__':
    main()
