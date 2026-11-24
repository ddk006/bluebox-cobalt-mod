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
 * UAC2 SET_CUR 48 kHz on its clock) and sends one packet of 48 frames per
 * SOF (1 ms). Stage 1: a 1 kHz sine at -12 dBFS on channels 1-2, 3-4 (the
 * S-4's track 1) or all of them, the other channels silent. Each second the
 * packet results are summed up for the USB page ("DEV Pkts:"). The device's feedback endpoint is not read yet.
 *
 * The packet goes through the periodic TX FIFO, 0xe0 words (896 bytes) after
 * the stock USB_HostInit (0x08117ac8, HS-core layout); a bigger packet moves
 * the non-periodic FIFO down to 0x80 words and gives the periodic one 0x100.
 * Buffers come from the FreeRTOS heap (pvPortMalloc 0x0811ba90) at Init and
 * go back at DeInit; the pointer and the state for the USB page sit in the
 * USB line of backup SRAM (0x38800ee0, struct pc_usb in src/pc_m4.c). */
#include "shared.h"

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
#define msg_put     ((int (*)(uint32_t, uint32_t, uint32_t))FN(0x0811b94au))
#define port_malloc ((void *(*)(uint32_t))FN(0x0811ba90u))
#define port_free   ((void (*)(void *))FN(0x0811bbc8u))

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
/* mode: 0 Off, 1 sine on 1-2, 2 on 3-4, 3 on all. pkts ("DEV Pkts:"), per second: */
enum {PK_NONE, PK_OK, PK_NOSOF, PK_STUCK, PK_NOTREADY, PK_NYET, PK_ERROR, PK_STALL};
#define LINE ((volatile struct host_line *)0x38800ee0u)
_Static_assert(sizeof(struct host_line) == 32, "the 0xee0 line");

#define CFG_MAX 1024u
#define PKT_MAX 1024u
#define FRAMES 48u   /* full speed: one packet per 1 ms frame at 48 kHz */
enum {R_CFG, R_ITF, R_RATE, R_OPEN, R_MIDI};
struct host_ctx {
    uint32_t magic;
    uint8_t step, pipe, open, inflight;
    uint8_t ep, itf, alt, ac_itf, clock, nrch, subslot, uac2;
    uint16_t maxp, len;
    uint32_t sent, late;
    uint32_t done, bad, last_bad, sofs, fill_mode;
    uint8_t rate[4];
    uint8_t cfg[CFG_MAX];
    uint8_t pkt[PKT_MAX];
};
#define CTX_MAGIC 0x48535431u

/* 1 kHz at 48 kHz is 48 samples a cycle: one packet holds one cycle. -12 dBFS. */
static const int16_t sine48[FRAMES] = {
    0, 1069, 2120, 3135, 4096, 4987, 5793, 6498, 7094, 7568, 7913, 8122,
    8192, 8122, 7913, 7568, 7094, 6498, 5793, 4987, 4096, 3135, 2120, 1069,
    0, -1069, -2120, -3135, -4096, -4987, -5793, -6498, -7094, -7568, -7913, -8122,
    -8192, -8122, -7913, -7568, -7094, -6498, -5793, -4987, -4096, -3135, -2120, -1069};

static volatile struct host_line *line(void) {backup_enable(); PWR_CR1 |= 1u << 8; return LINE;}
static struct host_ctx *ctx(void)
{
    uint32_t a = LINE->ctx;   /* written by every Init; checked anyway (backup SRAM outlives a reset) */
    if (a < 0x30000000u || a > 0x30048000u - sizeof(struct host_ctx) || (a & 3u)) return 0;
    struct host_ctx *c = (struct host_ctx *)a;
    return c->magic == CTX_MAGIC ? c : 0;
}
static void set_state(unsigned s) {line()->state = s;}
static void kick(void *phost) {msg_put(P32(phost, OS_EVENT), 1u, 0u);}

/* The device's audio OUT stream: the first alternate setting of an audio
 * streaming interface with an isochronous OUT endpoint and PCM of 2-4 bytes. */
static int parse(struct host_ctx *c, unsigned total)
{
    uint8_t term_id[8], term_clk[8];
    unsigned nterm = 0, num = 0, alt = 0, cls = 0, sub = 0, uac2 = 0, link = 0, nrch = 0, slot = 0, ep = 0, maxp = 0, ac = 0;
    for (unsigned i = 0; i + 2u <= total; ) {
        const uint8_t *d = c->cfg + i;
        unsigned len = d[0], type = d[1];
        if (len < 2u || i + len > total) break;
        if (type == 4u && len >= 9u) {
            if (cls == 1u && sub == 2u && alt && ep && nrch && slot >= 2u && slot <= 4u) break;   /* previous alt is it */
            num = d[2]; alt = d[3]; cls = d[5]; sub = d[6]; uac2 = d[7] == 0x20u;
            link = nrch = slot = ep = maxp = 0;
            if (cls == 1u && sub == 1u) ac = num;
        } else if (type == 0x24u && cls == 1u && sub == 1u && uac2 && len >= 8u && d[2] == 2u && nterm < 8u) {
            term_id[nterm] = d[3]; term_clk[nterm] = d[7]; nterm++;   /* UAC2 input terminal: id, clock source */
        } else if (type == 0x24u && cls == 1u && sub == 2u) {
            if (d[2] == 1u && uac2 && len >= 11u) {link = d[3]; nrch = d[10];}
            else if (d[2] == 2u && uac2 && len >= 6u) slot = d[4];
            else if (d[2] == 2u && !uac2 && len >= 8u) {nrch = d[4]; slot = d[5];}
        } else if (type == 5u && cls == 1u && sub == 2u && len >= 7u && !(d[2] & 0x80u) && (d[3] & 3u) == 1u) {
            ep = d[2]; maxp = (d[4] | d[5] << 8) & 0x7ffu;
        }
        i += len;
    }
    if (!(cls == 1u && sub == 2u && alt && ep && nrch && slot >= 2u && slot <= 4u)) return 0;
    c->itf = (uint8_t)num; c->alt = (uint8_t)alt; c->ep = (uint8_t)ep; c->maxp = (uint16_t)maxp;
    c->nrch = (uint8_t)nrch; c->subslot = (uint8_t)slot; c->uac2 = (uint8_t)uac2; c->ac_itf = (uint8_t)ac; c->clock = 0;
    for (unsigned k = 0; k < nterm; k++) if (term_id[k] == link) c->clock = term_clk[k];
    return 1;
}

/* One packet: the sine on channels 1 and 2 (top bytes of each subslot), the rest 0. */
static void fill(struct host_ctx *c, unsigned mode)
{
    uint8_t *p = c->pkt;
    c->fill_mode = mode;
    for (unsigned f = 0; f < FRAMES; f++)
        for (unsigned ch = 0; ch < c->nrch; ch++) {
            int on = mode == 3u || (mode == 1u && ch < 2u) || (mode == 2u && (ch == 2u || ch == 3u));
            int16_t v = on ? sine48[f] : 0;
            for (unsigned b = 0; b + 2u < c->subslot; b++) *p++ = 0;
            *p++ = (uint8_t)v; *p++ = (uint8_t)((uint16_t)v >> 8);
        }
}

/* Periodic TX FIFO deep enough for one packet (stock HS-core layout only). */
static int fifo(unsigned bytes)
{
    unsigned need = (bytes + 3u) / 4u;
    if ((OTG[HPTXFSIZ] >> 16) >= need) return 1;
    if (need > 0x100u || OTG[GRXFSIZ] != 0x200u || OTG[HNPTXFSIZ] != 0x01000200u) return 0;
    OTG[HNPTXFSIZ] = 0x80u << 16 | 0x200u;
    OTG[HPTXFSIZ] = 0x100u << 16 | 0x280u;
    for (unsigned n = 0; n < 100000u && !(OTG[GRSTCTL] & 1u << 31); n++) {}   /* AHB idle, as USB_FlushTxFifo */
    OTG[GRSTCTL] = 0x10u << 6 | 1u << 5;   /* flush all TX FIFOs */
    for (unsigned n = 0; n < 100000u && (OTG[GRSTCTL] & 1u << 5); n++) {}
    return 1;
}

uint8_t host_init(void *phost)
{
    volatile struct host_line *l = line();
    l->ctx = 0;
    l->pkts = PK_NONE;
    if (l->mode == 0u || l->mode > 3u) return midi_init(phost);   /* DEV Audio Off: the stock class */
    uint8_t r = midi_init(phost);
    struct host_ctx *c = port_malloc(sizeof *c);
    l->channels = 0;
    if (!c) {l->ctx = 0; set_state(ST_NOMEM); return r;}
    for (unsigned i = 0; i < sizeof *c; i++) ((uint8_t *)c)[i] = 0;
    c->magic = CTX_MAGIC;
    c->step = R_CFG;
    l->ctx = (uint32_t)c;
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
        c->len = (uint16_t)(FRAMES * c->nrch * c->subslot);
        if (c->len > c->maxp || c->len > PKT_MAX || !fifo(c->len)) {set_state(ST_BIG); c->step = R_MIDI; break;}
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
        c->step = R_OPEN;
        break;
    case R_OPEN:
        c->pipe = alloc_pipe(phost, c->ep);
        if (c->pipe == 0xffu) {set_state(ST_ERROR); c->step = R_MIDI; break;}
        open_pipe(phost, c->pipe, c->ep, P8(phost, DEV_ADDR), P8(phost, DEV_SPEED), 1u /* isochronous */, c->maxp);
        fill(c, LINE->mode);
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

/* SOF interrupt, every 1 ms: the next packet once the last one is out. */
uint8_t host_sof(void *phost)
{
    struct host_ctx *c = ctx();
    unsigned mode = LINE->mode;
    if (c && c->open && mode >= 1u && mode <= 3u) {
        unsigned st = c->inflight ? urb_state(phost, c->pipe) : 1u;
        if (c->inflight && st == URB_IDLE) c->late++;
        else {
            if (c->inflight) {if (st == 1u) c->done++; else {c->bad++; c->last_bad = st;}}
            if (c->fill_mode != mode) fill(c, mode);
            submit_urb(phost, c->pipe, 0 /* out */, 1u /* isochronous */, 1u /* data */, c->pkt, c->len, 0);
            c->inflight = 1;
            c->sent++;
        }
        if (++c->sofs >= 1000u) {   /* once a second: what happened to the packets */
            LINE->pkts = c->bad ? (c->last_bad >= 2u && c->last_bad <= 5u ? PK_NOTREADY + c->last_bad - 2u : PK_ERROR)
                       : c->done ? PK_OK : PK_STUCK;
            c->sofs = c->done = c->bad = 0;
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
        l->ctx = 0;   /* the SOF interrupt sees no context from here on */
        if (c->open) {close_pipe(phost, c->pipe); free_pipe(phost, c->pipe);}
        c->magic = 0;
        port_free(c);
    }
    if (l->mode >= 1u && l->mode <= 3u) {set_state(ST_WAIT); l->channels = 0;}
    l->pkts = PK_NONE;
    return MIDI_PDATA || !c ? midi_deinit(phost) : USBH_OK;
}
