"""Build Cobalt for another bluebox firmware from the desktop sources.

Usage: python3 tools/retarget.py euro /path/to/BLUEEURO.BIN [--skip REGEX]

Copies src/, tools/, tests/, themes/ and build.sh to build/<target>/ and rewrites them for the target image:
  - every stock flash address with its entry in targets/<target>_map.json (tools/addrmap.py, manual entries in
    targets/<target>_manual.json carry their evidence);
  - the per-file text replacements in targets/<target>.json (hashes, file offsets, object offsets), each one
    must match exactly as often as stated;
then runs build.sh there with the target's stock image. Only patches whose name starts with one of the target's
"ported" prefixes are applied; addresses still unmapped in built files are listed (build/<target>/unmapped.json).
The desktop sources are not changed. Output: build/<target>/patch_bluebox.py, patch_manifest.json, ...
"""
import hashlib, json, os, pathlib, re, shutil, subprocess, sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from addrmap import ADDR, BASE, CAVES, code_text  # noqa: E402


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

    unmapped = {}
    def rewrite(path):
        text = path.read_text()
        live = set(int(m.group(), 16) for m in ADDR.finditer(code_text(path)))
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
        path.write_text(ADDR.sub(sub, text))

    for p in sorted(out.rglob('*')):
        if p.is_file() and p.suffix in ('.c', '.h', '.py', '.ld', '.cjs', '.html'):
            rewrite(p)

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
    (out / 'unmapped.json').write_text(json.dumps({k: sorted('0x%08x' % a for a in v) for k, v in unmapped.items()},
                                                  indent=1) + '\n')

    env = dict(os.environ, COBALT_SKIP=skip)
    subprocess.run(['sh', str(out / 'build.sh'), str(stock_path)], check=True, env=env, cwd=out)


if __name__ == '__main__':
    main()
