/* USB audio out, channel choice (eurorack 1.5.1 only; built and patched by
 * the euro build through targets/euro.json, addresses are euro addresses).
 *
 * The bluebox eurorack is a UAC2 device (ST USBD, OTG_HS on the M7) that
 * sends the computer 18 channels of 24 bit: the engine (0x0804c778) packs
 * USB 1-12 from its 12 channel buffers and USB 13-18 from its six output
 * buses (13/14 the main mix), as 54-byte frames, and the audio loop writes
 * each block to the IN ring (bl 0x08066750 at 0x0804115a). The class DataIn
 * (0x08065db0) sends 6 +-1 frames per microframe from that ring.
 *
 * Here the user switches channels off (USB page on the M4, mask at
 * 0x38800ee0). With fewer than 18 the device says so: the config descriptor
 * (in RAM at 0x240000a8, read only while enumerating) gets the channel count
 * in input terminal 4 (+0x58) and interface 2's AS general (+0xc7) and a
 * smaller EP 0x82 max packet (+0xd7); the ring gets frames of 3 bytes per
 * channel kept. 16 bit instead of 24 (Format on the same page) also sets the
 * subslot size and bit resolution of interface 2 (+0xd1, +0xd2) to 2 and 16
 * and keeps the upper two bytes of each sample. A change soft-disconnects
 * (OTG DCTL.SDIS), edits the descriptor and reconnects, so the computer sees a
 * new device; the serial number carries the mask (and "-16") so it does not
 * mix up the variants. All 18 on (or none) at 24 bit is the stock device.
 *
 * Found independently in the disassembly; BlueLex's "USB Out" mod
 * (github.com/lexnels/bluelex-bluebox-euro, src/usb_m7.c) found the same
 * places for its 2-channel mode and was the cross-check. */
#include "shared.h"
#include "dev_ring.h"

#define FN(a) ((a) | 1u)
#define fw_ring_write ((int (*)(void *, uint8_t *, int))FN(0x08066750u))
#define fw_datain     ((uint8_t (*)(void *, uint8_t))FN(0x08065db0u))
#define fw_init       ((uint8_t (*)(void *, uint8_t))FN(0x080658c0u))
#define fw_tx         ((int (*)(void *, uint8_t, uint8_t *, uint32_t))FN(0x08066f48u))
#define fw_watchdog   ((void (*)(uint32_t, uint32_t, uint32_t, uint32_t))FN(0x080655dcu))
#define fw_serial     ((uint8_t *(*)(uint32_t, uint16_t *))FN(0x08066fecu))
#define fw_ms         ((uint32_t (*)(void))FN(0x0805e188u))

/* IN ring (*0x24000200, set up by the class Init 0x080658c0: size 0x2880,
 * frame 0x36): write +0x10, read +0x14, streaming flag +0x1c. */
struct usb_ring {uint8_t *buf; int32_t size, frame, c; int32_t wr, rd, x; uint32_t streaming;};
#define RING      (*(struct usb_ring *volatile *)0x24000200u)
#define TXBUF     (*(uint8_t *volatile *)0x2400bd00u)
#define RATE      (*(volatile uint32_t *)0x240001f8u)   /* 48000 while the host streams at 48 kHz */
#define ADJ       (*(volatile int32_t *)0x2400bc94u)    /* +-1 frame for the next packet */
#define IN_FED    (*(volatile uint32_t *)0x2400bd44u)   /* cleared by DataIn */
#define STARTED   (*(volatile uint32_t *)0x2400b9b4u)   /* host-loss watchdog state (0x080655dc) */
#define WD_SEEN   (*(volatile uint32_t *)0x2400b998u)
#define WD_STALL  (*(volatile uint32_t *)0x2400b9a0u)
#define WD_LAST   (*(volatile uint32_t *)0x2400b9a4u)
#define WD_SOFS   (*(volatile uint32_t *)0x2400b9a8u)
#define DCTL      (*(volatile uint32_t *)0x40040804u)   /* OTG_HS device control, bit 1 soft disconnect */
#define DESC      ((volatile uint8_t *)0x240000a8u)     /* config descriptor, 326 bytes */
#define SCB_DCIMVAC (*(volatile uint32_t *)0xe000ef5cu)

#define ALL 0x3ffffu   /* the stock device: all 18 channels, 24 bit */
#define F16 0x1000000u /* with a channel mask: 16 bit */
#define CHANNELS 18u
#define FRAME_ALL 54u
#define RING_FRAMES 192u   /* 4 ms, as the stock 0x2880-byte ring */

/* From the M4 (src/pc_m4.c struct pc_usb): its own cache line. */
struct usb_out {uint32_t magic, mask, format, spare[5];};   /* format 1: 16 bit */
#define USB_OUT ((volatile struct usb_out *)0x38800ee0u)
#define USB_OUT_MAGIC 0x55534231u
/* M7 only: mask|F16 in the descriptor, the switch in progress. */
struct usb_state {uint32_t magic, cur, phase, t0, want, spare[3];};
#define USB_STATE ((volatile struct usb_state *)0x38800ec0u)
#define USB_STATE_MAGIC 0x55534237u
_Static_assert(sizeof(struct usb_out) == 32 && sizeof(struct usb_state) == 32, "one cache line each");

static unsigned count(uint32_t m) {m &= ALL; unsigned n = 0; for (; m; m &= m - 1u) n++; return n;}
static unsigned bps(uint32_t m) {return m & F16 ? 2u : 3u;}   /* bytes per sample */
static uint32_t norm(uint32_t m) {uint32_t c = m & ALL; return (c ? c : ALL) | (m & F16);}
static volatile struct usb_state *state(void)
{
    backup_enable();
    PWR_CR1 |= 1u << 8;
    volatile struct usb_state *s = USB_STATE;
    if (s->magic != USB_STATE_MAGIC) {s->cur = ALL; s->phase = 0; s->t0 = 0; s->want = ALL; s->magic = USB_STATE_MAGIC;}
    return s;
}
/* The mask the descriptor says: after a reset the descriptor is stock again. */
static uint32_t current(void)
{
    volatile struct usb_state *s = state();
    uint32_t m = norm(s->cur);
    return DESC[0x58] == count(m) && DESC[0xd1] == bps(m) ? m : ALL;
}
static uint32_t wanted(void)
{
    SCB_DCIMVAC = 0x38800ee0u;
    __asm volatile("dsb\nisb" ::: "memory");
    volatile struct usb_out *o = USB_OUT;
    return o->magic == USB_OUT_MAGIC ? norm(o->mask | (o->format == 1u ? F16 : 0u)) : ALL;
}
static void apply(uint32_t m)
{
    unsigned n = count(m), maxp = m == ALL ? 0x180u : 8u * bps(m) * n;   /* 8 frames, the stock 384 for 18 */
    DESC[0x58] = (uint8_t)n;
    DESC[0xc7] = (uint8_t)n;
    DESC[0xd1] = (uint8_t)bps(m);
    DESC[0xd2] = (uint8_t)(8u * bps(m));
    DESC[0xd7] = (uint8_t)maxp;
    DESC[0xd8] = (uint8_t)(maxp >> 8);
    state()->cur = m;
}

/* DEV port audio (dev_ring.h): the block's 18 channels as 16 bit into the
 * M4's ring while its heartbeat runs. The heartbeat (low bits of the ring
 * address in the USB line) counts only when it changed at most 50 ms ago;
 * four changes in a row are needed, so neither a stale address in backup SRAM
 * after a reset nor one late change is written to. Kept in this core's own
 * line (USB_STATE spare: last beat, its time, beats in a row). */
#define DEV_LINE ((volatile uint32_t *)0x38800ee0u)   /* struct pc_usb: +0x18 the ring address | heartbeat */
#define DEV_BEATS 4u
#define DEV_BEAT_MS 50u
static void dev_feed(const uint8_t *src, int bytes)
{
    SCB_DCIMVAC = 0x38800ee0u;
    __asm volatile("dsb\nisb" ::: "memory");
    uint32_t a = DEV_LINE[6], hb = a & DEV_HB_MASK, now = fw_ms();
    volatile struct usb_state *s = state();
    uint32_t last = s->spare[0], t = s->spare[1], n = s->spare[2];
    if (now < t || now - t > DEV_BEAT_MS) n = 0;   /* no beat lately, or this core restarted */
    if (hb != last) {s->spare[0] = hb; s->spare[1] = now; if (n < DEV_BEATS) n++;}
    s->spare[2] = n;
    if (n < DEV_BEATS) return;
    a &= ~31u;
    if (a < DEV_RAM_LO || a > DEV_RAM_HI - sizeof(struct dev_ring)) return;
    struct dev_ring *r = (struct dev_ring *)a;
    SCB_DCIMVAC = a;   /* line 0 is the M4's */
    __asm volatile("dsb\nisb" ::: "memory");
    if (r->magic != DEV_RING_MAGIC) return;
    unsigned frames = (unsigned)bytes / FRAME_ALL, wr = r->wr % DEV_RING_FRAMES;
    for (unsigned i = 0; i < frames; i++) {
        const uint8_t *f = src + i * FRAME_ALL;
        int16_t *d = &r->data[wr * DEV_SRC];
        for (unsigned c = 0; c < DEV_SRC; c++) d[c] = (int16_t)(f[3 * c + 1] | f[3 * c + 2] << 8);
        SCB_DCCMVAC = (uint32_t)d;
        SCB_DCCMVAC = (uint32_t)d + DEV_SRC * 2u - 1u;
        wr = wr + 1u == DEV_RING_FRAMES ? 0u : wr + 1u;
    }
    __asm volatile("dsb" ::: "memory");   /* the data before the write position */
    r->wr = wr;
    r->blocks++;
    SCB_DCCMVAC = (uint32_t)&r->wr;
    __asm volatile("dsb" ::: "memory");
}

/* bl 0x08066750 at 0x0804115a: (ring, block, bytes). First the DEV port copy;
 * then keep the chosen channels (at 16 bit the upper two bytes of each
 * little-endian 24-bit sample). */
int usb_push(struct usb_ring *r, uint8_t *src, int bytes)
{
    dev_feed(src, bytes);
    uint32_t m = current();
    if (m != ALL) {
        unsigned f = (unsigned)bytes / FRAME_ALL;
        uint8_t *d = src;
        for (unsigned i = 0; i < f; i++) {
            const uint8_t *s = src + i * FRAME_ALL;
            for (unsigned c = 0; c < CHANNELS; c++)
                if (!(m >> c & 1u)) continue;
                else if (m & F16) {d[0] = s[3 * c + 1]; d[1] = s[3 * c + 2]; d += 2;}
                else {d[0] = s[3 * c]; d[1] = s[3 * c + 1]; d[2] = s[3 * c + 2]; d += 3;}
        }
        bytes = (int)(d - src);
    }
    if (r->wr < 0 || r->wr >= r->size) r->wr = 0;
    return fw_ring_write(r, src, bytes);
}

/* Class DataIn (slot 0x08076c40), EP 0x82 with fewer channels or 16 bit: the
 * stock routine (6 +-1 frames from the ring, silence when the host is not at
 * 48 kHz) for frames of 3 (2) bytes per channel. */
uint8_t usb_datain(void *pdev, uint8_t ep)
{
    uint32_t m = current();
    if (m == ALL || (ep & 0x7fu) != 2u) return fw_datain(pdev, ep);
    int frame = (int)(bps(m) * count(m));
    IN_FED = 0;
    int want = (6 + ADJ) * frame;
    ADJ = 0;
    uint8_t *tx = TXBUF;
    struct usb_ring *r = RING;
    if (RATE != 48000u) {
        for (int i = 0; i < want; i++) tx[i] = 0;
        fw_tx(pdev, 0x82, tx, (uint32_t)want);
        if (r->streaming) r->streaming = 0;
        return 0;
    }
    int size = r->size, rd = r->rd;
    if (rd < 0 || rd >= size) rd = 0;
    int avail = r->wr - rd;
    if (avail < 0) avail += size;
    int n = want < avail ? want : avail;
    for (int i = 0; i < n; i++) {tx[i] = r->buf[rd]; if (++rd == size) rd = 0;}
    r->rd = rd;
    fw_tx(pdev, 0x82, tx, (uint32_t)n);
    if (r->streaming != 1u) r->streaming = 1;
    return 0;
}

/* The three fixed 6-frame (324-byte) sends on EP 0x82: SET_INTERFACE
 * (0x08065ab4), iso IN incomplete (0x08065d1a), SOF watchdog (0x08066640). */
int usb_tx82(void *pdev, uint8_t ep, uint8_t *buf, uint32_t len)
{
    uint32_t m = current();
    return fw_tx(pdev, ep, buf, m == ALL ? len : 6u * bps(m) * count(m));
}

/* Class Init (slot 0x08076c2c), on SET_CONFIGURATION: the ring for the smaller frames. */
uint8_t usb_init(void *pdev, uint8_t cfg)
{
    uint8_t ret = fw_init(pdev, cfg);
    uint32_t m = current();
    if (m != ALL) {
        struct usb_ring *r = RING;
        int frame = (int)(bps(m) * count(m));
        r->frame = frame;
        r->size = (int)RING_FRAMES * frame;
        r->wr = 0;
        r->rd = 0;
    }
    return ret;
}

/* Serial string (descriptor callback slot 0x08076c88): "-" and the mask in
 * hex appended (and "-16" at 16 bit), so the computer keeps each variant apart. */
uint8_t *usb_serial(uint32_t speed, uint16_t *len)
{
    uint8_t *s = fw_serial(speed, len);
    uint32_t m = current();
    if (m != ALL && s && *len >= 2u && *len < 100u) {
        unsigned at = *len;
        s[at] = '-'; s[at + 1] = 0; at += 2;
        for (int k = 16; k >= 0; k -= 4) {
            unsigned d = m >> k & 15u;
            s[at] = (uint8_t)(d < 10u ? '0' + d : 'A' + d - 10u); s[at + 1] = 0; at += 2;
        }
        if (m & F16) {s[at] = '-'; s[at + 2] = '1'; s[at + 4] = '6'; s[at + 1] = s[at + 3] = s[at + 5] = 0; at += 6;}
        *len = (uint16_t)at;
        s[0] = (uint8_t)at;
    }
    return s;
}

/* bl 0x080655dc at 0x08041166: the host-loss watchdog, every other block.
 * When the wanted mask differs from the descriptor: disconnect, 20 ms later
 * edit, reconnect at 250 ms with the watchdog reset (it sits out meanwhile,
 * it would restart USB on the missing SOFs). Before USB runs, just edit. */
void usb_tick(uint32_t a, uint32_t b, uint32_t c, uint32_t d)
{
    volatile struct usb_state *s = state();
    uint32_t now = fw_ms();
    if (s->phase == 1u) {
        if (now - s->t0 >= 20u) {apply(s->want); s->phase = 2;}
        return;
    }
    if (s->phase == 2u) {
        if (now - s->t0 >= 250u) {
            WD_STALL = 0; WD_SEEN = 0; WD_LAST = WD_SOFS;
            DCTL &= ~2u;
            s->phase = 0;
        }
        return;
    }
    s->phase = 0;
    uint32_t w = wanted();
    if (w != current()) {
        if (!STARTED) apply(w);
        else {s->want = w; DCTL |= 2u; s->t0 = now; s->phase = 1; return;}
    }
    fw_watchdog(a, b, c, d);
}
