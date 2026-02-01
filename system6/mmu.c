// mmu.c - 68030/68040 MMU register access

#include <Gestalt.h>

#include "../src/dmg.h"
#include "../src/rom.h"
#include "../src/mbc.h"

#define ROOT_040_ENTRIES     128
#define POINTER_040_ENTRIES  128
#define PAGE_040_ENTRIES     64

/* 68040 UDT=2: resident descriptor pointing to next level */
#define UDT_RESIDENT    0x02
/* 68040 PDT=1: resident page descriptor */
#define PDT_RESIDENT    0x01

// 128 MB, maybe move to 0x3f000000? right below ROM
#define GB_VIRTUAL_BASE 0x08000000

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

void pflusha_040(void)
{
    asm volatile(
        ".word   0xf518"                /* pflusha */
        :
        :
        : "memory"
    );
}

static int cpu_type;

static int get_cpu_type(void)
{
    long response;
    if (Gestalt(gestaltProcessorType, &response) != noErr) {
        return 0;
    }

    return (int) response;
}

// storage with padding for manual alignment
static uint8_t pointer_table_040_storage[POINTER_040_ENTRIES * 4 + 512];
static uint8_t page_table_040_storage[PAGE_040_ENTRIES * 4 + 256];
// aligned pointers (set during init)
static uint32_t *pointer_table_040;
static uint32_t *page_table_040;
static uint32_t old_root_entry_040;

static void mmu_setup_translation_040(struct dmg *dmg)
{
    uint16_t tc;
    uint32_t srp;
    uint32_t *root_table;
    int root_index, pointer_index;
    short sr;
    int k;

    // manually align page tables since linker may not honor __attribute__((aligned))
    pointer_table_040 = (uint32_t *)(((uint32_t)pointer_table_040_storage + 511) & ~511);
    page_table_040 = (uint32_t *)(((uint32_t)page_table_040_storage + 255) & ~255);

    // zero the tables
    for (k = 0; k < POINTER_040_ENTRIES; k++)
        pointer_table_040[k] = 0;
    for (k = 0; k < PAGE_040_ENTRIES; k++)
        page_table_040[k] = 0;

    root_index = GB_VIRTUAL_BASE >> 25;              /* bits 31:25 */
    pointer_index = (GB_VIRTUAL_BASE >> 18) & 0x7f;  /* bits 24:18 */

    tc = read_tc_040();
    srp = read_srp_040();
    root_table = (uint32_t *)(srp & 0xfffffe00);
    old_root_entry_040 = root_table[root_index];

    // UDT=2, page table address in bits 31:8
    pointer_table_040[pointer_index] = ((uint32_t) page_table_040 & 0xffffff00) | UDT_RESIDENT;

    // PDT=1, physical address in bits 31:13, copyback cache (CM=01)
    // rom bank 0
    page_table_040[0] = ((uint32_t) dmg->rom->data & 0xffffe000) | 0x24 | PDT_RESIDENT;
    page_table_040[1] = ((uint32_t) (dmg->rom->data + 0x2000) & 0xffffe000) | 0x24 | PDT_RESIDENT;
    // rom bank switchable - start with bank 1
    page_table_040[2] = ((uint32_t) (dmg->rom->data + 0x4000) & 0xffffe000) | 0x24 | PDT_RESIDENT;
    page_table_040[3] = ((uint32_t) (dmg->rom->data + 0x6000) & 0xffffe000) | 0x24 | PDT_RESIDENT;
    // video ram
    page_table_040[4] = ((uint32_t) dmg->video_ram & 0xffffe000) | 0x20 | PDT_RESIDENT;
    // cart ram - default to not present
    page_table_040[5] = ((uint32_t) dmg->unused_area & 0xffffe000) | 0x20 | PDT_RESIDENT;
    // main ram
    page_table_040[6] = ((uint32_t) dmg->main_ram & 0xffffe000) | 0x20 | PDT_RESIDENT;
    // echo ram + I/O... I/O will branch before trying to read/write here though
    page_table_040[7] = ((uint32_t) dmg->main_ram & 0xffffe000) | 0x20 | PDT_RESIDENT;

    // disable interrupts
    asm volatile("move.w %%sr, %0" : "=d" (sr));
    asm volatile("ori.w #0x0700, %sr");

    // UDT=2, pointer table address in bits 31:9
    root_table[root_index] = ((uint32_t) pointer_table_040 & 0xfffffe00) | UDT_RESIDENT;
    pflusha_040();
    // enable interrupts
    asm volatile("move.w %0, %%sr" : : "d" (sr));
}

static void mmu_cleanup_040(void)
{
    uint32_t srp;
    uint32_t *root_table;
    short sr;

    srp = read_srp_040();
    root_table = (uint32_t *)(srp & 0xfffffe00);

    asm volatile("move.w %%sr, %0" : "=d" (sr));
    asm volatile("ori.w #0x0700, %sr");

    root_table[GB_VIRTUAL_BASE >> 25] = old_root_entry_040;
    pflusha_040();

    asm volatile("move.w %0, %%sr" : : "d" (sr));
}

static void mmu_setup_translation_030(struct dmg *dmg)
{

}

static void mmu_cleanup_030(void)
{

}

void mmu_setup_translation(struct dmg *dmg)
{
    cpu_type = get_cpu_type();
    if (cpu_type == gestalt68040) {
        mmu_setup_translation_040(dmg);
    } else if (cpu_type == gestalt68030) {
        mmu_setup_translation_030(dmg);
    }
}

void mmu_cleanup(void)
{
    if (cpu_type == gestalt68040) {
        mmu_cleanup_040();
    } else if (cpu_type == gestalt68030) {
        mmu_cleanup_030();
    }
}

void mmu_update_rom_bank(struct dmg *dmg)
{
    if (cpu_type == gestalt68040) {
        struct mbc *mbc = dmg->rom->mbc;
        uint8_t *bank_data = dmg->rom->data + mbc->rom_bank * 0x4000;
        short sr;

        asm volatile("move.w %%sr, %0" : "=d" (sr));
        asm volatile("ori.w #0x0700, %sr");

        page_table_040[2] = ((uint32_t) bank_data & 0xffffe000) | 0x24 | PDT_RESIDENT;
        page_table_040[3] = ((uint32_t) (bank_data + 0x2000) & 0xffffe000) | 0x24 | PDT_RESIDENT;
        pflusha_040();

        asm volatile("move.w %0, %%sr" : : "d" (sr));
    }
}

void mmu_update_ram_bank(struct dmg *dmg)
{
    if (cpu_type == gestalt68040) {
        struct mbc *mbc = dmg->rom->mbc;
        short sr;

        asm volatile("move.w %%sr, %0" : "=d" (sr));
        asm volatile("ori.w #0x0700, %sr");

        if (mbc->ram_enabled) {
            uint8_t *ram_data = mbc->ram + mbc->ram_bank * 0x2000;
            page_table_040[5] = ((uint32_t) ram_data & 0xffffe000) | 0x20 | PDT_RESIDENT;
        } else {
            page_table_040[5] = ((uint32_t) dmg->unused_area & 0xffffe000) | 0x20 | PDT_RESIDENT;
        }
        pflusha_040();

        asm volatile("move.w %0, %%sr" : : "d" (sr));
    }
}