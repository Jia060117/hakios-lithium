#include "r21-ufs-readonly.h"

#include <assert.h>
#include <stdio.h>

int main(void)
{
    unsigned char bounce[65536], other[8];
    unsigned char tur[6] = {0}, capacity[10] = {0x25};
    unsigned char read[10] = {0x28, 0, 0x12, 0x34, 0, 0x89, 0, 0, 1, 0};
    assert(lithium_r21_ufs_admit(0, tur, sizeof(tur), NULL, 0, bounce, sizeof(bounce)));
    assert(lithium_r21_ufs_admit(0, capacity, sizeof(capacity), bounce, 8, bounce, sizeof(bounce)));
    assert(lithium_r21_ufs_admit(0, read, sizeof(read), bounce, 4096, bounce, sizeof(bounce)));
    read[8] = 16;
    assert(lithium_r21_ufs_admit(0, read, sizeof(read), bounce, 65536, bounce, sizeof(bounce)));

    /* Try all opcodes under every accepted transfer shape. Includes WRITE,
     * FORMAT, UNMAP, SECURITY PROTOCOL OUT and both SYNCHRONIZE CACHE forms. */
    for (unsigned op = 0; op < 256; ++op) {
        unsigned char cdb[10] = {(unsigned char)op};
        assert(lithium_r21_ufs_admit(0, cdb, 6, NULL, 0, bounce, sizeof(bounce)) == (op == 0));
        assert(lithium_r21_ufs_admit(0, cdb, 10, bounce, 8, bounce, sizeof(bounce)) == (op == 0x25));
        cdb[8] = 1;
        assert(lithium_r21_ufs_admit(0, cdb, 10, bounce, 4096, bounce, sizeof(bounce)) == (op == 0x28));
    }
    assert(!lithium_r21_ufs_admit(1, read, 10, bounce, 65536, bounce, sizeof(bounce)));
    assert(!lithium_r21_ufs_admit(0, NULL, 0, bounce, 0, bounce, sizeof(bounce)));
    assert(!lithium_r21_ufs_admit(0, read, 6, bounce, 65536, bounce, sizeof(bounce)));
    assert(!lithium_r21_ufs_admit(0, read, 10, bounce, 65535, bounce, sizeof(bounce)));
    assert(!lithium_r21_ufs_admit(0, read, 10, bounce, 65536, bounce, sizeof(bounce) - 1));
    assert(!lithium_r21_ufs_admit(0, read, 10, NULL, 65536, NULL, sizeof(bounce)));
    assert(!lithium_r21_ufs_admit(0, capacity, 10, other, 8, bounce, sizeof(bounce)));
    read[1] = 0x08;
    assert(!lithium_r21_ufs_admit(0, read, 10, bounce, 65536, bounce, sizeof(bounce)));
    tur[5] = 1;
    capacity[8] = 1;
    assert(!lithium_r21_ufs_admit(0, tur, 6, NULL, 0, bounce, sizeof(bounce)));
    assert(!lithium_r21_ufs_admit(0, capacity, 10, bounce, 8, bounce, sizeof(bounce)));
    puts("PASS read-only UFS: 768 opcode/shape cases, valid reads and malformed requests");
    return 0;
}
