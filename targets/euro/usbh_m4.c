/* USB audio to a device on the DEV port (eurorack 1.5.1 only; built and
 * patched by the euro build through targets/euro.json, euro addresses).
 *
 * The DEV port is a USB host (OTG2, full speed, PA11/PA12) run by the M4 with
 * ST's USB host library (USBH_Process thread 0x0811d80e, phost 0x3000ac28,
 * hhcd 0x3000b1d0). It has one class, MIDI (RAM 0x30000098, class code 1 =
 * audio), so any device with an audio interface activates it. Its
 * registration (bl 0x0811d184 at 0x081056c6) goes through host_register,
 * which puts the wrappers below into the class first: MIDI runs as before
 * (when its Init found a MIDI interface, the class pData is set), and with
 * "DEV Audio" on (USB page, src/pc_m4.c) the wrapper also finds the device's
 * audio OUT stream (host -> device, isochronous), selects it (SET_INTERFACE,
 * UAC2 SET_CUR 48 kHz on its clock, mute off and 0 dB on the stream's feature
 * unit) and sends one packet per SOF (1 ms): 48 frames, or 47-49 as the
 * device's feedback endpoint asks (async sinks such as the Torso S-4). Stage 1: a 1 kHz sine at -12 dBFS on channels 1-2, 3-4 (the
 * S-4's track 1) or all of them, the other channels silent. Each second the
 * packet results are summed up for the USB page ("DEV Pkts:"). Mode Audio:
 * the bluebox's 18 USB channels come from the M7 through a ring (dev_ring.h)
 * and go to the device's channels 1-10 as "DEV 1:".."DEV 10:" route them.
 *
 * The packets go through the periodic TX FIFO, 0xe0 words (896 bytes) after
 * the stock USB_HostInit (0x08117ac8, HS-core layout); it has to hold two
 * (two pipes in turn, see pipe2), so bigger ones shrink the RX FIFO to 0x100
 * words and the non-periodic one to 0x80 and give the periodic one 0x200.
 * Buffers come from the FreeRTOS heap (pvPortMalloc 0x0811ba90) at Init and
 * go back at DeInit; the pointer and the state for the USB page sit in the
 * USB line of backup SRAM (0x38800ee0, struct pc_usb in src/pc_m4.c). */
#include "shared.h"
#include "dev_ring.h"

#define FN(a) ((a) | 1u)
typedef uint8_t (*cls_fn)(void *);
#define midi_init   ((cls_fn)FN(0x08105990u))
#define midi_deinit ((cls_fn)FN(0x08105944u))
#define midi_req    ((cls_fn)FN(0x08105936u))
#define midi_bgnd   ((cls_fn)FN(0x08105b5au))
#define midi_sof    ((cls_fn)FN(0x08105aecu))
#define register_class ((uint8_t (*)(void *, void *))FN(0x0811d184u))
#define MIDI_PDATA  (*(volatile uint32_t *)0x300000b4u)   /* class pData: the MIDI handle while MIDI runs */
#define get_desc    ((uint8_t (*)(void *, uint8_t, uint16_t, uint8_t *, uint16_t))FN(0x0811dbdau))
#define ctl_req     ((uint8_t (*)(void *, uint8_t *, uint16_t))FN(0x0811d8f2u))
#define alloc_pipe  ((uint8_t (*)(void *, uint8_t))FN(0x0811dfd2u))
#define open_pipe   ((uint8_t (*)(void *, uint8_t, uint8_t, uint8_t, uint8_t, uint8_t, uint16_t))FN(0x0811dfa2u))
#define close_pipe  ((uint8_t (*)(void *, uint8_t))FN(0x0811dfc8u))
#define free_pipe   ((uint8_t (*)(void *, uint8_t))FN(0x0811e004u))
#define submit_urb  ((uint8_t (*)(void *, uint8_t, uint8_t, uint8_t, uint8_t, uint8_t *, uint16_t, uint8_t))FN(0x08105894u))
#define urb_state   ((uint8_t (*)(void *, uint8_t))FN(0x081058ceu))
#define xfer_size   ((uint32_t (*)(void *, uint8_t))FN(0x08105838u))   /* USBH_LL_GetLastXferSize: HAL hc[pipe].xfer_count (+0x4c) */
#define msg_put     ((int (*)(uint32_t, uint32_t, uint32_t))FN(0x0811b94au))
#define port_malloc ((void *(*)(uint32_t))FN(0x0811ba90u))
#define fw_get      ((int (*)(void *, unsigned))FN(0x08120588u))   /* settings value by tag (read only, linear search) */
#define SETTINGS    ((void *)(0x30018010u + 0x73b4u))            /* the session's settings (src/pc_m4.c PC_SETTINGS, euro) */
#define TAG_DEV_OUT 0x2b1u   /* "DEV 1:".."DEV 10:" (src/pc_m4.c PC_DEV_OUT): 0 Off, 1-18 bluebox USB channel */
#define HEAP_FREE   (*(volatile uint32_t *)0x3000eaccu)   /* FreeRTOS heap_4 xFreeBytesRemaining (pvPortMalloc 0x0811bb72) */

/* phost (USBH_HandleTypeDef) fields used. */
#define P8(p, o)  (*(volatile uint8_t *)((uint8_t *)(p) + (o)))
#define P16(p, o) (*(volatile uint16_t *)((uint8_t *)(p) + (o)))
#define P32(p, o) (*(volatile uint32_t *)((uint8_t *)(p) + (o)))
#define REQ_STATE 0x02u   /* RequestState, 1 = CMD_SEND */
#define SETUP     0x10u   /* Control.setup: type, request, value, index, length */
#define DEV_ADDR  0x31cu
#define DEV_SPEED 0x31du
#define CFG_TOTAL 0x33au  /* CfgDesc.wTotalLength */
#define OS_EVENT  0x59cu

#define OTG ((volatile uint32_t *)0x40080000u)   /* OTG2 (FS) core registers, word index */
#define GRSTCTL (0x10u / 4u)
#define GRXFSIZ (0x24u / 4u)
#define HNPTXFSIZ (0x28u / 4u)
#define HPTXFSIZ (0x100u / 4u)

enum {USBH_OK, USBH_BUSY, USBH_FAIL, USBH_NOT_SUPPORTED};
enum {URB_IDLE};
/* USB page state ("DEV State:"), same order as the list in src/pc_m4.c. */
enum {ST_OFF, ST_WAIT, ST_NOAUDIO, ST_START, ST_SEND, ST_ERROR, ST_NOMEM, ST_BIG};
/* Shared with src/pc_m4.c (struct pc_usb): M4 only, the M7 reads magic/mask/format. */
struct host_line {uint32_t magic, mask, format, mode, state, channels, ctx, pkts;};
/* channels: bits 0-7 the channel count, 8-31 the last feedback answer ("DEV FB raw:"). */
/* mode: 0 Off, 1 sine on 1-2, 2 on 3-4, 3 on all, 4 the bluebox audio as routed. pkts ("DEV Pkts:"), per second: */
#define MODE_AUDIO 4u
#define MODE_RING 5u   /* "Ring test": as Audio, but the M7 writes a sine (checks the ring path) */
#define MODE_SYNC 6u   /* "Audio sync": as Audio without resampling; the frames per packet follow the M7 instead */
#define MODE_LAST MODE_SYNC
#define RCC_D2CCIP2R (*(volatile uint32_t *)0x58024454u)   /* USBSEL bits 21:20: 1 PLL1Q, 2 PLL3Q, 3 HSI48 */
#define RCC_APB1HENR (*(volatile uint32_t *)0x580240ecu)   /* bit 1 CRSEN */
#define CRS_CR (*(volatile uint32_t *)0x40008400u)         /* bit 6 AUTOTRIMEN, bits 13:8 TRIM (HSI48, reset 32) */
#define TRIM_SOFS 4000u   /* SOFs per HSI48 trim step check */
#define TRIM_TOL 3u       /* ms off in TRIM_SOFS before a step (0.075 %; one TRIM step is about 0.14 %) */
#define TRIM_LO 20u
#define TRIM_HI 44u
enum {PK_NONE, PK_OK, PK_NOSOF, PK_STUCK, PK_NOTREADY, PK_NYET, PK_ERROR, PK_STALL, PK_NOFB, PK_FBODD,
      PK_FBEMPTY, PK_FB44, PK_FB96, PK_NOINPUT, PK_XRUN};
/* Mode Audio: PK_NOINPUT no blocks from the M7 this second, PK_XRUN the ring
 * ran empty (a gap) this second. */
/* PK_OK with a feedback endpoint: packets out and the device's feedback near 48
 * (it runs the stream at 48 kHz); PK_NOFB packets out but no feedback answer;
 * PK_FBEMPTY answers of 0 (or empty); PK_FB44 / PK_FB96 the device runs at
 * 44.1 / 88.2-96 kHz; PK_FBODD any other value. */
#define LINE ((volatile struct host_line *)0x38800ee0u)
_Static_assert(sizeof(struct host_line) == 32, "the 0xee0 line");

#define CFG_MAX 1024u
#define PKT_MAX 1024u
#define FRAMES 48u   /* full speed: one packet per 1 ms frame at 48 kHz */
#define FRAMES_MAX 49u
enum {R_CFG, R_ITF, R_RATE, R_MUTE, R_VOL, R_OPEN, R_MIDI};
struct host_ctx {
    uint32_t magic;
    uint8_t step, pipe, open, inflight;
    uint8_t ep, itf, alt, ac_itf, clock, nrch, subslot, uac2;
    uint16_t maxp, len;
    uint32_t sent, late;
    uint32_t done, bad, last_bad, sofs, fill_mode;
    uint8_t fu, fb_ep, fb_pipe, fb_open, fb_inflight, fb_size, nmax, phase;
    uint32_t fb, acc, fb_seen;   /* frames per ms, 16.16; feedback answers near 48 this second */
    uint32_t fb_odd, fb_empty;   /* answers far from 48, answers of 0, this second */
    uint32_t fb_k;               /* frames per ms of the last answer far from 48 */
    uint32_t fb_raw;             /* the last answer as received ("DEV FB raw:") */
    uint8_t fb_buf[4], ctl[4];
    uint8_t rate[4];
    uint8_t cfg[CFG_MAX];   /* the config descriptor while Requests parse it, then the second packet (pkt2) */
    uint8_t pkt[PKT_MAX];
    /* The second pipe of each pair: the USB core sends an isochronous
     * transfer in the frame after it is started (ODDFRM = next frame), so one
     * pipe can only serve every other frame. Two pipes in turn serve every
     * frame, as the device expects (a device may only take a packet in the
     * frame it set up for it). Index 0 is pipe / inflight / pkt / fb_pipe /
     * fb_inflight / fb_buf above, index 1 these. */
    uint8_t pipe2, inflight2, fb_pipe2, fb_inflight2, cur, fb_cur, open2, fb_open2;
    uint8_t fb_buf2[4];
    /* Mode Audio: live (Init to DeInit; the block stays allocated for the
     * M7), ring read position, primed (ring filled to half once),
     * heartbeat, averaged fill (x256), gaps and M7 blocks this second. */
    uint8_t live, primed, hb, spare8;
    uint32_t ring;   /* the ring after this context (dev_ring.h) */
    uint32_t rd, avg, xruns, blocks_seen, spare32;
    uint32_t m7_frames;   /* the M7's frame count at the last second */
    /* Resampler (fill_audio): read position fraction (16 bit), integral of
     * the fill error (x256 frames x ms). */
    uint32_t frac;
    int32_t isum;
    int32_t off;   /* the controller's last offset (x65536): Audio steps by it, Audio sync sends that many frames more */
    uint32_t clk_sofs, clk_ms, clk_shown;   /* SOFs and the M7's ms tick since the last 10 s, the last result */
    uint32_t tr_sofs, tr_ms;   /* the same for the HSI48 trim (hsi48_trim) */
};
#define PKT2(c) ((c)->cfg)
_Static_assert(CFG_MAX >= PKT_MAX, "cfg doubles as pkt2");
/* Ring sizes tried, largest first. The smallest must hold a packet (up to 51
 * frames resampled), an M7 block (32 frames) and the margins (see fill_audio). */
static const uint16_t ring_sizes[] = {256, 160, 128};
#define CTX_MAGIC 0x48535431u

/* 1 kHz at 48 kHz is 48 samples a cycle: one packet holds one cycle. -12 dBFS. */
static const int16_t sine48[FRAMES] = {
    0, 1069, 2120, 3135, 4096, 4987, 5793, 6498, 7094, 7568, 7913, 8122,
    8192, 8122, 7913, 7568, 7094, 6498, 5793, 4987, 4096, 3135, 2120, 1069,
    0, -1069, -2120, -3135, -4096, -4987, -5793, -6498, -7094, -7568, -7913, -8122,
    -8192, -8122, -7913, -7568, -7094, -6498, -5793, -4987, -4096, -3135, -2120, -1069};

static volatile struct host_line *line(void) {backup_enable(); PWR_CR1 |= 1u << 8; return LINE;}
/* The block: the ring (dev_ring.h), then the context. LINE->ctx holds the
 * ring address and the heartbeat; cleared at boot (host_register). */
static struct host_ctx *block(void)
{
    uint32_t a = LINE->ctx & ~31u;
    if (a < DEV_RAM_LO || a > DEV_RAM_HI - sizeof(struct dev_ring)) return 0;
    struct dev_ring *r = (struct dev_ring *)a;
    uint32_t b = r->ctx;
    if (r->magic != DEV_RING_MAGIC || b < DEV_RAM_LO || b > DEV_RAM_HI - sizeof(struct host_ctx) || (b & 31u)) return 0;
    struct host_ctx *c = (struct host_ctx *)b;
    return c->magic == CTX_MAGIC && c->ring == a ? c : 0;
}
static struct dev_ring *ring_of(struct host_ctx *c) {return (struct dev_ring *)c->ring;}
static struct host_ctx *ctx(void) {struct host_ctx *c = block(); return c && c->live ? c : 0;}
static void set_state(unsigned s) {line()->state = s;}
static void kick(void *phost) {msg_put(P32(phost, OS_EVENT), 1u, 0u);}

/* The device's audio OUT stream: the first alternate setting of an audio
 * streaming interface with an isochronous OUT endpoint and PCM of 2-4 bytes. */
static int parse(struct host_ctx *c, unsigned total)
{
    uint8_t term_id[8], term_clk[8], fu_id[8], fu_src[8];
    unsigned nterm = 0, nfu = 0, num = 0, alt = 0, cls = 0, sub = 0, uac2 = 0, link = 0, nrch = 0, slot = 0, ep = 0, maxp = 0, ac = 0, fb = 0, fbmax = 0;
    for (unsigned i = 0; i + 2u <= total; ) {
        const uint8_t *d = c->cfg + i;
        unsigned len = d[0], type = d[1];
        if (len < 2u || i + len > total) break;
        if (type == 4u && len >= 9u) {
            if (cls == 1u && sub == 2u && alt && ep && nrch && slot >= 2u && slot <= 4u) break;   /* previous alt is it */
            num = d[2]; alt = d[3]; cls = d[5]; sub = d[6]; uac2 = d[7] == 0x20u;
            link = nrch = slot = ep = maxp = fb = fbmax = 0;
            if (cls == 1u && sub == 1u) ac = num;
        } else if (type == 0x24u && cls == 1u && sub == 1u && uac2 && len >= 8u && d[2] == 2u && nterm < 8u) {
            term_id[nterm] = d[3]; term_clk[nterm] = d[7]; nterm++;   /* UAC2 input terminal: id, clock source */
        } else if (type == 0x24u && cls == 1u && sub == 1u && uac2 && len >= 6u && d[2] == 6u && nfu < 8u) {
            fu_id[nfu] = d[3]; fu_src[nfu] = d[4]; nfu++;   /* feature unit: id, source */
        } else if (type == 0x24u && cls == 1u && sub == 2u) {
            if (d[2] == 1u && uac2 && len >= 11u) {link = d[3]; nrch = d[10];}
            else if (d[2] == 2u && uac2 && len >= 6u) slot = d[4];
            else if (d[2] == 2u && !uac2 && len >= 8u) {nrch = d[4]; slot = d[5];}
        } else if (type == 5u && cls == 1u && sub == 2u && len >= 7u && !(d[2] & 0x80u) && (d[3] & 3u) == 1u) {
            ep = d[2]; maxp = (d[4] | d[5] << 8) & 0x7ffu;
        } else if (type == 5u && cls == 1u && sub == 2u && len >= 7u && (d[2] & 0x80u) && (d[3] & 0x33u) == 0x11u) {
            fb = d[2]; fbmax = (d[4] | d[5] << 8) & 0x7ffu;   /* isochronous IN, usage feedback */
        }
        i += len;
    }
    if (!(cls == 1u && sub == 2u && alt && ep && nrch && slot >= 2u && slot <= 4u)) return 0;
    c->itf = (uint8_t)num; c->alt = (uint8_t)alt; c->ep = (uint8_t)ep; c->maxp = (uint16_t)maxp;
    c->nrch = (uint8_t)nrch; c->subslot = (uint8_t)slot; c->uac2 = (uint8_t)uac2; c->ac_itf = (uint8_t)ac; c->clock = 0;
    for (unsigned k = 0; k < nterm; k++) if (term_id[k] == link) c->clock = term_clk[k];
    c->fu = 0;
    for (unsigned k = 0; k < nfu; k++) if (fu_src[k] == link) c->fu = fu_id[k];
    c->fb_ep = fbmax == 3u || fbmax == 4u ? (uint8_t)fb : 0;
    c->fb_size = (uint8_t)fbmax;
    return 1;
}

/* One packet of n frames: the sine (phase carried over) on the chosen
 * channels, in the top two bytes of each subslot; the rest 0. */
static void fill(struct host_ctx *c, uint8_t *buf, unsigned mode, unsigned n)
{
    uint8_t *p = buf;
    c->fill_mode = mode;
    unsigned ph = c->phase;
    for (unsigned f = 0; f < n; f++, ph = ph + 1u == FRAMES ? 0u : ph + 1u)
        for (unsigned ch = 0; ch < c->nrch; ch++) {
            int on = mode == 3u || (mode == 1u && ch < 2u) || (mode == 2u && (ch == 2u || ch == 3u));
            int16_t v = on ? sine48[ph] : 0;
            for (unsigned b = 0; b + 2u < c->subslot; b++) *p++ = 0;
            *p++ = (uint8_t)v; *p++ = (uint8_t)((uint16_t)v >> 8);
        }
    c->phase = (uint8_t)ph;
    c->len = (uint16_t)(p - buf);
}

/* Routes from the settings (DEV 1-10): 0 Off, 1-18 a bluebox USB channel. */
static void routes(struct host_ctx *c)
{
    for (unsigned k = 0; k < DEV_OUTS; k++) {
        int v = fw_get(SETTINGS, TAG_DEV_OUT + k);
        ring_of(c)->route[k] = (uint8_t)(v >= 0 && v <= (int)DEV_SRC ? v : 0);   /* the M7 routes as it writes */
    }
}

/* Mode Audio: n frames from the M7's ring, each device channel from its
 * route (the M7 routes as it writes). The bluebox audio clock is not the
 * device's (measured on the S-4: 48224 frames a second against 48000), so the
 * ring is read with a resampler: each frame is interpolated between two ring
 * frames, the read position moves by step = 1 + offset per frame. A PI
 * controller on the averaged fill sets the offset and holds the fill at target;
 * its integral is the clock ratio (kept across gaps), at most RS_MAX (3 %).
 * The ring is first filled to target (silence meanwhile); it runs empty: a gap,
 * silence until it is filled again. */
#define RS_MAX 2048   /* step offset limit, x65536: 3.1 % */
#define RS_KP 4       /* proportional: 16 per frame off (fill x256 >> 4) */
#define RS_KI 12      /* integral: 1/16 per frame off and ms (sum of fill x256 >> 12) */
static int32_t rs_clamp(int32_t v) {return v > RS_MAX ? RS_MAX : v < -RS_MAX ? -RS_MAX : v;}
static void fill_audio(struct host_ctx *c, uint8_t *buf, unsigned n, int resample)
{
    struct dev_ring *g = ring_of(c);
    unsigned size = g->frames, target = (size + 36u) / 2u;   /* midway between a packet + an M7 block and the overrun limit */
    unsigned wr = g->wr % size, rd = c->rd % size;
    unsigned avail = (wr + size - rd) % size;
    if (avail > size - 16u) {rd = (wr + size - target) % size; avail = target; c->frac = 0; c->xruns++;}   /* overrun */
    if (!c->primed && avail >= target) {c->primed = 1; c->avg = avail << 8; c->frac = 0;}
    int32_t step = 65536;
    if (c->primed) {
        c->avg += (int32_t)((avail << 8) - c->avg) / 16;
        int32_t e = (int32_t)c->avg - (int32_t)(target << 8);
        c->isum += e;
        int32_t lim = RS_MAX << RS_KI;
        c->isum = c->isum > lim ? lim : c->isum < -lim ? -lim : c->isum;
        c->off = rs_clamp((c->isum >> RS_KI) + (e >> RS_KP));
        if (resample) step += c->off;
    }
    uint8_t *p = buf;
    uint32_t frac = c->frac;
    for (unsigned f = 0; f < n; f++) {
        const int16_t *s = 0, *t = 0;
        int32_t h = (int32_t)(frac >> 1);   /* weight of t, 15 bit */
        if (c->primed) {
            if (avail >= 2u) {
                s = &g->data[rd * DEV_OUTS];
                t = &g->data[((rd + 1u) % size) * DEV_OUTS];
                frac += (uint32_t)step;
                rd = (rd + (frac >> 16)) % size; avail -= frac >> 16; frac &= 0xffffu;
            } else {c->primed = 0; c->xruns++;}
        }
        for (unsigned ch = 0; ch < c->nrch; ch++) {
            int16_t v = 0;
            if (s && ch < DEV_OUTS) v = (int16_t)(s[ch] + (((int32_t)t[ch] - s[ch]) * h >> 15));
            for (unsigned b = 0; b + 2u < c->subslot; b++) *p++ = 0;
            *p++ = (uint8_t)v; *p++ = (uint8_t)((uint16_t)v >> 8);
        }
    }
    c->frac = frac;
    c->rd = rd;
    c->len = (uint16_t)(p - buf);
}

/* The DEV port's USB clock is HSI48, an RC oscillator the stock firmware does
 * not trim (no CRS): on Kyrill's bluebox its frames ran 0.32 % long (10032 ms
 * of the M7 in 10000 SOFs), outside the 0.25 % of full speed, and the device
 * got too few frames a second (crackling in every mode). In the audio modes
 * (the M7 writes its ms tick into the ring) the SOFs are compared with the
 * M7's tick every TRIM_SOFS and the CRS TRIM moved one step towards 1 ms
 * frames, within TRIM_LO-TRIM_HI. Only with USBSEL HSI48 and automatic
 * trimming off. The trim stays until reset, also for the sine modes and the
 * USB port to the computer (same clock). */
static void hsi48_trim(struct host_ctx *c)
{
    if (++c->tr_sofs < TRIM_SOFS) return;
    uint32_t ms = ring_of(c)->spare1[2], d = ms - c->tr_ms;
    c->tr_ms = ms; c->tr_sofs = 0;
    if (((RCC_D2CCIP2R >> 20) & 3u) != 3u || d < TRIM_SOFS - 100u || d > TRIM_SOFS + 100u) return;   /* not HSI48, or the M7 tick not running */
    RCC_APB1HENR |= 2u;
    uint32_t cr = CRS_CR;
    unsigned t = (cr >> 8) & 63u;
    if (cr & (1u << 6)) return;
    if (d > TRIM_SOFS + TRIM_TOL && t < TRIM_HI) t++;        /* frames long: HSI48 slow, higher TRIM is faster */
    else if (d < TRIM_SOFS - TRIM_TOL && t > TRIM_LO) t--;
    else return;
    CRS_CR = (cr & ~(63u << 8)) | t << 8;
}

/* Periodic TX FIFO deep enough for two packets (the one on the bus and the
 * next, see pipe2), stock HS-core layout only: RX 0x200 words at 0, NPTX
 * 0x100 at 0x200, PTX 0xe0 at 0x300 (0x3e0 words used). Then RX 0x100 at 0
 * (1 KB: control, MIDI and feedback IN packets are 64 bytes at most), NPTX
 * 0x80 at 0x100, PTX 0x200 (2 KB) at 0x180: 0x380 words, inside the stock
 * use. Called while no transfer runs (between control requests, before MIDI
 * starts its pipes); all FIFOs flushed. */
static int fifo(unsigned bytes)
{
    unsigned need = 2u * ((bytes + 3u) / 4u);
    if ((OTG[HPTXFSIZ] >> 16) >= need) return 1;
    if (need > 0x200u || OTG[GRXFSIZ] != 0x200u || OTG[HNPTXFSIZ] != 0x01000200u) return 0;
    OTG[GRXFSIZ] = 0x100u;
    OTG[HNPTXFSIZ] = 0x80u << 16 | 0x100u;
    OTG[HPTXFSIZ] = 0x200u << 16 | 0x180u;
    for (unsigned n = 0; n < 100000u && !(OTG[GRSTCTL] & 1u << 31); n++) {}   /* AHB idle, as USB_FlushTxFifo */
    OTG[GRSTCTL] = 0x10u << 6 | 1u << 5;   /* flush all TX FIFOs */
    for (unsigned n = 0; n < 100000u && (OTG[GRSTCTL] & 1u << 5); n++) {}
    OTG[GRSTCTL] = 1u << 4;                /* flush the RX FIFO */
    for (unsigned n = 0; n < 100000u && (OTG[GRSTCTL] & 1u << 4); n++) {}
    return 1;
}

uint8_t host_init(void *phost)
{
    volatile struct host_line *l = line();
    struct host_ctx *c = block();
    if (c) c->live = 0;
    l->pkts = PK_NONE;
    if (l->mode == 0u || l->mode > MODE_LAST) return midi_init(phost);   /* DEV Audio Off: the stock class */
    uint8_t r = midi_init(phost);
    l->channels = 0;
    struct dev_ring *g = c ? ring_of(c) : 0;
    if (!c) {   /* once per boot: context and ring, 32-byte aligned, kept; the ring as big as the heap allows */
        const unsigned head = (sizeof(struct host_ctx) + 31u) & ~31u;
        uint32_t raw = 0;
        unsigned frames = 0;
        for (unsigned k = 0; k < sizeof ring_sizes / sizeof ring_sizes[0] && !raw; k++) {
            frames = ring_sizes[k];
            raw = (uint32_t)port_malloc(31u + head + sizeof(struct dev_ring) + frames * DEV_OUTS * 2u);
        }
        if (!raw) {l->channels = (HEAP_FREE & 0xffffffu) << 8; set_state(ST_NOMEM); return r;}   /* "DEV FB raw:" shows the free heap */
        c = (struct host_ctx *)((raw + 31u) & ~31u);
        g = (struct dev_ring *)((uint32_t)c + head);
        for (unsigned i = 0; i < sizeof *g; i++) ((uint8_t *)g)[i] = 0;
        g->frames = frames;
        g->ctx = (uint32_t)c;
        g->magic = DEV_RING_MAGIC;
        l->ctx = (uint32_t)g;
    }
    for (unsigned i = 0; i < sizeof *c; i++) ((uint8_t *)c)[i] = 0;
    c->ring = (uint32_t)g;
    c->magic = CTX_MAGIC;
    c->step = R_CFG;
    c->live = 1;
    set_state(ST_START);
    return USBH_OK;   /* MIDI or not: the audio stream is looked for in Requests */
}

uint8_t host_requests(void *phost)
{
    struct host_ctx *c = ctx();
    if (!c) return midi_req(phost);
    uint8_t r;
    switch (c->step) {
    case R_CFG: {
        unsigned total = P16(phost, CFG_TOTAL);
        if (total > CFG_MAX) total = CFG_MAX;
        r = get_desc(phost, 0, 0x0200u, c->cfg, (uint16_t)total);
        if (r == USBH_BUSY) break;
        if (r != USBH_OK) {set_state(ST_ERROR); c->step = R_MIDI; break;}
        if (!parse(c, total)) {set_state(ST_NOAUDIO); c->step = R_MIDI; break;}
        line()->channels = c->nrch;
        unsigned frame = c->nrch * c->subslot, nmax = c->maxp / frame;
        if (nmax > FRAMES_MAX) nmax = FRAMES_MAX;
        if (nmax < FRAMES || nmax * frame > PKT_MAX || !fifo(nmax * frame)) {set_state(ST_BIG); c->step = R_MIDI; break;}
        c->nmax = (uint8_t)nmax;
        c->step = R_ITF;
        break;
    }
    case R_ITF:
        if (P8(phost, REQ_STATE) == 1u) {
            P8(phost, SETUP) = 0x01u; P8(phost, SETUP + 1u) = 0x0bu;   /* SET_INTERFACE */
            P16(phost, SETUP + 2u) = c->alt; P16(phost, SETUP + 4u) = c->itf; P16(phost, SETUP + 6u) = 0;
        }
        r = ctl_req(phost, 0, 0);
        if (r == USBH_BUSY) break;
        if (r != USBH_OK) {set_state(ST_ERROR); c->step = R_MIDI; break;}
        c->step = c->uac2 && c->clock ? R_RATE : R_OPEN;
        break;
    case R_RATE:   /* UAC2 SET_CUR sampling frequency on the stream's clock; a stall is fine (fixed rate) */
        if (P8(phost, REQ_STATE) == 1u) {
            c->rate[0] = 0x80; c->rate[1] = 0xbb; c->rate[2] = 0; c->rate[3] = 0;   /* 48000 */
            P8(phost, SETUP) = 0x21u; P8(phost, SETUP + 1u) = 0x01u;
            P16(phost, SETUP + 2u) = 0x0100u; P16(phost, SETUP + 4u) = (uint16_t)(c->clock << 8 | c->ac_itf);
            P16(phost, SETUP + 6u) = 4u;
        }
        r = ctl_req(phost, c->rate, 4u);
        if (r == USBH_BUSY) break;
        c->step = c->fu ? R_MUTE : R_OPEN;
        break;
    case R_MUTE:   /* the stream's feature unit, master channel: mute off, then volume 0 dB (as a computer does) */
    case R_VOL:
        if (P8(phost, REQ_STATE) == 1u) {
            c->ctl[0] = 0; c->ctl[1] = 0;
            P8(phost, SETUP) = 0x21u; P8(phost, SETUP + 1u) = 0x01u;
            P16(phost, SETUP + 2u) = c->step == R_MUTE ? 0x0100u : 0x0200u;
            P16(phost, SETUP + 4u) = (uint16_t)(c->fu << 8 | c->ac_itf);
            P16(phost, SETUP + 6u) = c->step == R_MUTE ? 1u : 2u;
        }
        r = ctl_req(phost, c->ctl, c->step == R_MUTE ? 1u : 2u);
        if (r == USBH_BUSY) break;
        c->step = c->step == R_MUTE ? R_VOL : R_OPEN;   /* a stall (no such control) is fine */
        break;
    case R_OPEN:
        c->pipe = alloc_pipe(phost, c->ep);
        if (c->pipe == 0xffu) {set_state(ST_ERROR); c->step = R_MIDI; break;}
        open_pipe(phost, c->pipe, c->ep, P8(phost, DEV_ADDR), P8(phost, DEV_SPEED), 1u /* isochronous */, c->maxp);
        c->pipe2 = alloc_pipe(phost, c->ep);
        if (c->pipe2 != 0xffu) {
            open_pipe(phost, c->pipe2, c->ep, P8(phost, DEV_ADDR), P8(phost, DEV_SPEED), 1u, c->maxp);
            c->open2 = 1;
        }
        if (c->fb_ep) {
            c->fb_pipe = alloc_pipe(phost, c->fb_ep);
            if (c->fb_pipe != 0xffu) {
                open_pipe(phost, c->fb_pipe, c->fb_ep, P8(phost, DEV_ADDR), P8(phost, DEV_SPEED), 1u, c->fb_size);
                c->fb_open = 1;
                c->fb_pipe2 = alloc_pipe(phost, c->fb_ep);
                if (c->fb_pipe2 != 0xffu) {
                    open_pipe(phost, c->fb_pipe2, c->fb_ep, P8(phost, DEV_ADDR), P8(phost, DEV_SPEED), 1u, c->fb_size);
                    c->fb_open2 = 1;
                }
            }
        }
        c->fb = FRAMES << 16;
        c->acc = 0;
        c->phase = 0;
        c->rd = ring_of(c)->wr;   /* mode Audio: from what the M7 writes next */
        c->primed = 0;
        c->blocks_seen = ring_of(c)->blocks;
        c->m7_frames = ring_of(c)->spare1[1];
        c->frac = 0; c->isum = 0; c->off = 0;
        c->clk_sofs = 0; c->clk_ms = ring_of(c)->spare1[2]; c->clk_shown = 0;
        c->tr_sofs = 0; c->tr_ms = c->clk_ms;
        routes(c);
        c->open = 1;
        set_state(ST_SEND);
        LINE->pkts = PK_NOSOF;   /* until the first second of SOFs */
        c->step = R_MIDI;
        break;
    default:
        return MIDI_PDATA ? midi_req(phost) : USBH_OK;
    }
    kick(phost);   /* come back: no URB event may follow a step */
    return USBH_BUSY;
}

/* Feedback (async sink): frames per ms. Full speed is 10.14 in 3 bytes, but
 * some devices send 16.16 per frame or per 125 us microframe (Linux gadgets
 * of some versions): the first reading that lands in 40-100 frames counts.
 * Taken when it is near 48. Then the next read. */
static uint32_t fb_decode(uint32_t raw, unsigned size)
{
    uint32_t c[3] = {size == 3u ? raw << 2 : raw, size == 3u ? raw : raw << 3, raw << 3};
    for (unsigned i = 0; i < 3u; i++)
        if (c[i] >= 40u << 16 && c[i] <= 100u << 16) return c[i];
    return 0;
}

static void feedback(void *phost, struct host_ctx *c)
{
    unsigned i = c->fb_open2 ? c->fb_cur : 0u;   /* the pipe of this frame */
    uint8_t pipe = i ? c->fb_pipe2 : c->fb_pipe, *inflight = i ? &c->fb_inflight2 : &c->fb_inflight;
    uint8_t *buf = i ? c->fb_buf2 : c->fb_buf;
    unsigned st = *inflight ? urb_state(phost, pipe) : 1u;
    if (*inflight && st == URB_IDLE) return;   /* not back yet: this frame is skipped, the pipe stays */
    c->fb_cur = (uint8_t)(i ^ 1u);
    if (*inflight && st == 1u) {
        uint32_t raw = buf[0] | buf[1] << 8 | buf[2] << 16;
        if (c->fb_size == 4u) raw |= (uint32_t)buf[3] << 24;
        uint32_t v = fb_decode(raw, c->fb_size);
        c->fb_raw = raw;
        if (!raw || !xfer_size(phost, pipe)) c->fb_empty++;
        else if (v >= 46u << 16 && v <= 50u << 16) {c->fb = v; c->fb_seen++;}
        else {c->fb_odd++; c->fb_k = (v + (1u << 15)) >> 16;}
    }
    for (unsigned k = 0; k < 4u; k++) buf[k] = 0;   /* an empty answer reads as 0 */
    submit_urb(phost, pipe, 1 /* in */, 1u /* isochronous */, 1u /* data */, buf, c->fb_size, 0);
    *inflight = 1;
}

/* SOF interrupt, every 1 ms: the next packet once the last one is out, with
 * as many frames as the feedback asks (48 without feedback). */
uint8_t host_sof(void *phost)
{
    struct host_ctx *c = ctx();
    unsigned mode = LINE->mode;
    if (c && c->open && mode >= 1u && mode <= MODE_LAST) {
        if (c->fb_open) feedback(phost, c);
        unsigned i = c->open2 ? c->cur : 0u;   /* pipes in turn, one per frame */
        uint8_t pipe = i ? c->pipe2 : c->pipe, *inflight = i ? &c->inflight2 : &c->inflight;
        uint8_t *buf = i ? PKT2(c) : c->pkt;
        unsigned st = *inflight ? urb_state(phost, pipe) : 1u;
        if (*inflight && st == URB_IDLE) c->late++;
        else {
            c->cur = (uint8_t)(i ^ 1u);
            if (*inflight) {if (st == 1u) c->done++; else {c->bad++; c->last_bad = st;}}
            c->acc += c->fb;
            if (mode == MODE_SYNC) c->acc += (uint32_t)((int32_t)(c->fb >> 8) * c->off >> 8);   /* as many frames as the M7 gives */
            unsigned n = c->acc >> 16;
            c->acc -= n << 16;
            if (n > c->nmax) n = c->nmax;
            if (n < FRAMES - 1u) n = FRAMES - 1u;
            if (mode >= MODE_AUDIO) fill_audio(c, buf, n, mode != MODE_SYNC);
            else fill(c, buf, mode, n);
            submit_urb(phost, pipe, 0 /* out */, 1u /* isochronous */, 1u /* data */, buf, c->len, 0);
            *inflight = 1;
            c->sent++;
        }
        if (mode >= MODE_AUDIO && (c->sofs & 7u) == 0u)   /* heartbeat for the M7 (dev_ring.h) */
            LINE->ctx = (uint32_t)ring_of(c) | (c->hb = (uint8_t)((c->hb + 1u) & DEV_HB_MASK));
        if (c->sofs % 100u == 50u) routes(c);
        if (mode >= MODE_AUDIO) hsi48_trim(c);
        if (mode >= MODE_AUDIO && ++c->clk_sofs >= 10000u) {   /* SOF clock against the M7's ms tick */
            uint32_t ms = ring_of(c)->spare1[2];
            c->clk_shown = ms - c->clk_ms > 99999u ? 99999u : ms - c->clk_ms;
            c->clk_ms = ms; c->clk_sofs = 0;
        }
        if (++c->sofs >= 1000u) {   /* once a second: what happened to the packets */
            uint32_t blocks = ring_of(c)->blocks;
            LINE->pkts = c->bad ? (c->last_bad >= 2u && c->last_bad <= 5u ? PK_NOTREADY + c->last_bad - 2u : PK_ERROR)
                       : !c->done ? PK_STUCK
                       : c->fb_open && !c->fb_seen ? (c->fb_odd ? (c->fb_k == 44u ? PK_FB44 : c->fb_k >= 88u && c->fb_k <= 96u ? PK_FB96 : PK_FBODD)
                                                     : c->fb_empty ? PK_FBEMPTY : PK_NOFB)
                       : mode < MODE_AUDIO ? PK_OK
                       : blocks == c->blocks_seen ? PK_NOINPUT
                       : c->xruns ? PK_XRUN : PK_OK;
            c->blocks_seen = blocks;
            c->xruns = 0;
            uint32_t m7 = ring_of(c)->spare1[1];
            /* Audio modes: "DEV FB raw:" shows the HSI48 TRIM x 100000 + the M7's ms in the last 10000 SOFs
             * (10000 when the DEV port's frames are 1 ms long); before the first 10 s the frames the M7 wrote this second. */
            uint32_t show = c->clk_shown ? ((CRS_CR >> 8) & 63u) * 100000u + c->clk_shown : m7 - c->m7_frames;
            LINE->channels = c->nrch | ((mode >= MODE_AUDIO ? show : c->fb_raw) & 0xffffffu) << 8;
            c->m7_frames = m7;
            c->sofs = c->done = c->bad = c->fb_seen = c->fb_odd = c->fb_empty = 0;
        }
    }
    return MIDI_PDATA || !c ? midi_sof(phost) : USBH_OK;
}

uint8_t host_init(void *), host_deinit(void *), host_requests(void *), host_bgnd(void *), host_sof(void *);
/* bl USBH_RegisterClass in MX_USB_HOST_Init: the class (Name, ClassCode,
 * Init, DeInit, Requests, BgndProcess, SOFProcess, pData) calls the wrappers. */
uint8_t host_register(void *phost, void *cls)
{
    volatile uint32_t *f = (volatile uint32_t *)cls;
    line()->ctx = 0;   /* a new boot: the heap is new, an old block address is stale */
    if (f[2] == FN(0x08105990u)) {
        f[2] = (uint32_t)host_init; f[3] = (uint32_t)host_deinit; f[4] = (uint32_t)host_requests;
        f[5] = (uint32_t)host_bgnd; f[6] = (uint32_t)host_sof;
    }
    return register_class(phost, cls);
}

uint8_t host_bgnd(void *phost) {return MIDI_PDATA || !ctx() ? midi_bgnd(phost) : USBH_OK;}

uint8_t host_deinit(void *phost)
{
    volatile struct host_line *l = line();
    struct host_ctx *c = ctx();
    if (c) {
        c->live = 0;   /* the SOF interrupt sees no context from here on; the block stays (the M7 stops with the heartbeat) */
        l->ctx = (uint32_t)ring_of(c);
        if (c->open) {close_pipe(phost, c->pipe); free_pipe(phost, c->pipe);}
        if (c->open2) {close_pipe(phost, c->pipe2); free_pipe(phost, c->pipe2);}
        if (c->fb_open) {close_pipe(phost, c->fb_pipe); free_pipe(phost, c->fb_pipe);}
        if (c->fb_open2) {close_pipe(phost, c->fb_pipe2); free_pipe(phost, c->fb_pipe2);}
    }
    if (l->mode >= 1u && l->mode <= MODE_LAST) {set_state(ST_WAIT); l->channels = 0;}
    l->pkts = PK_NONE;
    return MIDI_PDATA || !c ? midi_deinit(phost) : USBH_OK;
}
