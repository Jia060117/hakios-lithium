/* Generated from pinned ios-sailfish shim/ufs.c. See storage-provenance.json. */
#define UFS_BASE	0x00624000ULL	/* ufshc@1d84000, lithium stock/live FDT */

#define REG_CAP		0x00
#define REG_VER		0x08
#define REG_IS		0x20
#define REG_HCS		0x30
#define REG_HCE		0x34
#define REG_UTRLBA	0x50
#define REG_UTRLBAU	0x54
#define REG_UTRLDBR	0x58
#define REG_UTRLCLR	0x5c
#define REG_UTRLRSR	0x60

#define HCS_DP		(1u << 0)
#define HCS_UTRLRDY	(1u << 1)
#define HCS_UCRDY	(1u << 3)

#define BLOCK		4096u

static inline uint32_t ufs_rd(uint32_t off) { return *(volatile uint32_t *)(UFS_BASE + off); }
static inline void ufs_wr(uint32_t off, uint32_t v) { __asm__ volatile("dsb sy" ::: "memory");
	*(volatile uint32_t *)(UFS_BASE + off) = v; __asm__ volatile("dsb sy" ::: "memory"); }

/* UTP transfer request descriptor (32 bytes). */
struct utrd {
	uint32_t dw0;		/* [31:28] CT, [26:25] DD, [24] I */
	uint32_t dw1;
	uint32_t dw2;		/* [7:0] OCS */
	uint32_t dw3;
	uint32_t ucdba;		/* command descriptor base, 128-byte aligned */
	uint32_t ucdbau;
	uint32_t resp;		/* [31:16] response UPIU offset, [15:0] length (DWORDs) */
	uint32_t prdt;		/* [31:16] PRDT offset, [15:0] entries (DWORDs/entries) */
};

/* UTP command descriptor: command UPIU, response UPIU, PRDT. */
struct ucd {
	uint8_t cmd[512];
	uint8_t resp[512];
	struct {
		uint32_t addr_lo, addr_hi, rsvd, dbc;	/* dbc = byte count - 1 */
	} prdt[1];
	uint8_t cache_page_pad[4096 - 1040];
};

static struct utrd utrl[32] __attribute__((aligned(4096)));
static struct ucd ucd __attribute__((aligned(4096)));
static uint8_t data[16 * BLOCK] __attribute__((aligned(4096)));

static uint64_t ticks(void)
{
	uint64_t t;
	__asm__ volatile("isb; mrs %0, cntvct_el0" : "=r"(t));
	return t;
}

static void zero(void *p, unsigned n)
{
	volatile uint8_t *b = p;
	for (unsigned i = 0; i < n; i++)
		b[i] = 0;
}

static uint32_t be32(const uint8_t *p)
{
	return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

/*
 * Issue one bounded SCSI command in slot 0. WRITE(10) reaches Data only;
 * SYNCHRONIZE CACHE(10) completes the upstream HakiOS filesystem flush.
 * Returns 0 on success, otherwise a code describing the failure stage.
 */
static uint32_t scsi(uint8_t lun, const uint8_t *cdb, unsigned cdb_len, void *buf, uint32_t len,
    uint32_t *ocs_out, uint32_t *status_out)
{
	static uint8_t tag;
	BOOLEAN Write = cdb && cdb[0] == 0x2a;
	BOOLEAN Flush = cdb && cdb[0] == 0x35;
	/* LUN0 only; no generic command, firmware write or partition-table write. */
	if (!mActive || mDead || lun != 0 || !cdb || !ocs_out || !status_out ||
	    (cdb[0] != 0 && cdb[0] != 0x25 && cdb[0] != 0x28 && !Write && !Flush) ||
	    cdb_len > 16 || len > sizeof(data) || (len && buf != data) ||
	    (cdb[0] == 0 && (cdb_len != 6 || len != 0)) ||
	    (cdb[0] == 0x25 && (cdb_len != 10 || len != 8)) ||
	    ((cdb[0] == 0x28 || Write) && (cdb_len != 10 || len == 0 ||
	      (uint32_t)(cdb[7] << 8 | cdb[8]) * BLOCK != len)) ||
	    (Flush && (cdb_len != 10 || len != 0 || !mWriteEnabled)) ||
	    (Write && (!mWriteEnabled || !lithium_write_range(be32(cdb + 2), len / BLOCK)))) return 5;
	zero(&utrl[0], sizeof(utrl[0]));
	zero(&ucd, sizeof(ucd));

	/* Command UPIU: header (12 bytes), expected length, CDB. */
	ucd.cmd[0] = 0x01;			/* transaction type: COMMAND */
	ucd.cmd[1] = len ? (Write ? 0x20 : 0x40) : 0x00;
	ucd.cmd[2] = lun;
	ucd.cmd[3] = ++tag;
	ucd.cmd[12] = len >> 24;
	ucd.cmd[13] = len >> 16;
	ucd.cmd[14] = len >> 8;
	ucd.cmd[15] = len;
	for (unsigned i = 0; i < cdb_len; i++)
		ucd.cmd[16 + i] = cdb[i];

	uintptr_t ucd_pa = (uintptr_t)&ucd;
	utrl[0].dw0 = (1u << 28) | ((len ? (Write ? 1u : 2u) : 0u) << 25) | (1u << 24);
	utrl[0].dw2 = 0x0f;			/* OCS: invalid until the host updates it */
	utrl[0].ucdba = (uint32_t)ucd_pa;
	utrl[0].ucdbau = (uint32_t)(ucd_pa >> 32);
	utrl[0].resp = (512u / 4) << 16 | (512u / 4);
	if (len) {
		uintptr_t buf_pa = (uintptr_t)buf;
		ucd.prdt[0].addr_lo = (uint32_t)buf_pa;
		ucd.prdt[0].addr_hi = (uint32_t)(buf_pa >> 32);
		ucd.prdt[0].dbc = len - 1;
		utrl[0].prdt = (1024u / 4) << 16 | 1;
	}
	__asm__ volatile("dsb sy" ::: "memory");

	WriteBackInvalidateDataCacheRange(utrl, sizeof(utrl));
	WriteBackInvalidateDataCacheRange(&ucd, sizeof(ucd));
	if (len) WriteBackInvalidateDataCacheRange(data, sizeof(data));
	/* Never ring a doorbell against LK/another kernel's descriptor list. */
	if (ufs_rd(REG_UTRLBA) != (uint32_t)(uintptr_t)utrl ||
	    ufs_rd(REG_UTRLBAU) != (uint32_t)((uint64_t)(uintptr_t)utrl >> 32) ||
	    !(ufs_rd(REG_UTRLRSR) & 1) || ufs_rd(REG_UTRLDBR)) { mDead = TRUE; return 5; }
	ufs_wr(REG_UTRLDBR, 1);
	uint64_t start = ticks();
	while (ufs_rd(REG_UTRLDBR) & 1) {
		if (ticks() - start > ({ uint64_t hz; __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(hz)); hz; })) {	/* 1 s */
			ufs_wr(REG_UTRLCLR, ~1u);	/* clear slot 0 (write 0 to the bit) */
			mDead = TRUE; /* retain DMA buffers forever; no reuse after timeout */
			return 1;			/* timeout */
		}
	}
	__asm__ volatile("dsb sy" ::: "memory");
	InvalidateDataCacheRange(utrl, sizeof(utrl));
	InvalidateDataCacheRange(&ucd, sizeof(ucd));
	if (len) InvalidateDataCacheRange(data, sizeof(data));
	*ocs_out = utrl[0].dw2 & 0xff;
	*status_out = ucd.resp[7];		/* SCSI status in the response UPIU */
	if (*ocs_out)
		return 2;
	if (ucd.resp[0] != 0x21 || ucd.resp[3] != tag || ucd.resp[6] != 0 ||
	    (ucd.resp[1] & 0x60))		/* correct tag, response, no under/overrun */
		return 3;
	return *status_out ? 4 : 0;
}

static uint32_t read_blocks(uint32_t lba, uint32_t count, void *buf)
{
	uint8_t cdb[10] = { 0x28, 0, lba >> 24, lba >> 16, lba >> 8, lba, 0, count >> 8, count, 0 };
	uint32_t ocs, st;
	return scsi(0, cdb, sizeof(cdb), buf, count * BLOCK, &ocs, &st);
}

