"""Map desktop 1.5.2 flash addresses to another bluebox image (eurorack 1.5.1).

Usage: python3 tools/addrmap.py BLUEBOX.BIN BLUEEURO.BIN [--report]

Collects every stock flash address the sources, the release tool and the tests use, and looks each one up in
the target image:
  anchors   unique 32-byte windows found once in both images give a piecewise offset (desktop -> target);
  code      the candidate from the nearest anchors must match the desktop bytes around the address, with BL / B.W
            and flash-pointer words masked;
  table     a word table (vtables, lists) matches word by word, pointers compared through their own mapping.
Manual entries in targets/<target>_manual.json win; each carries the evidence. Unresolved addresses are listed and
the retarget step refuses to build while any remains. Writes targets/<target>_map.json.
"""
import io, tokenize, bisect, json, pathlib, re, struct, sys

from capstone import CS_ARCH_ARM, CS_MODE_MCLASS, CS_MODE_THUMB, Cs

ROOT = pathlib.Path(__file__).resolve().parents[1]
BASE = 0x08040000
CAVES = (0x080A0000, 0x080E0000)          # Cobalt's own code: same addresses on both images
ADDR = re.compile(r'0x0?8[01][0-9a-fA-F]{5}(?![0-9a-fA-F])')
PCREL = re.compile(r'\[pc, #-?0x[0-9a-f]+\]')
MD = Cs(CS_ARCH_ARM, CS_MODE_THUMB | CS_MODE_MCLASS)
MDS = Cs(CS_ARCH_ARM, CS_MODE_THUMB | CS_MODE_MCLASS)
MDS.skipdata = True


def code_text(f):
    """The file without comments: addresses only mentioned in comments need no mapping."""
    text = f.read_text(errors='replace')
    if f.suffix in ('.c', '.h', '.ld', '.cjs'):
        return re.sub(r'/\*.*?\*/|//[^\n]*', ' ', text, flags=re.S)
    if f.suffix != '.py':
        return text
    # Python: drop comment tokens only ('#' inside strings, e.g. a patch name, stays)
    try:
        toks = [t for t in tokenize.generate_tokens(io.StringIO(text).readline) if t.type != tokenize.COMMENT]
        return tokenize.untokenize(toks)
    except (tokenize.TokenError, SyntaxError, IndentationError):
        return re.sub(r'#[^\n]*', ' ', text)


def flash_refs(desk):
    """Every stock flash address written in src/, tools/make_release.py and tests/ (code, not comments)."""
    files = list((ROOT / 'src').glob('*')) + [ROOT / 'tools/make_release.py'] + list((ROOT / 'tests').glob('*.py'))
    refs = {}
    for f in files:
        for m in ADDR.finditer(code_text(f)):
            v = int(m.group(), 16)
            if not BASE < v < BASE + len(desk) or CAVES[0] <= v < CAVES[1]:
                continue
            refs.setdefault(v, set()).add(f.relative_to(ROOT).as_posix())
    return refs


def is_bl(b, i):
    if i + 4 > len(b):
        return False
    h1, h2 = struct.unpack_from('<HH', b, i)
    return (h1 & 0xF800) == 0xF000 and (h2 & 0xC000) == 0xC000


def masked(b, lo, hi, size):
    """Bytes lo..hi of b with BL/B.W and words that look like flash pointers replaced by None."""
    out = list(b[lo:hi])
    for i in range(lo - lo % 2, hi, 2):
        if i >= lo and is_bl(b, i):
            for k in range(4):
                if i + k < hi:
                    out[i + k - lo] = None
    for i in range(lo + (-lo) % 4, hi - 3, 4):
        w = struct.unpack_from('<I', b, i)[0]
        if BASE <= w < BASE + size:
            for k in range(4):
                out[i + k - lo] = None
    return out


class Mapper:
    def __init__(self, desk, tgt):
        self.d, self.t = desk, tgt
        self.anchors = self._anchors()
        self.keys = [a for a, _ in self.anchors]
        self.cache = {}

    def _anchors(self):
        W = 32
        def index(b):
            idx = {}
            for i in range(0, len(b) - W, 4):
                idx.setdefault(b[i:i + W], []).append(i)
            return idx
        di, ti = index(self.d), index(self.t)
        pairs = []
        for k, ds in di.items():
            ts = ti.get(k)
            if len(ds) == 1 and ts and len(ts) == 1 and k.count(0) < W // 2 and len(set(k)) > 6:
                pairs.append((ds[0], ts[0]))
        pairs.sort()
        # longest increasing subsequence on the target offsets: drop anchors out of order
        tails, prev, idx = [], [None] * len(pairs), []
        for n, (_, t) in enumerate(pairs):
            j = bisect.bisect_left(tails, t)
            if j == len(tails):
                tails.append(t); idx.append(n)
            else:
                tails[j] = t; idx[j] = n
            prev[n] = idx[j - 1] if j else None
        out, n = [], idx[-1]
        while n is not None:
            out.append(pairs[n]); n = prev[n]
        return out[::-1]

    def candidates(self, off):
        i = bisect.bisect_right(self.keys, off)
        deltas = []
        for j in (i - 1, i, i - 2, i + 1):
            if 0 <= j < len(self.anchors):
                d = self.anchors[j][1] - self.anchors[j][0]
                if d not in deltas:
                    deltas.append(d)
        return deltas

    def score(self, off, toff, before=24, after=24):
        """Number of compared bytes that agree / total, with BL and pointers masked on the desktop side."""
        lo, hi = max(0, off - before), min(len(self.d), off + after)
        a = masked(self.d, lo, hi, len(self.d))
        tl = toff - (off - lo)
        if tl < 0 or tl + (hi - lo) > len(self.t):
            return 0, 1
        b = self.t[tl:tl + hi - lo]
        n = ok = 0
        for x, y in zip(a, b):
            if x is None:
                continue
            n += 1; ok += x == y
        return ok, n

    def insns(self, b, off, n):
        """n normalised instructions from file offset off: branch targets and PC-relative offsets dropped."""
        out = []
        for i in MD.disasm(b[off:off + 4 * n], BASE + off):
            op = i.op_str
            if i.mnemonic.startswith(('b', 'cb')) and '#' in op:
                op = op[:op.index('#')] + '#T'
            op = PCREL.sub('[pc, #P]', op)
            if i.mnemonic in ('adr', 'adr.w'):
                op = op.split(',')[0] + ', #P'
            out.append(i.mnemonic + ' ' + op)
            if len(out) == n:
                break
        return out

    def code(self, addr):
        off = (addr & ~1) - BASE
        # Addresses are function entries, call sites or table starts: the instructions from the address on must
        # agree. Instructions before it (another function's tail) may differ; agreeing ones only break ties.
        N = 12
        want, before = self.insns(self.d, off, N), self.insns(self.d, max(0, off - 24), 12)
        if len(want) < N:
            return self.data(addr)
        base = self.candidates(off)
        best = None
        for d in sorted({d + s for d in base for s in range(-48, 50, 2)}, key=lambda x: min(abs(x - d) for d in base)):
            got = self.insns(self.t, off + d, N)
            ok = sum(x == y for x, y in zip(want, got))
            if ok >= N - 1:
                pre = self.insns(self.t, max(0, off + d - 24), 12)
                pok = sum(x == y for x, y in zip(before[-6:], pre[-6:]))
                key = (ok, pok)
                if best is None or key > best[0]:
                    best = (key, d, '%d/%d insns, %d/6 before' % (ok, N, pok))
        if best:
            return addr + best[1], 'code ' + best[2]
        return self.data(addr)

    def stream(self, b, lo, hi, mapped):
        """Normalised instructions lo..hi (file offsets) as (offset, text); BL targets mapped when asked."""
        out = []
        for i in MDS.disasm(b[lo:hi], BASE + lo):
            op = i.op_str
            if i.mnemonic == 'bl' and op.startswith('#'):
                t = int(op[1:], 16)
                if mapped:
                    r = self.map(t, table_ok=False)[0]
                    t = r & ~1 if r else None
                op = '#%x' % t if t is not None else '#?'
            elif i.mnemonic.startswith(('b', 'cb')) and '#' in op:
                op = op[:op.index('#')] + '#T'
            op = PCREL.sub('[pc, #P]', op)
            out.append((i.address - BASE, i.mnemonic + ' ' + op))
        return out

    def site(self, addr):
        """A call site in changed code: the instructions around it, BL targets compared through the mapping."""
        off = (addr & ~1) - BASE
        d = self.stream(self.d, off - 40, off + 24, True)
        idx = next((k for k, (o, _) in enumerate(d) if o == off), None)
        if idx is None:
            return None
        base, t = self.candidates(off)[0], None
        for before, after in ((4, 2), (6, 2), (3, 3), (8, 3), (5, 0), (8, 0), (2, 4), (4, 1), (3, 1), (2, 2), (3, 0), (1, 3), (2, 1)):
            if idx < before or idx + after >= len(d):
                continue
            want = [x for _, x in d[idx - before:idx + after + 1]]
            if any(w.startswith('bl ') for w in want) is False or any('#?' in w for w in want):
                continue
            if t is None:
                t = self.stream(self.t, max(0, off + base - 0x4000), off + base + 0x4000, False)
            hits = [t[k][0] for k in range(before, len(t) - after)
                    if [x for _, x in t[k - before:k + after + 1]] == want]
            if len(hits) == 1:
                return BASE + hits[0] + (addr & 1), 'site %d+%d' % (before, after)
        return None

    def data(self, addr):
        """Bytes from the address on (data, or code capstone stops on), BL and flash pointers masked."""
        off = (addr & ~1) - BASE
        best = None
        for d in self.candidates(off):
            ok, n = self.score(off, off + d, 0, 32)
            if n >= 16 and ok >= n - 1:
                pok, pn = self.score(off, off + d, 24, 0)
                key = (ok / n, pok / pn if pn else 0)
                if best is None or key > best[0]:
                    best = (key, d, '%d/%d after, %d/%d before' % (ok, n, pok, pn))
        if best:
            return addr + best[1], 'bytes ' + best[2]
        # search: the masked window anywhere in the target, must be unique
        for before, after in ((16, 48), (32, 32), (48, 48), (64, 64), (64, 16), (96, 8), (8, 96), (24, 12), (12, 24)):
            lo, hi = max(0, off - before), min(len(self.d), off + after)
            pat = b''.join(b'.' if x is None else re.escape(bytes([x])) for x in masked(self.d, lo, hi, len(self.d)))
            hits = [m.start() for m in re.finditer(pat, self.t, re.S)]
            if len(hits) == 1:
                return BASE + hits[0] + (off - lo) + (addr & 1), 'search %d+%d' % (before, after)
        return None, 'unresolved'

    def word(self, w):
        """A desktop word as it should appear in the target, or None if it is a pointer we cannot map."""
        if BASE <= (w & ~1) < BASE + len(self.d):
            r, _ = self.map(w, table_ok=False)
            return r
        return w

    def table(self, addr, span=8):
        off = addr - BASE
        if off % 4:
            return None, 'unaligned'
        words = [struct.unpack_from('<I', self.d, off + 4 * k)[0] for k in range(-span, span)]
        tw = [self.word(w) for w in words]
        known = sum(1 for w, t in zip(words, tw) if t is not None and BASE <= (w & ~1) < BASE + len(self.d))
        if known < 3:
            return None, 'table: too few pointers'
        pat = b''.join(b'....' if t is None else re.escape(struct.pack('<I', t)) for t in tw)
        hits = [m.start() for m in re.finditer(pat, self.t, re.S) if m.start() % 4 == 0]
        if len(hits) == 1:
            return BASE + hits[0] + 4 * span, 'table %d ptrs' % known
        return None, 'table: %d hits' % len(hits)

    def map(self, addr, table_ok=True):
        if addr in self.cache:
            return self.cache[addr]
        self.cache[addr] = (None, 'recursion')
        r = self.code(addr)
        if r[0] is None:
            r = self.site(addr) or r
        if r[0] is None and table_ok:
            r = self.table(addr)
        self.cache[addr] = r
        return r


def main():
    desk, tgt = (pathlib.Path(p).read_bytes() for p in sys.argv[1:3])
    target = 'euro'
    manual_path = ROOT / 'targets' / f'{target}_manual.json'
    manual = json.loads(manual_path.read_text()) if manual_path.exists() else {}
    m = Mapper(desk, tgt)
    refs = flash_refs(desk)
    out, open_ = {}, []
    for a in sorted(refs):
        key = '0x%08x' % a
        mk = '0x%08x' % (a & ~1)
        if mk in manual:
            e = manual[mk]
            out[key] = dict(target='0x%08x' % (int(e['target'], 16) | (a & 1)), delta=int(e['target'], 16) - (a & ~1),
                            method='manual: ' + e['evidence'])
            continue
        r, how = m.map(a)
        # data words (tables): prefer the table method when the code score is weak
        if r is None:
            open_.append((a, how, sorted(refs[a])))
            continue
        out[key] = dict(target='0x%08x' % r, delta=r - a, method=how)
    (ROOT / 'targets').mkdir(exist_ok=True)
    (ROOT / 'targets' / f'{target}_map.json').write_text(json.dumps(out, indent=1, sort_keys=True) + '\n')
    print('anchors %d, addresses %d, mapped %d, unresolved %d' % (len(m.anchors), len(refs), len(out), len(open_)))
    for a, how, files in open_:
        print('  0x%08x  %-24s %s' % (a, how, ' '.join(files)))
    if '--report' in sys.argv:
        for k, v in out.items():
            print(k, v)


if __name__ == '__main__':
    main()
