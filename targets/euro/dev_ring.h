/* DEV port audio ring: the M7 audio loop (targets/euro/usb_m7.c, usb_push)
 * writes every block of the 18 bluebox USB channels (USB 1-12 the channel
 * buffers, 13/14 the main mix, 15-18 the other output buses) as 16 bit; the
 * M4 DEV port driver (targets/euro/usbh_m4.c) reads 47-49 frames per ms for
 * the device on the DEV port and routes them to its channels ("DEV 1:" ..
 * "DEV 10:" on the USB page).
 *
 * The ring lives in the M4 heap (D2 SRAM, allocated once per boot, never
 * freed). The USB line of backup SRAM (0x38800ee0, +0x18) holds its address
 * with a heartbeat in the low 3 bits: the M4 changes it every 8 ms while it
 * streams, and the M7 writes only while it changes (and not across its own
 * reboot), so a stale address from before a reset is never written.
 *
 * Cache: the M7 caches D2 SRAM, the M4 has no cache. Line 0 is written by the
 * M4 only (the M7 invalidates it before reading), line 1 and the data by the
 * M7 only (it cleans them after writing, data before wr). */
#ifndef DEV_RING_H
#define DEV_RING_H
#include <stdint.h>

#define DEV_RING_MAGIC 0x52564544u   /* "DEVR" */
#define DEV_SRC 18u                  /* sources per frame */
#define DEV_RING_FRAMES 256u         /* 5.3 ms */
struct dev_ring {
    /* line 0, M4: magic, the driver context behind the ring */
    uint32_t magic, ctx, spare0[6];
    /* line 1, M7: frames written (mod DEV_RING_FRAMES), blocks written */
    uint32_t wr, blocks, spare1[6];
    int16_t data[DEV_RING_FRAMES * DEV_SRC];
};
_Static_assert(sizeof(struct dev_ring) % 32u == 0u, "whole cache lines");
#define DEV_RAM_LO 0x30000000u
#define DEV_RAM_HI 0x30048000u   /* D2 SRAM1-3 */
#define DEV_HB_MASK 7u
#endif
