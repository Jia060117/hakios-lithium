/*
 * MIX / MSM8996 warm DWC3 USB log device. Derived from ios-sailfish
 * patches 0072/0073 (same XNU 8796 interfaces); source hashes in usb-provenance.json.
 * EP0 follows that component. Board gates, bulk log transport and lifecycle
 * are lithium additions. No PHY reset, clock enable, host commands or flash I/O.
 * lithiumusb=0 disabled, 1 read-only probe, 2 warm device takeover + log stream.
 * If power/clocks/device mode are absent this returns without touching DWC3.
 */
#if defined(LITHIUM_ARMV8_0)
#include <sys/param.h>
#include <sys/systm.h>
#include <kern/clock.h>
#include <kern/startup.h>
#include <machine/atomic.h>
#include <kern/thread.h>
#include <kern/sched_prim.h>
#include <machine/machine_routines.h>
#include <arm/caches_internal.h>
#include <vm/pmap.h>
#include <vm/vm_kern.h>
#include <pexpert/pexpert.h>

extern vm_offset_t ml_io_map(vm_offset_t, vm_size_t);
extern kern_return_t thread_terminate(thread_t);
void lithium_usb_probe(void);
static void lithium_usb_gadget_start(vm_offset_t dwc);
extern ppnum_t pmap_find_phys(pmap_t pmap, addr64_t va);

/* MIX original DT + Linux v6.12 gcc-msm8996.c, not the Sweet offsets. */
#define GCC_PA 0x00300000
#define GCC_SIZE 0x00090000
#define GCC_USB30_PRIM_GDSCR 0x0f004
#define CBCR_CLK_OFF (1u << 31)
static const struct { const char *name; uint32_t off; } usb_cbcr[] = {
    { "master", 0x0f008 }, { "sleep", 0x0f00c },
    { "utmi", 0x0f010 }, { "aggre-axi", 0x83018 },
};
#define DWC3_PA 0x06a00000
#define DWC3_SIZE 0x0000d000

static uint32_t
usb_rd(vm_offset_t base, uint32_t off)
{
	uint32_t v = *(volatile uint32_t *)(base + off);
	__asm__ volatile ("dmb oshld" ::: "memory");
	return v;
}

void
lithium_usb_probe(void)
{
	int enabled = 0;
	uint32_t gdscr, cbcr[sizeof(usb_cbcr) / sizeof(usb_cbcr[0])];
	bool clocks_on = true;

	/* Explicit opt-in until warm takeover is verified on this board. */
	PE_parse_boot_argn("lithiumusb", &enabled, sizeof(enabled));
	if (!enabled) {
		return;
	}
	vm_offset_t gcc = ml_io_map(GCC_PA, GCC_SIZE);
	if (gcc == 0) {
		printf("LITHIUM USB: GCC map failed\n");
		return;
	}
	gdscr = usb_rd(gcc, GCC_USB30_PRIM_GDSCR);
	printf("LITHIUM USB: GDSC usb30_prim 0x%08x (%s)\n", gdscr, (gdscr & (1u << 31)) ? "on" : "off");
	for (unsigned i = 0; i < sizeof(usb_cbcr) / sizeof(usb_cbcr[0]); i++) {
		cbcr[i] = usb_rd(gcc, usb_cbcr[i].off);
		printf("LITHIUM USB: clk %-12s CBCR 0x%08x (%s)\n", usb_cbcr[i].name, cbcr[i],
		    (cbcr[i] & CBCR_CLK_OFF) ? "off" : "on");
	}
	/* DWC3 registers need the GDSC, the master clock and the config-NoC path. */
	clocks_on = (gdscr & (1u << 31));
	for (unsigned i=0; i<sizeof(cbcr)/sizeof(cbcr[0]); i++) clocks_on &= !(cbcr[i] & CBCR_CLK_OFF);
	if (!clocks_on) {
		printf("LITHIUM USB: controller unpowered or unclocked; DWC3 not read\n");
		return;
	}
	vm_offset_t dwc = ml_io_map(DWC3_PA, DWC3_SIZE);
	if (dwc == 0) {
		printf("LITHIUM USB: DWC3 map failed\n");
		return;
	}
	uint32_t id=usb_rd(dwc,0xc120), mode=(usb_rd(dwc,0xc110)>>12)&3;
	if ((id & 0xffff0000u)!=0x55330000u || mode!=2) {
		printf("LITHIUM USB: unsupported core/mode %08x/%u; skipped\n",id,mode); return;
	}
	printf("LITHIUM USB: DWC3 GSNPSID 0x%08x GCTL 0x%08x GSTS 0x%08x GUSB2PHYCFG 0x%08x\n",
	    usb_rd(dwc, 0xc120), usb_rd(dwc, 0xc110), usb_rd(dwc, 0xc118), usb_rd(dwc, 0xc200));
	printf("LITHIUM USB: DWC3 DCFG 0x%08x DCTL 0x%08x DEVTEN 0x%08x DSTS 0x%08x DALEPENA 0x%08x\n",
	    usb_rd(dwc, 0xc700), usb_rd(dwc, 0xc704), usb_rd(dwc, 0xc708), usb_rd(dwc, 0xc70c),
	    usb_rd(dwc, 0xc720));
	printf("LITHIUM USB: DWC3 GEVNTADR 0x%08x%08x GEVNTSIZ 0x%08x GEVNTCOUNT 0x%08x GHWPARAMS0 0x%08x\n",
	    usb_rd(dwc, 0xc404), usb_rd(dwc, 0xc400), usb_rd(dwc, 0xc408), usb_rd(dwc, 0xc40c),
	    usb_rd(dwc, 0xc140));
	if (enabled >= 2) {
		lithium_usb_gadget_start(dwc);
	}
}

/*
 * USB2 EP0 plus an IN-only bulk log endpoint (lithiumusb=2). If LK leaves
 * the DWC3 powered, clocked, in device mode and halted, with the PHY set
 * up, so only the device controller is restarted: soft reset, an event
 * buffer in our memory, endpoint 0, run. Events are polled from a kernel
 * thread (no interrupt), like the UFS driver. Buffers are explicitly cleaned/invalidated; cache coherence is not assumed.
 * Register layout and the control-transfer state machine follow the
 * Synopsys DWC3 databook as used by Linux drivers/usb/dwc3 (gadget.c,
 * ep0.c).
 */
#define GCTL            0xc110
#define GEVNTADRLO      0xc400
#define GEVNTADRHI      0xc404
#define GEVNTSIZ        0xc408
#define GEVNTCOUNT      0xc40c
#define DCFG            0xc700
#define DCTL            0xc704
#define DEVTEN          0xc708
#define DSTS            0xc70c
#define DALEPENA        0xc720
#define DEPCMDPAR2(n)   (0xc800 + 0x10 * (n))
#define DEPCMDPAR1(n)   (0xc804 + 0x10 * (n))
#define DEPCMDPAR0(n)   (0xc808 + 0x10 * (n))
#define DEPCMD(n)       (0xc80c + 0x10 * (n))

#define DCTL_RUN_STOP   (1u << 31)
#define DCTL_CSFTRST    (1u << 30)
#define DSTS_DEVCTRLHLT (1u << 22)

#define DEPCMD_SETEPCONFIG      1
#define DEPCMD_SETTRANSFRESOURCE 2
#define DEPCMD_SETSTALL         4
#define DEPCMD_STARTTRANSFER    6
#define DEPCMD_DEPSTARTCFG      9
#define DEPCMD_CMDACT           (1u << 10)

#define TRB_HWO         (1u << 0)
#define TRB_LST         (1u << 1)
#define TRB_ISP_IMI     (1u << 10)
#define TRB_IOC         (1u << 11)
#define TRBCTL_SETUP    (2u << 4)
#define TRBCTL_STATUS2  (3u << 4)
#define TRBCTL_STATUS3  (4u << 4)
#define TRBCTL_DATA     (5u << 4)

/* DMA area layout */
#define UD_EVT          0x0000          /* event buffer, 4 KiB */
#define UD_EVT_SIZE     0x1000
#define UD_TRB_OUT      0x1000          /* ep0 OUT TRB */
#define UD_TRB_IN       0x1040          /* ep0 IN TRB, separate cache line */
#define UD_SETUP        0x10c0          /* 8-byte setup packet, separate cache line */
#define UD_DATA         0x1100          /* ep0 data stage, 512 bytes */
#define UD_TRB_TX       0x1080
#define UD_TX           0x2000
#define UD_SIZE         0x4000
#define EP_TX           3
#define TRBCTL_NORMAL   (1u << 4)

static struct {
	vm_offset_t dwc;
	vm_offset_t dma;
	uint64_t    dma_pa;
	uint32_t    evt_pos;
	int         sleep_chan;
	enum { EP0_SETUP, EP0_DATA, EP0_WAIT_STATUS2, EP0_WAIT_STATUS3, EP0_STATUS } state;
	bool        data_in;        /* data stage direction */
	uint32_t    events;
	bool configured, tx_busy;
	volatile uint32_t stopped;
	uint32_t packet_size, old_gctl;
	uint64_t log_cursor, pending_cursor, dropped;
} ud;

/* Published only after a completed host bulk transfer. Normal timer return
 * can wait while scheduling/interrupts still work; panic never waits here. */
static _Atomic uint64_t ud_log_delivered;
/* Independent Fastboot-return observations. Keep the existing transport and
 * drain API unchanged; an old bus session's completed cursor must never grant
 * the new terminal action. Counters are software-only, with no new MMIO. */
static _Atomic uint64_t ud_fb_session = 1, ud_fb_done_session;
static _Atomic uint64_t ud_fb_delivered, ud_fb_lost;
static uint64_t ud_fb_pending_session;
static void ud_sleep_ms(unsigned ms);

uint64_t lithium_usb_log_session(void);
uint64_t lithium_usb_log_delivered(void);
uint64_t lithium_usb_log_lost_total(void);

uint64_t
lithium_usb_log_session(void)
{
    return os_atomic_load(&ud_fb_session, acquire);
}

uint64_t
lithium_usb_log_delivered(void)
{
    uint64_t session = os_atomic_load(&ud_fb_session, acquire);
    uint64_t done = os_atomic_load(&ud_fb_done_session, acquire);
    uint64_t delivered = os_atomic_load(&ud_fb_delivered, acquire);
    return session != 0 && session == done &&
        session == os_atomic_load(&ud_fb_session, acquire) ? delivered : 0;
}

uint64_t
lithium_usb_log_lost_total(void)
{
    return os_atomic_load(&ud_fb_lost, acquire);
}

static void
ud_fb_new_session(void)
{
    (void)os_atomic_add(&ud_fb_session, 1, acq_rel);
    os_atomic_store(&ud_fb_done_session, 0, release);
}

bool lithium_usb_log_drain(uint64_t target, unsigned milliseconds);
bool
lithium_usb_log_drain(uint64_t target, unsigned milliseconds)
{
    uint64_t deadline;
    if (!target || milliseconds > 5000) return false;
    clock_interval_to_deadline(milliseconds, NSEC_PER_MSEC, &deadline);
    while (os_atomic_load(&ud_log_delivered, acquire) < target) {
        if (os_atomic_load(&ud.stopped, acquire) || mach_absolute_time() >= deadline) return false;
        ud_sleep_ms(25);
    }
    return true;
}

static inline void
dwc_wr(uint32_t off, uint32_t v)
{
	*(volatile uint32_t *)(ud.dwc + off) = v;
	__asm__ volatile ("dsb sy" ::: "memory");
}

static inline uint32_t
dwc_rd(uint32_t off)
{
	return usb_rd(ud.dwc, off);
}

static void
ud_sync(uint32_t off, uint32_t len)
{
	flush_dcache64((addr64_t)(ud.dma_pa + off), len, TRUE);
}

static void
ud_sleep_ms(unsigned ms)
{
	uint64_t deadline;
	clock_interval_to_deadline(ms, NSEC_PER_MSEC, &deadline);
	(void)assert_wait_deadline(&ud.sleep_chan, THREAD_UNINT, deadline);
	thread_block(THREAD_CONTINUE_NULL);
}

static int
ud_epcmd(unsigned ep, uint32_t cmd, uint32_t p0, uint32_t p1, uint32_t p2)
{
	dwc_wr(DEPCMDPAR0(ep), p0);
	dwc_wr(DEPCMDPAR1(ep), p1);
	dwc_wr(DEPCMDPAR2(ep), p2);
	dwc_wr(DEPCMD(ep), cmd | DEPCMD_CMDACT);
	for (int i = 0; i < 1000; i++) {
		uint32_t v = dwc_rd(DEPCMD(ep));
		if (!(v & DEPCMD_CMDACT)) {
			uint32_t status = (v >> 12) & 0xf;
			if (status) {
				printf("LITHIUM USB: ep%u cmd 0x%x status %u\n", ep, cmd & 0xf, status);
				return -1;
			}
			return 0;
		}
		delay(1);
	}
	printf("LITHIUM USB: ep%u cmd 0x%x timeout\n", ep, cmd & 0xf);
	return -1;
}

/* One TRB on physical endpoint ep (0 = ep0 OUT, 1 = ep0 IN). */
static int
ud_start_trb(unsigned ep, uint32_t trb, uint32_t buf, uint32_t len, uint32_t ctl)
{
	volatile uint32_t *t = (volatile uint32_t *)(ud.dma + trb);
	uint64_t bpa = ud.dma_pa + buf;

	t[0] = (uint32_t)bpa;
	t[1] = (uint32_t)(bpa >> 32);
	t[2] = len;
	t[3] = ctl | TRB_HWO | TRB_LST | TRB_ISP_IMI | TRB_IOC;
	ud_sync(trb, 16);
	if (len) {
		ud_sync(buf, len);
	}
	uint64_t tpa = ud.dma_pa + trb;
	return ud_epcmd(ep, DEPCMD_STARTTRANSFER, (uint32_t)(tpa >> 32), (uint32_t)tpa, 0);
}

static void
ud_start(unsigned ep, uint32_t buf, uint32_t len, uint32_t ctl)
{
    if (ud_start_trb(ep,ep ? UD_TRB_IN : UD_TRB_OUT,buf,len,ctl)) {
        os_atomic_store(&ud.stopped,1,release);
    }
}

static void
ud_start_setup(void)
{
	ud.state = EP0_SETUP;
	ud_start(0, UD_SETUP, 8, TRBCTL_SETUP);
}

static void
ud_stall(void)
{
	ud_epcmd(0, DEPCMD_SETSTALL, 0, 0, 0);
	ud_start_setup();
}

/* Descriptors. VID 0x1209 / PID 0x0001 = pid.codes test ID, not a product. */
static const uint8_t ud_dev_desc[18] = {
	18, 1, 0x00, 0x02, 0x00, 0x00, 0x00, 64,
	0x09, 0x12, 0x01, 0x00, 0x00, 0x01, 1, 2, 3, 1,
};
/* Private development VID/PID from the source component; not a product ID. */
static const uint8_t ud_cfg_desc[25] = {
    9,2,25,0,1,1,0,0x80,250,
    9,4,0,0,1,0xff,0,0,0,
    7,5,0x81,2,0,2,0,
};
static const uint8_t ud_qualifier[10] = {10,6,0,2,0,0,0,64,1,0};
static const uint8_t ud_ms_string[18] = {18,3,'M',0,'S',0,'F',0,'T',0,'1',0,'0',0,'0',0,0x20,0};
/* Microsoft OS 1.0 compatible ID descriptor: automatically select WinUSB. */
static const uint8_t ud_ms_compat[40] = {
    40,0,0,0,0,1,4,0,1,0,0,0,0,0,0,0,
    0,1,'W','I','N','U','S','B',0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
};
static const char *const ud_strings[] = {NULL,"HakiOS","MIX USB log (experimental)","lithium-log-1"};

extern uint32_t lithium_log_copy(uint64_t *, char *, uint32_t, uint64_t *);

static int
ud_configure(uint16_t value)
{
    if (value==0) { ud_fb_new_session(); ud.configured=false; return 0; }
    if (value!=1) return -1;
    if (ud.configured) return 0;
    uint32_t mps=ud.packet_size;
    if (ud_epcmd(0, DEPCMD_DEPSTARTCFG | (2u << 16),0,0,0) ||
        ud_epcmd(EP_TX,DEPCMD_SETEPCONFIG,(2u<<1)|(mps<<3)|(1u<<17),(1u<<8)|(EP_TX<<25),0) ||
        ud_epcmd(EP_TX,DEPCMD_SETTRANSFRESOURCE,1,0,0)) return -1;
    dwc_wr(DALEPENA,dwc_rd(DALEPENA)|(1u<<EP_TX));
    ud.configured=true;
    printf("LITHIUM USB: WinUSB log endpoint configured mps=%u\n",mps);
    return 0;
}

/* Frames: magic[8], absolute offset u64, dropped u64, length u32, flags u32,
 * then bytes. Offset/drop accounting allows host detection of gaps/duplicates.
 * Keep each transfer short of a packet multiple so a host read completes. */
static void
ud_log_kick(void)
{
    if (!ud.configured || ud.tx_busy || os_atomic_load(&ud.stopped,acquire)) return;
    uint8_t *data=(uint8_t *)(ud.dma+UD_TX);
    uint64_t pos=ud.log_cursor, lost=0;
    uint32_t n=lithium_log_copy(&pos,(char *)data+32,4063,&lost);
    if (!n) return;
    if ((n+32)%ud.packet_size==0) { n--; pos--; }
    uint64_t begin=pos-n;
    uint32_t flags=0;
    memcpy(data,"LITHLOG1",8); memcpy(data+8,&begin,8); memcpy(data+16,&lost,8);
    memcpy(data+24,&n,4); memcpy(data+28,&flags,4);
    ud.pending_cursor=pos; ud.dropped=lost;
    ud_fb_pending_session = os_atomic_load(&ud_fb_session, acquire);
    if (ud_start_trb(EP_TX,UD_TRB_TX,UD_TX,n+32,TRBCTL_NORMAL)==0) ud.tx_busy=true;
    else os_atomic_store(&ud.stopped,1,release);
}

static uint32_t
ud_string(unsigned idx, uint8_t *out)
{
	if (idx == 0) {
		out[0] = 4; out[1] = 3; out[2] = 0x09; out[3] = 0x04;   /* en-US */
		return 4;
	}
	if (idx >= sizeof(ud_strings) / sizeof(ud_strings[0])) {
		return 0;
	}
	const char *str = ud_strings[idx];
	uint32_t n = 2;
	for (; *str && n < 126; str++) {
		out[n++] = (uint8_t)*str;
		out[n++] = 0;
	}
	out[0] = (uint8_t)n;
	out[1] = 3;
	return n;
}

static void
ud_setup(void)
{
	const uint8_t *st = (const uint8_t *)(ud.dma + UD_SETUP);
	uint8_t *data = (uint8_t *)(ud.dma + UD_DATA);
	uint8_t type = st[0], req = st[1];
	uint16_t value = (uint16_t)(st[2] | st[3] << 8);
	uint16_t length = (uint16_t)(st[6] | st[7] << 8);
	uint32_t n = 0;
	bool ok = true;

	uint16_t index=(uint16_t)(st[4]|st[5]<<8);
	ud.data_in = (type & 0x80) != 0;
	if (type==0xc0 && req==0x20 && index==4) {
		memcpy(data,ud_ms_compat,sizeof(ud_ms_compat)); n=sizeof(ud_ms_compat);
	} else if ((type & 0x60) != 0) {
		ok = false;
	} else if (req == 6) {          /* GET_DESCRIPTOR */
		switch (value >> 8) {
		case 1: memcpy(data, ud_dev_desc, sizeof(ud_dev_desc)); n = sizeof(ud_dev_desc); break;
		case 2: case 7:
            memcpy(data,ud_cfg_desc,sizeof(ud_cfg_desc)); n=sizeof(ud_cfg_desc); data[1]=value>>8;
            uint32_t mps=(value>>8)==7 ? (ud.packet_size==512?64:512) : ud.packet_size;
            data[22]=mps; data[23]=mps>>8; break;
        case 6: memcpy(data,ud_qualifier,sizeof(ud_qualifier)); n=sizeof(ud_qualifier); break;
		case 3:
            if ((value&0xff)==0xee) {memcpy(data,ud_ms_string,sizeof(ud_ms_string));n=sizeof(ud_ms_string);}
            else n=ud_string(value&0xff,data);
            ok=n!=0; break;
		default: ok = false; break;
		}
	} else if (req == 5) {          /* SET_ADDRESS: takes effect now (DWC3 handles the timing) */
		uint32_t dcfg = dwc_rd(DCFG) & ~(0x7fu << 3);
		dwc_wr(DCFG, dcfg | ((uint32_t)(value & 0x7f) << 3));
		printf("LITHIUM USB: address %u\n", value & 0x7f);
	} else if (req == 9) {          /* SET_CONFIGURATION */
		ok=ud_configure(value)==0;
	} else if (req == 8) {          /* GET_CONFIGURATION */
		data[0] = ud.configured; n = 1;
	} else if (req == 0) {          /* GET_STATUS */
		data[0] = 0; data[1] = 0; n = 2;
	} else if (req == 10) {         /* GET_INTERFACE */
        data[0]=0; n=1;
    } else if (req == 11) {         /* SET_INTERFACE */
        ok=value==0 && index==0;
	} else {
		ok = false;
	}
	if (!ok) {
		ud_stall();
		return;
	}
	if (length == 0) {
		ud.state = EP0_WAIT_STATUS2;    /* status stage on XferNotReady */
		return;
	}
	ud.state = EP0_DATA;
	if (ud.data_in) {
		if (n > length) {
			n = length;
		}
		ud_start(1, UD_DATA, n, TRBCTL_DATA);
	} else {
		ud_start(0, UD_DATA, 64, TRBCTL_DATA);  /* OUT data is not used here */
	}
}

static void
ud_event(uint32_t ev)
{
	if (ev & 1) {                   /* device event */
		uint32_t type = (ev >> 8) & 0xf;
		if (type == 1) {        /* USB reset */
			dwc_wr(DCFG, dwc_rd(DCFG) & ~(0x7fu << 3));
            ud_fb_new_session();
            ud.configured=false; ud.tx_busy=false; ud.log_cursor=0; ud.pending_cursor=0;
            dwc_wr(DALEPENA,3);
            printf("LITHIUM USB: bus reset\n");
		} else if (type == 2) { /* connect done */
			uint32_t speed = dwc_rd(DSTS) & 7;
            ud.packet_size=speed==0?512:64;
			/* ep0 max packet: 64 at high/full speed (modify action) */
			ud_epcmd(0, DEPCMD_SETEPCONFIG, (0u << 1) | (64u << 3) | (2u << 30),
			    (1u << 8) | (1u << 10) | (0u << 25), 0);
			ud_epcmd(1, DEPCMD_SETEPCONFIG, (0u << 1) | (64u << 3) | (2u << 30),
			    (1u << 8) | (1u << 10) | (1u << 25), 0);
			printf("LITHIUM USB: connected, speed %u\n", speed);
		} else if (type == 0) {
			ud_fb_new_session();
			ud.configured=false; ud.tx_busy=false;
            printf("LITHIUM USB: disconnect\n");
		}
		return;
	}
	unsigned ep = (ev >> 1) & 0x1f;
	unsigned type = (ev >> 6) & 0xf;
	unsigned status = (ev >> 12) & 0xf;
	if (ep==EP_TX && type==1 && ud.configured && ud.tx_busy) {
        ud_sync(UD_TRB_TX,16);
        volatile uint32_t *trb=(volatile uint32_t *)(ud.dma+UD_TRB_TX);
        if ((trb[2]&0xffffff)==0 && !(trb[3]&TRB_HWO)) {
            ud.log_cursor=ud.pending_cursor;
            os_atomic_store(&ud_log_delivered,ud.log_cursor,release);
            uint64_t epoch = os_atomic_load(&ud_fb_session, acquire);
            if (epoch != 0 && ud_fb_pending_session == epoch) {
                os_atomic_store(&ud_fb_delivered, ud.log_cursor, release);
                (void)os_atomic_add(&ud_fb_lost, ud.dropped, acq_rel);
                os_atomic_store(&ud_fb_done_session, epoch, release);
            }
        }
        ud.tx_busy=false; return;
    }
	if (ep > 1) {
		return;
	}
	/*
	 * Control transfer state machine (Linux dwc3 ep0.c): SETUP completes ->
	 * request handled; the data stage (if any) is started right away; the
	 * status stage is started when the controller reports XferNotReady
	 * with status "control status"; its completion re-arms SETUP.
	 */
	if (type == 1) {                /* XferComplete */
		switch (ud.state) {
		case EP0_SETUP:
			ud_sync(UD_SETUP, 8);
			ud_setup();
			break;
		case EP0_DATA:
			ud.state = EP0_WAIT_STATUS3;
			break;
		case EP0_STATUS:
			ud_start_setup();
			break;
		default:
			break;
		}
		return;
	}
	if (type == 3 && status == 2) { /* XferNotReady, control status phase */
		if (ud.state == EP0_WAIT_STATUS2 || ud.state == EP0_WAIT_STATUS3) {
			uint32_t ctl = ud.state == EP0_WAIT_STATUS3 ? TRBCTL_STATUS3 : TRBCTL_STATUS2;
			ud.state = EP0_STATUS;
			ud_start(ep, UD_DATA, 0, ctl);
		}
	}
}

static void
ud_thread(void *arg __unused, wait_result_t wr __unused)
{
	while (!os_atomic_load(&ud.stopped,acquire)) {
		uint32_t count = dwc_rd(GEVNTCOUNT) & 0xffff;
		if (count>UD_EVT_SIZE || (count&3)) {
            printf("LITHIUM USB: invalid event byte count %u; halted\n",count); break;
        }
		if (count) {
			ud_sync(UD_EVT, UD_EVT_SIZE);
			for (uint32_t done = 0; done < count; done += 4) {
				uint32_t ev = *(volatile uint32_t *)(ud.dma + UD_EVT + ud.evt_pos);
				ud.evt_pos = (ud.evt_pos + 4) % UD_EVT_SIZE;
				if (ud.events++ < 20) {
					printf("LITHIUM USB: event 0x%08x\n", ev);
				}
				ud_event(ev);
			}
			dwc_wr(GEVNTCOUNT, count);
		}
		ud_log_kick();
		ud_sleep_ms(2);
	}
    dwc_wr(DEVTEN,0);
    dwc_wr(DCTL,dwc_rd(DCTL)&~DCTL_RUN_STOP);
    os_atomic_store(&ud.stopped,2,release);
    thread_terminate(current_thread());
}

static void
lithium_usb_gadget_start(vm_offset_t dwc)
{
	thread_t thread;

	ud.dwc = dwc; ud.packet_size=512; ud.old_gctl=dwc_rd(GCTL);
	if (kmem_alloc_contig(kernel_map, &ud.dma, UD_SIZE, PAGE_MASK, 0, 0,
	    KMA_DATA | KMA_ZERO | KMA_PERMANENT, VM_KERN_MEMORY_IOKIT) != KERN_SUCCESS) {
		printf("LITHIUM USB: no DMA memory\n");
		return;
	}
	ppnum_t pn = pmap_find_phys(kernel_pmap, (addr64_t)ud.dma);
	if (!pn) {
		return;
	}
	ud.dma_pa = ptoa_64(pn);

	/* Halt before reset; preserve global PHY setup. This board has no EFI USB
     * owner while XNU runs. The existing fastboot session ends at takeover. */
    dwc_wr(DCTL,dwc_rd(DCTL)&~DCTL_RUN_STOP);
    for (unsigned j=0;j<500 && !(dwc_rd(DSTS)&DSTS_DEVCTRLHLT);j++) delay(1000);
    if (!(dwc_rd(DSTS)&DSTS_DEVCTRLHLT)) {printf("LITHIUM USB: halt timed out\n");return;}
    /* Device controller soft reset (PHY and global setup stay as ABL left them). */
	dwc_wr(DCTL, DCTL_CSFTRST);
	int i;
	for (i = 0; i < 500 && (dwc_rd(DCTL) & DCTL_CSFTRST); i++) {
		delay(1000);
	}
	printf("LITHIUM USB: soft reset %s after %d ms, GCTL 0x%08x DCFG 0x%08x\n",
	    (dwc_rd(DCTL) & DCTL_CSFTRST) ? "STUCK" : "done", i, dwc_rd(GCTL), dwc_rd(DCFG));
	if (dwc_rd(DCTL) & DCTL_CSFTRST) {
		return;
	}

	dwc_wr(GEVNTADRLO, (uint32_t)(ud.dma_pa + UD_EVT));
	dwc_wr(GEVNTADRHI, (uint32_t)((ud.dma_pa + UD_EVT) >> 32));
	dwc_wr(GEVNTSIZ, (1u << 31) | UD_EVT_SIZE);     /* interrupt masked: polled */
	dwc_wr(GEVNTCOUNT, dwc_rd(GEVNTCOUNT) & 0xffff);
	ud.evt_pos = 0;

	/* High speed, address 0 */
	uint32_t dcfg = dwc_rd(DCFG) & ~(7u | (0x7fu << 3));
	dwc_wr(DCFG, dcfg | 0);
	/* Events: disconnect, USB reset, connect done */
	dwc_wr(DEVTEN, 0x7);

	/* USB2 EP0 uses 64-byte packets from the beginning. */
	if (ud_epcmd(0, DEPCMD_DEPSTARTCFG, 0, 0, 0) ||
	    ud_epcmd(0, DEPCMD_SETEPCONFIG, (0u << 1) | (64u << 3), (1u << 8) | (1u << 10) | (0u << 25), 0) ||
	    ud_epcmd(0, DEPCMD_SETTRANSFRESOURCE, 1, 0, 0) ||
	    ud_epcmd(1, DEPCMD_SETEPCONFIG, (0u << 1) | (64u << 3), (1u << 8) | (1u << 10) | (1u << 25), 0) ||
	    ud_epcmd(1, DEPCMD_SETTRANSFRESOURCE, 1, 0, 0)) {
		printf("LITHIUM USB: endpoint 0 setup failed\n");
		return;
	}
	dwc_wr(DALEPENA, 0x3);
	ud_start_setup();

	dwc_wr(DCTL, dwc_rd(DCTL) | DCTL_RUN_STOP);
	for (i = 0; i < 500 && (dwc_rd(DSTS) & DSTS_DEVCTRLHLT); i++) {
		delay(1000);
	}
	printf("LITHIUM USB: running (DSTS 0x%08x after %d ms), DMA at 0x%llx\n", dwc_rd(DSTS), i, ud.dma_pa);
	if (kernel_thread_start(ud_thread, NULL, &thread) == KERN_SUCCESS) {
		thread_deallocate(thread);
	} else {
        dwc_wr(DEVTEN,0); dwc_wr(DCTL,dwc_rd(DCTL)&~DCTL_RUN_STOP);
    }
}
/* No DMA memory is freed on stop: the halt may itself fail during panic.
 * Keeping it reserved prevents a late controller write into reused memory. */
void lithium_usb_quiesce(void);
void
lithium_usb_quiesce(void)
{
    if (!ud.dwc) return;
    os_atomic_store(&ud.stopped,1,release);
    dwc_wr(DEVTEN,0);
    dwc_wr(DCTL,dwc_rd(DCTL)&~DCTL_RUN_STOP);
    for (unsigned i=0;i<10000 && !(dwc_rd(DSTS)&DSTS_DEVCTRLHLT);i++) delay(1);
}

static void
lithium_usb_init_thread(void *arg __unused, wait_result_t wr __unused)
{
    lithium_usb_probe();
    thread_terminate(current_thread());
}
__startup_func static void
lithium_usb_init(void)
{
    thread_t thread;
    if (kernel_thread_start(lithium_usb_init_thread,NULL,&thread)==KERN_SUCCESS) thread_deallocate(thread);
}
STARTUP(EARLY_BOOT, STARTUP_RANK_LAST, lithium_usb_init);
#endif /* LITHIUM_ARMV8_0 */
