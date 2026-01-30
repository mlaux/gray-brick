// mmu.h - 68030/68040 MMU register access

#ifndef _MMU_H
#define _MMU_H

#include <stdint.h>

// 68030 TC register fields
#define TC030_ENABLE    (1UL << 31)
#define TC030_SRE       (1UL << 25)
#define TC030_FCL       (1UL << 24)
#define TC030_PS_SHIFT  20
#define TC030_PS_MASK   0x0f
#define TC030_IS_SHIFT  16
#define TC030_IS_MASK   0x0f
#define TC030_TIA_SHIFT 12
#define TC030_TIA_MASK  0x0f
#define TC030_TIB_SHIFT 8
#define TC030_TIB_MASK  0x0f
#define TC030_TIC_SHIFT 4
#define TC030_TIC_MASK  0x0f
#define TC030_TID_SHIFT 0
#define TC030_TID_MASK  0x0f

// 68040 TC register fields
#define TC040_ENABLE    (1U << 15)
#define TC040_PAGE_8K   (1U << 14)

// Descriptor types (030 and 040)
#define DT_INVALID      0
#define DT_PAGE         1
#define DT_VALID_4      2
#define DT_VALID_8      3

// 68030 MMU register access
uint32_t read_tc_030(void);
void read_crp_030(uint32_t *limit_out, uint32_t *base_out);
void read_srp_030(uint32_t *limit_out, uint32_t *base_out);
void write_tc_030(uint32_t tc);
void write_crp_030(uint32_t limit, uint32_t base);
void pflusha_030(void);

// 68040 MMU register access
uint16_t read_tc_040(void);
uint32_t read_srp_040(void);
uint32_t read_urp_040(void);

#endif /* _MMU_H */
