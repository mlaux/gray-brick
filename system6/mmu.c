// mmu.c - 68030/68040 MMU register access

#include "mmu.h"

// 68030 MMU register access via PMOVE
// pmove %reg, (addr) encoding: F-line with extension word
//   pmove tc,(a0)  -> 0xf010 0x4200
//   pmove crp,(a0) -> 0xf010 0x4e00
//   pmove srp,(a0) -> 0xf010 0x4a00
uint32_t read_tc_030(void)
{
    uint32_t tc;
    uint32_t *p = &tc;
    asm volatile(
        "move.l  %1, %%a0       \n\t"
        ".word   0xf010         \n\t"   /* pmove tc,(a0) */
        ".word   0x4200"
        : "=m" (tc)
        : "g" (p)
        : "a0", "memory"
    );
    return tc;
}

void read_crp_030(uint32_t *limit_out, uint32_t *base_out)
{
    uint32_t buf[2];
    uint32_t *p = buf;
    asm volatile(
        "move.l  %1, %%a0       \n\t"
        ".word   0xf010         \n\t"   /* pmove crp,(a0) */
        ".word   0x4e00"
        : "=m" (buf)
        : "g" (p)
        : "a0", "memory"
    );
    *limit_out = buf[0];
    *base_out = buf[1];
}

void read_srp_030(uint32_t *limit_out, uint32_t *base_out)
{
    uint32_t buf[2];
    uint32_t *p = buf;
    asm volatile(
        "move.l  %1, %%a0       \n\t"
        ".word   0xf010         \n\t"   /* pmove srp,(a0) */
        ".word   0x4a00"
        : "=m" (buf)
        : "g" (p)
        : "a0", "memory"
    );
    *limit_out = buf[0];
    *base_out = buf[1];
}

void write_tc_030(uint32_t tc)
{
    uint32_t *p = &tc;
    asm volatile(
        "move.l  %0, %%a0       \n\t"
        ".word   0xf010         \n\t"   /* pmove (a0),tc */
        ".word   0x4000"
        :
        : "g" (p)
        : "a0", "memory"
    );
}

void write_crp_030(uint32_t limit, uint32_t base)
{
    uint32_t buf[2];
    uint32_t *p = buf;
    buf[0] = limit;
    buf[1] = base;
    asm volatile(
        "move.l  %0, %%a0       \n\t"
        ".word   0xf010         \n\t"   /* pmove (a0),crp */
        ".word   0x4c00"
        :
        : "g" (p)
        : "a0", "memory"
    );
}

void pflusha_030(void)
{
    asm volatile(
        ".word   0xf000         \n\t"   /* pflusha */
        ".word   0x2400"
        :
        :
        : "memory"
    );
}

// 68040 MMU register access via MOVEC
//   movec tc,d0  -> 0x4e7a 0x0003
//   movec srp,d0 -> 0x4e7a 0x0807
//   movec urp,d0 -> 0x4e7a 0x0806
uint16_t read_tc_040(void)
{
    uint32_t tc;
    asm volatile(
        ".word   0x4e7a         \n\t"   /* movec tc,d0 */
        ".word   0x0003         \n\t"
        "move.l  %%d0, %0"
        : "=m" (tc)
        :
        : "d0"
    );
    return (uint16_t)tc;
}

uint32_t read_srp_040(void)
{
    uint32_t srp;
    asm volatile(
        ".word   0x4e7a         \n\t"   /* movec srp,d0 */
        ".word   0x0807         \n\t"
        "move.l  %%d0, %0"
        : "=m" (srp)
        :
        : "d0"
    );
    return srp;
}

uint32_t read_urp_040(void)
{
    uint32_t urp;
    asm volatile(
        ".word   0x4e7a         \n\t"   /* movec urp,d0 */
        ".word   0x0806         \n\t"
        "move.l  %%d0, %0"
        : "=m" (urp)
        :
        : "d0"
    );
    return urp;
}
