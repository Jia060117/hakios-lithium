#ifndef LITHIUM_R21_UFS_READONLY_H
#define LITHIUM_R21_UFS_READONLY_H

/* Checked before building descriptors or ringing the doorbell. Only
 * canonical, LUN0, device-to-host CDBs are admitted. */
static inline int
lithium_r21_ufs_admit(unsigned lun, const unsigned char *cdb, unsigned cdb_len,
    const void *buffer, unsigned len, const void *bounce, unsigned capacity)
{
    if (lun != 0 || !cdb || (cdb_len != 6 && cdb_len != 10) ||
        len > capacity || (len != 0 && (!bounce || buffer != bounce))) return 0;
    if (cdb[0] == 0x00 || cdb[0] == 0x25) {
        if (cdb[0] == 0x00 ? (cdb_len != 6 || len != 0) : (cdb_len != 10 || len != 8))
            return 0;
        for (unsigned i = 1; i < cdb_len; ++i)
            if (cdb[i] != 0) return 0;
        return 1;
    }
    if (cdb[0] != 0x28 || cdb_len != 10 || cdb[1] || cdb[6] || cdb[9] || !len)
        return 0;
    return (((unsigned)cdb[7] << 8) | cdb[8]) * 4096u == len;
}

#endif
