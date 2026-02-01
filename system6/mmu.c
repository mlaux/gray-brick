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

// Flush a single page from the ATC (68030)
// pflush #0,#0,(a0) - flush all FC entries for address in a0
static void pflush_030(uint32_t addr)
{
    asm volatile(
        "movea.l %0, %%a0\n\t"
        ".word   0xf010\n\t"            /* pflush #0,#0,(a0) */
        ".word   0x3800"
        :
        : "g" (addr)
        : "a0", "memory"
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

// Flush a single page from the ATC (68040)
static void pflush_040(uint32_t addr)
{
    asm volatile(
        "movea.l %0, %%a0\n\t"
        ".word   0xf508"                /* pflush (a0) */
        :
        : "g" (addr)
        : "a0", "memory"
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

// 68040 storage with padding for manual alignment
static uint8_t pointer_table_040_storage[POINTER_040_ENTRIES * 4 + 512];
static uint8_t page_table_040_storage[PAGE_040_ENTRIES * 4 + 256];
// aligned pointers (set during init)
static uint32_t *pointer_table_040;
static uint32_t *page_table_040;
static uint32_t old_root_entry_040;

// 68030 table sizes (for TC with TIA=7, TIB=7, TIC=5, PS=13)
#define LEVEL_A_030_ENTRIES  128
#define LEVEL_B_030_ENTRIES  128
#define LEVEL_C_030_ENTRIES  32

// new TC for 8KB pages: E=1, PS=13, IS=0, TIA=7, TIB=7, TIC=5, TID=0
#define NEW_TC_030  0x80d07750

// 68030 storage with padding for manual alignment (16-byte for table4)
static uint8_t level_a_030_storage[LEVEL_A_030_ENTRIES * 4 + 16];
static uint8_t level_b_030_storage[LEVEL_B_030_ENTRIES * 4 + 16];
static uint8_t level_c_030_storage[LEVEL_C_030_ENTRIES * 4 + 16];
// aligned pointers (set during init)
static uint32_t *level_a_030;
static uint32_t *level_b_030;
static uint32_t *level_c_030;
// for injection mode (8KB page systems)
static uint32_t old_level_a_entry_030;
static uint32_t *existing_level_a_030;
// for full rebuild mode (32KB page systems)
static uint32_t old_tc_030;
static uint32_t old_crp_limit_030;
static uint32_t old_crp_base_030;
static int use_full_tables_030;

// Check if current 68030 MMU setup allows simple injection (8KB pages).
// Returns 1 for 8KB pages with standard structure, 0 otherwise.
static int is_injectable_mmu_030(uint32_t tc)
{
    int ps, tia, tib, tic;

    if (!(tc & TC030_ENABLE))
        return 0;

    ps = (tc >> TC030_PS_SHIFT) & TC030_PS_MASK;
    tia = (tc >> TC030_TIA_SHIFT) & TC030_TIA_MASK;
    tib = (tc >> TC030_TIB_SHIFT) & TC030_TIB_MASK;
    tic = (tc >> TC030_TIC_SHIFT) & TC030_TIC_MASK;

    if (ps != 13)
        return 0;
    if (tia != 7 || tib != 7 || tic != 5)
        return 0;

    return 1;
}

// Check if we can rebuild the page tables (32KB or other page sizes).
// We support rebuilding for common Mac configurations.
static int is_rebuildable_mmu_030(uint32_t tc)
{
    if (!(tc & TC030_ENABLE))
        return 0;

    // we can rebuild any enabled MMU - we'll create our own 8KB page structure
    return 1;
}

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

// Build Level C page table entries for Game Boy memory (shared by both modes)
static void build_level_c_030(struct dmg *dmg)
{
    // DT_PAGE=1, WP (bit 2) set for ROM
    // rom bank 0
    level_c_030[0] = ((uint32_t)dmg->rom->data & 0xffffe000) | 0x04 | DT_PAGE;
    level_c_030[1] = ((uint32_t)(dmg->rom->data + 0x2000) & 0xffffe000) | 0x04 | DT_PAGE;
    // rom bank switchable - start with bank 1
    level_c_030[2] = ((uint32_t)(dmg->rom->data + 0x4000) & 0xffffe000) | 0x04 | DT_PAGE;
    level_c_030[3] = ((uint32_t)(dmg->rom->data + 0x6000) & 0xffffe000) | 0x04 | DT_PAGE;
    // video ram
    level_c_030[4] = ((uint32_t)dmg->video_ram & 0xffffe000) | DT_PAGE;
    // cart ram - default to not present
    level_c_030[5] = ((uint32_t)dmg->unused_area & 0xffffe000) | DT_PAGE;
    // main ram
    level_c_030[6] = ((uint32_t)dmg->main_ram & 0xffffe000) | DT_PAGE;
    // echo ram + I/O
    level_c_030[7] = ((uint32_t)dmg->main_ram & 0xffffe000) | DT_PAGE;
}

// Build full page tables with 8KB pages (for 32KB page systems)
static void build_full_tables_030(struct dmg *dmg)
{
    int k;
    uint32_t phys;
    int level_a_index, level_b_index;

    level_a_index = GB_VIRTUAL_BASE >> 25;              /* bits 31:25 */
    level_b_index = (GB_VIRTUAL_BASE >> 18) & 0x7f;     /* bits 24:18 */

    /*
     * Level A: identity map with early termination (DT_PAGE).
     * Each entry covers 32MB (bits 24:0).
     * Memory map (typical Mac):
     *   0-3:   RAM (up to 128MB) - cacheable
     *   4:     Our GB region (0x08000000) -> points to Level B
     *   5-31:  More RAM or invalid
     *   32-39: ROM area (0x40000000-0x4fffffff) - cacheable
     *   40+:   I/O and slot space (0x50000000+) - cache inhibit
     */
    for (k = 0; k < LEVEL_A_030_ENTRIES; k++) {
        phys = k << 25;  /* physical = virtual (identity map) */

        if (k == level_a_index) {
            /* our GB region: point to Level B table */
            level_a_030[k] = ((uint32_t)level_b_030 & 0xfffffff0) | DT_VALID_4;
        } else if (k < 40) {
            /* RAM and ROM area: cacheable, early termination */
            level_a_030[k] = (phys & 0xfe000000) | DT_PAGE;
        } else {
            /* I/O and slot space (0x50000000+): cache inhibit */
            level_a_030[k] = (phys & 0xfe000000) | 0x40 | DT_PAGE;
        }
    }

    /* Level B: only our entry points to Level C, rest invalid */
    for (k = 0; k < LEVEL_B_030_ENTRIES; k++) {
        if (k == level_b_index) {
            level_b_030[k] = ((uint32_t)level_c_030 & 0xfffffff0) | DT_VALID_4;
        } else {
            level_b_030[k] = DT_INVALID;
        }
    }

    /* Level C: Game Boy memory regions */
    build_level_c_030(dmg);
}

static void mmu_setup_translation_030(struct dmg *dmg)
{
    uint32_t tc;
    uint32_t crp_limit, crp_base;
    int level_a_index, level_b_index;
    short sr;
    int k;

    tc = read_tc_030();
    read_crp_030(&crp_limit, &crp_base);

    // manually align page tables (16-byte for table4 descriptors)
    level_a_030 = (uint32_t *)(((uint32_t)level_a_030_storage + 15) & ~15);
    level_b_030 = (uint32_t *)(((uint32_t)level_b_030_storage + 15) & ~15);
    level_c_030 = (uint32_t *)(((uint32_t)level_c_030_storage + 15) & ~15);

    // zero the tables
    for (k = 0; k < LEVEL_A_030_ENTRIES; k++)
        level_a_030[k] = 0;
    for (k = 0; k < LEVEL_B_030_ENTRIES; k++)
        level_b_030[k] = 0;
    for (k = 0; k < LEVEL_C_030_ENTRIES; k++)
        level_c_030[k] = 0;

    level_a_index = GB_VIRTUAL_BASE >> 25;              /* bits 31:25 */
    level_b_index = (GB_VIRTUAL_BASE >> 18) & 0x7f;     /* bits 24:18 */

    if (is_injectable_mmu_030(tc)) {
        // simple injection into existing 8KB page tables
        use_full_tables_030 = 0;
        existing_level_a_030 = (uint32_t *)(crp_base & 0xfffffff0);
        old_level_a_entry_030 = existing_level_a_030[level_a_index];

        // Level B: entry at level_b_index points to Level C
        level_b_030[level_b_index] = ((uint32_t)level_c_030 & 0xfffffff0) | DT_VALID_4;

        // Level C: Game Boy memory regions
        build_level_c_030(dmg);

        // disable interrupts
        asm volatile("move.w %%sr, %0" : "=d" (sr));
        asm volatile("ori.w #0x0700, %sr");

        // inject into Level A
        existing_level_a_030[level_a_index] = ((uint32_t)level_b_030 & 0xfffffff0) | DT_VALID_4;
        pflusha_030();

        asm volatile("move.w %0, %%sr" : : "d" (sr));
    } else if (is_rebuildable_mmu_030(tc)) {
        // full rebuild with 8KB pages (for 32KB page systems, etc)
        use_full_tables_030 = 1;
        existing_level_a_030 = NULL;
        old_tc_030 = tc;
        old_crp_limit_030 = crp_limit;
        old_crp_base_030 = crp_base;

        // build complete page tables with identity mapping
        build_full_tables_030(dmg);

        // disable interrupts
        asm volatile("move.w %%sr, %0" : "=d" (sr));
        asm volatile("ori.w #0x0700, %sr");

        // switch to new tables with 8KB pages
        pflusha_030();
        write_crp_030(0x7fff0002, (uint32_t)level_a_030);
        write_tc_030(NEW_TC_030);
        pflusha_030();

        asm volatile("move.w %0, %%sr" : : "d" (sr));
    }
    // else: MMU disabled or unsupported, do nothing
}

static void mmu_cleanup_030(void)
{
    short sr;

    if (use_full_tables_030) {
        // restore original TC and CRP
        asm volatile("move.w %%sr, %0" : "=d" (sr));
        asm volatile("ori.w #0x0700, %sr");

        pflusha_030();
        // disable MMU briefly while switching
        write_tc_030(NEW_TC_030 & ~TC030_ENABLE);
        write_crp_030(old_crp_limit_030, old_crp_base_030);
        write_tc_030(old_tc_030);
        pflusha_030();

        asm volatile("move.w %0, %%sr" : : "d" (sr));

        use_full_tables_030 = 0;
    } else if (existing_level_a_030) {
        // restore original Level A entry
        asm volatile("move.w %%sr, %0" : "=d" (sr));
        asm volatile("ori.w #0x0700, %sr");

        existing_level_a_030[GB_VIRTUAL_BASE >> 25] = old_level_a_entry_030;
        pflusha_030();

        asm volatile("move.w %0, %%sr" : : "d" (sr));

        existing_level_a_030 = NULL;
    }
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
    struct mbc *mbc = dmg->rom->mbc;
    uint8_t *bank_data = dmg->rom->data + mbc->rom_bank * 0x4000;
    short sr;

    if (cpu_type == gestalt68040) {
        asm volatile("move.w %%sr, %0" : "=d" (sr));
        asm volatile("ori.w #0x0700, %sr");

        page_table_040[2] = ((uint32_t)bank_data & 0xffffe000) | 0x24 | PDT_RESIDENT;
        page_table_040[3] = ((uint32_t)(bank_data + 0x2000) & 0xffffe000) | 0x24 | PDT_RESIDENT;
        pflush_040(GB_VIRTUAL_BASE + 0x4000);
        pflush_040(GB_VIRTUAL_BASE + 0x6000);

        asm volatile("move.w %0, %%sr" : : "d" (sr));
    } else if (cpu_type == gestalt68030 && level_c_030) {
        asm volatile("move.w %%sr, %0" : "=d" (sr));
        asm volatile("ori.w #0x0700, %sr");

        level_c_030[2] = ((uint32_t)bank_data & 0xffffe000) | 0x04 | DT_PAGE;
        level_c_030[3] = ((uint32_t)(bank_data + 0x2000) & 0xffffe000) | 0x04 | DT_PAGE;
        pflush_030(GB_VIRTUAL_BASE + 0x4000);
        pflush_030(GB_VIRTUAL_BASE + 0x6000);

        asm volatile("move.w %0, %%sr" : : "d" (sr));
    }
}

void mmu_update_ram_bank(struct dmg *dmg)
{
    struct mbc *mbc = dmg->rom->mbc;
    short sr;

    if (cpu_type == gestalt68040) {
        asm volatile("move.w %%sr, %0" : "=d" (sr));
        asm volatile("ori.w #0x0700, %sr");

        if (mbc->ram_enabled) {
            uint8_t *ram_data = mbc->ram + mbc->ram_bank * 0x2000;
            page_table_040[5] = ((uint32_t)ram_data & 0xffffe000) | 0x20 | PDT_RESIDENT;
        } else {
            page_table_040[5] = ((uint32_t)dmg->unused_area & 0xffffe000) | 0x20 | PDT_RESIDENT;
        }
        pflush_040(GB_VIRTUAL_BASE + 0xa000);

        asm volatile("move.w %0, %%sr" : : "d" (sr));
    } else if (cpu_type == gestalt68030 && level_c_030) {
        asm volatile("move.w %%sr, %0" : "=d" (sr));
        asm volatile("ori.w #0x0700, %sr");

        if (mbc->ram_enabled) {
            uint8_t *ram_data = mbc->ram + mbc->ram_bank * 0x2000;
            level_c_030[5] = ((uint32_t)ram_data & 0xffffe000) | DT_PAGE;
        } else {
            level_c_030[5] = ((uint32_t)dmg->unused_area & 0xffffe000) | DT_PAGE;
        }
        pflush_030(GB_VIRTUAL_BASE + 0xa000);

        asm volatile("move.w %0, %%sr" : : "d" (sr));
    }
}