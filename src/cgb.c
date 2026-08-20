#include <string.h>

#include "cgb.h"
#include "dmg.h"
#include "lcd.h"
#include "types.h"

// Perform GPDMA: immediate transfer that halts CPU
// Returns number of M-cycles taken
static int cgb_perform_gpdma(struct cgb_state *cgb, struct dmg *dmg, u8 length_value)
{
    int blocks = (length_value & 0x7f) + 1;
    int bytes = blocks * 16;
    int i;

    for (i = 0; i < bytes; i++) {
        // Read from source
        u8 data = dmg_read(dmg, cgb->hdma_source);
        cgb->hdma_source++;

        // Write to destination (always in VRAM $8000-$9FFF)
        u16 vram_addr = 0x8000 | (cgb->hdma_dest & 0x1fff);
        u8 *vram = &dmg->vram[cgb->vram_bank * 0x2000];
        vram[vram_addr & 0x1fff] = data;
        cgb->hdma_dest++;
    }

    // Transfer complete
    cgb->hdma_active = 0;

    // Return cycles taken: 8 M-cycles per block in both speed modes
    return blocks * 8;
}

void cgb_init(struct cgb_state *cgb, int cgb_flag)
{
    memset(cgb, 0, sizeof(struct cgb_state));

    cgb->mode = (cgb_flag & 0x80) ? 1 : 0;
    cgb->double_speed = 0;
    cgb->speed_switch_armed = 0;
    cgb->vram_bank = 0;
    cgb->wram_bank = 1;  // SVBK defaults to bank 1
    cgb->hdma_last_ly = 0xff;  // No HDMA triggered yet
}

int cgb_read_reg(struct cgb_state *cgb, struct dmg *dmg, u16 address, u8 *out)
{
    struct lcd *lcd = dmg->lcd;

    if (!cgb->mode) {
        return 0;
    }

    switch (address) {
    case REG_KEY1:
        // KEY1 - Speed switch register
        // Bit 7 = current speed (1 = double), bit 0 = switch armed
        *out = (cgb->double_speed << 7) | cgb->speed_switch_armed | 0x7e;
        return 1;

    case REG_VBK:
        // VBK - VRAM bank (only bit 0 matters)
        *out = cgb->vram_bank | 0xfe;
        return 1;

    case REG_BCPS:
        *out = lcd->bcps;
        return 1;

    case REG_BCPD:
        *out = lcd->bg_palette_ram[lcd->bcps & 0x3f];
        return 1;

    case REG_OCPS:
        *out = lcd->ocps;
        return 1;

    case REG_OCPD:
        *out = lcd->obj_palette_ram[lcd->ocps & 0x3f];
        return 1;

    case REG_SVBK:
        *out = cgb->wram_bank | 0xf8;
        return 1;

    case REG_HDMA5:
        // Bit 7 = 0 if HDMA active, 1 if not active
        // Bits 6-0 = remaining blocks - 1 (or 0x7F if not active)
        // catch up first so mid-transfer polls see the true count
        hdma_flush_now(dmg);
        if (cgb->hdma_active) {
            *out = cgb->hdma_remaining;  // bit 7 = 0 (active)
        } else {
            *out = 0xff;
        }
        return 1;
    }

    return 0;
}

int cgb_write_reg(struct cgb_state *cgb, struct dmg *dmg, u16 address, u8 data)
{
    if (!cgb->mode) {
        return 0;
    }

    switch (address) {
    case REG_KEY1:
        // KEY1 - Only bit 0 (switch armed) is writable
        cgb->speed_switch_armed = data & 0x01;
        return 1;

    case REG_VBK:
        // VBK - VRAM bank select
        cgb->vram_bank = data & 0x01;
        cgb_update_vram_bank(cgb, dmg);
        return 1;

    case REG_BCPS:
        dmg->lcd->bcps = data;
        return 1;

    case REG_BCPD: {
        u8 idx = dmg->lcd->bcps & 0x3f;
        // dedup: most games rewrite identical palettes every vblank
        if (dmg->lcd->bg_palette_ram[idx] != data) {
            dmg->lcd->bg_palette_ram[idx] = data;
            // Mark color as dirty (2 bytes per color, so color = idx >> 1)
            dmg->lcd->bg_palette_dirty |= (1UL << (idx >> 1));
            dmg->lcd->palette_frame_dirty = 1;
            dmg_palette_record(dmg, idx, data);
        }
        // Auto-increment if bit 7 is set
        if (dmg->lcd->bcps & 0x80) {
            dmg->lcd->bcps = (dmg->lcd->bcps & 0x80) | ((idx + 1) & 0x3f);
        }
        return 1;
    }

    case REG_OCPS:
        dmg->lcd->ocps = data;
        return 1;

    case REG_OCPD: {
        u8 idx = dmg->lcd->ocps & 0x3f;
        if (dmg->lcd->obj_palette_ram[idx] != data) {
            dmg->lcd->obj_palette_ram[idx] = data;
            // Mark color as dirty (2 bytes per color, so color = idx >> 1)
            dmg->lcd->obj_palette_dirty |= (1UL << (idx >> 1));
            dmg->lcd->palette_frame_dirty = 1;
            dmg_palette_record(dmg, idx | 0x40, data);
        }
        // Auto-increment if bit 7 is set
        if (dmg->lcd->ocps & 0x80) {
            dmg->lcd->ocps = (dmg->lcd->ocps & 0x80) | ((idx + 1) & 0x3f);
        }
        return 1;
    }

    case REG_SVBK:
        cgb->wram_bank = data & 0x07;
        cgb_update_wram_bank(cgb, dmg);
        return 1;

    case REG_HDMA1:
        // Source high byte
        cgb->hdma_source = (cgb->hdma_source & 0x00ff) | (data << 8);
        return 1;

    case REG_HDMA2:
        // Source low byte (bits 0-3 ignored, 16-byte aligned)
        cgb->hdma_source = (cgb->hdma_source & 0xff00) | (data & 0xf0);
        return 1;

    case REG_HDMA3:
        // Destination high byte (bits 5-7 ignored, forced to VRAM $8000-$9FFF)
        cgb->hdma_dest = (cgb->hdma_dest & 0x00ff) | ((data & 0x1f) << 8);
        return 1;

    case REG_HDMA4:
        // Destination low byte (bits 0-3 ignored, 16-byte aligned)
        cgb->hdma_dest = (cgb->hdma_dest & 0xff00) | (data & 0xf0);
        return 1;

    case REG_HDMA5:
        if (cgb->hdma_active && !(data & 0x80)) {
            // cancel: chunks for hblanks already crossed still happen
            hdma_flush_now(dmg);
            cgb->hdma_active = 0;
            // hdma_remaining stays as-is for reads (with bit 7 = 1 set)
        } else if (data & 0x80) {
            // Start HDMA (HBlank DMA): bit 7 = 1
            u32 pos;
            u8 ly = dmg_current_ly(dmg, &pos);

            cgb->hdma_active = 1;
            cgb->hdma_remaining = data & 0x7f;

            if (ly >= 144) {
                // in vblank: first chunk at line 0's hblank next frame
                cgb->hdma_last_ly = 143;
            } else if (pos >= 252) {
                // already in hblank: first chunk transfers now
                cgb_hdma_hblank(cgb, dmg, ly);
                cgb->hdma_last_ly = ly;
            } else {
                // first chunk at this line's hblank
                cgb->hdma_last_ly = ly ? ly - 1 : 0xff;
            }
        } else {
            // Start GPDMA (immediate transfer): bit 7 = 0, no active HDMA
            cgb_perform_gpdma(cgb, dmg, data);
        }
        return 1;
    }

    return 0;
}

void cgb_update_vram_bank(struct cgb_state *cgb, struct dmg *dmg)
{
    u8 *bank_base = &dmg->vram[cgb->vram_bank * 0x2000];

    dmg->read_page[0x8] = PAGE_BIAS(bank_base, 0x8);
    dmg->write_page[0x8] = dmg->read_page[0x8];
    dmg->read_page[0x9] = PAGE_BIAS(&bank_base[0x1000], 0x9);
    dmg->write_page[0x9] = dmg->read_page[0x9];
}

void cgb_update_wram_bank(struct cgb_state *cgb, struct dmg *dmg)
{
    // SVBK 0 is treated as 1
    int bank = cgb->wram_bank ? cgb->wram_bank : 1;
    u8 *bank_base = &dmg->wram[bank * 0x1000];

    // $D000-$DFFF; the echo at $F000-$FDFF is slow-path and follows
    // this mapping through the forward in dmg_read_slow/dmg_write_slow
    dmg->read_page[0xd] = PAGE_BIAS(bank_base, 0xd);
    dmg->write_page[0xd] = dmg->read_page[0xd];
}

int cgb_speed_switch(struct cgb_state *cgb)
{
    if (!cgb->mode) {
        return 0;  // DMG mode - just halt
    }

    if (cgb->speed_switch_armed) {
        // Toggle speed
        cgb->double_speed = !cgb->double_speed;
        cgb->speed_switch_armed = 0;
        return 1;  // Speed switched, continue execution
    }

    return 0;  // Not armed - halt
}

int cgb_hdma_hblank(struct cgb_state *cgb, struct dmg *dmg, u8 ly)
{
    int i;
    u8 *vram;

    if (!cgb->mode || !cgb->hdma_active) {
        return 0;
    }

    // No transfers during VBlank (LY 144-153)
    if (ly >= 144) {
        return 0;
    }

    // Transfer 16 bytes
    vram = &dmg->vram[cgb->vram_bank * 0x2000];
    for (i = 0; i < 16; i++) {
        // Read from source
        u8 data = dmg_read(dmg, cgb->hdma_source);
        cgb->hdma_source++;

        // Write to destination (always in VRAM $8000-$9FFF)
        vram[cgb->hdma_dest & 0x1fff] = data;
        cgb->hdma_dest++;
    }

    // Update remaining count
    if (cgb->hdma_remaining == 0) {
        cgb->hdma_active = 0;
    } else {
        cgb->hdma_remaining--;
    }

    // Return cycles: 8 M-cycles in normal speed, 16 M-cycles in double speed
    // (same real-time duration, but different CPU cycles)
    return cgb->double_speed ? 16 : 8;
}
