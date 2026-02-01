#include <string.h>

#include "cgb.h"
#include "dmg.h"
#include "lcd.h"
#include "types.h"

void cgb_init(struct cgb_state *cgb, int cgb_flag)
{
    memset(cgb, 0, sizeof(struct cgb_state));

    // Set CGB mode based on ROM header flag
    // 0x80 = CGB enhanced (works on DMG too)
    // 0xC0 = CGB only
    cgb->mode = (cgb_flag == 0x80 || cgb_flag == 0xC0) ? 1 : 0;
    cgb->double_speed = 0;
    cgb->speed_switch_armed = 0;
    cgb->vram_bank = 0;
    cgb->wram_bank = 1;  // SVBK defaults to bank 1

    // Initialize HDMA state
    cgb->hdma_src = 0;
    cgb->hdma_dst = 0x8000;
    cgb->hdma_length = 0x7F;  // inactive
    cgb->hdma_active = 0;
    cgb->hdma_mode = 0;
    cgb->hdma_last_line = 0;
}

int cgb_read_reg(struct cgb_state *cgb, struct lcd *lcd, u16 address, u8 *out)
{
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

    case REG_HDMA1:
        *out = (cgb->hdma_src >> 8) & 0xff;
        return 1;

    case REG_HDMA2:
        *out = cgb->hdma_src & 0xf0;
        return 1;

    case REG_HDMA3:
        *out = (cgb->hdma_dst >> 8) & 0x1f;
        return 1;

    case REG_HDMA4:
        *out = cgb->hdma_dst & 0xf0;
        return 1;

    case REG_HDMA5:
        // Bit 7 = 0 means HDMA active, 1 means inactive
        // Bits 0-6 = remaining length - 1
        if (cgb->hdma_active) {
            *out = cgb->hdma_length;  // bit 7 already 0
        } else {
            *out = 0xff;  // inactive: bit 7 = 1, length = 0x7F
        }
        return 1;
    }

    return 0;
}

int cgb_write_reg(struct cgb_state *cgb, struct dmg *dmg, u16 address, u8 data)
{
    int k;

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
        dmg->lcd->bg_palette_ram[idx] = data;
        // Mark color as dirty (2 bytes per color, so color = idx >> 1)
        dmg->lcd->bg_palette_dirty |= (1UL << (idx >> 1));
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
        dmg->lcd->obj_palette_ram[idx] = data;
        // Mark color as dirty (2 bytes per color, so color = idx >> 1)
        dmg->lcd->obj_palette_dirty |= (1UL << (idx >> 1));
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
        cgb->hdma_src = (cgb->hdma_src & 0x00f0) | (data << 8);
        return 1;

    case REG_HDMA2:
        // Source low byte (bits 0-3 ignored)
        cgb->hdma_src = (cgb->hdma_src & 0xff00) | (data & 0xf0);
        return 1;

    case REG_HDMA3:
        // Dest high byte (forced to $80-$9F range)
        cgb->hdma_dst = (cgb->hdma_dst & 0x00f0) | (((data & 0x1f) | 0x80) << 8);
        return 1;

    case REG_HDMA4:
        // Dest low byte (bits 0-3 ignored)
        cgb->hdma_dst = (cgb->hdma_dst & 0xff00) | (data & 0xf0);
        return 1;

    case REG_HDMA5:
        // Writing to HDMA5 starts or cancels a transfer

        // If HDMA is active and bit 7 is clear, cancel the HDMA (don't start GPDMA)
        // This is used by Pokemon Crystal to terminate HDMA early
        if (cgb->hdma_active && cgb->hdma_mode == 1 && !(data & 0x80)) {
            cgb->hdma_active = 0;
            // Keep the current hdma_length (don't update from data)
            // After cancellation, bit 7 reads as 1 (inactive) with remaining length
            return 1;
        }

        cgb->hdma_length = data & 0x7f;
        cgb->hdma_mode = (data >> 7) & 1;

        if (cgb->hdma_mode == 0) {
            // GPDMA: immediate transfer, blocks CPU
            int bytes = (cgb->hdma_length + 1) * 16;
            for (k = 0; k < bytes; k++) {
                u8 val = dmg_read(dmg, cgb->hdma_src);
                // Write directly to VRAM (respects current bank)
                dmg->video_ram[(cgb->vram_bank * 0x2000) + (cgb->hdma_dst & 0x1fff)] = val;
                cgb->hdma_src++;
                cgb->hdma_dst++;
                // Destination wraps within $8000-$9FFF
                if ((cgb->hdma_dst & 0x1fff) == 0) {
                    cgb->hdma_dst = 0x8000;
                }
            }
            cgb->hdma_active = 0;
            cgb->hdma_length = 0x7f;
        } else {
            // HDMA: set up for per-HBlank transfer
            cgb->hdma_active = 1;
        }
        return 1;
    }

    return 0;
}

void cgb_update_vram_bank(struct cgb_state *cgb, struct dmg *dmg)
{
    int k;
    u8 *bank_base = &dmg->video_ram[cgb->vram_bank * 0x2000];

    for (k = 0x80; k <= 0x9f; k++) {
        int offset = (k - 0x80) << 8;
        dmg->read_page[k] = &bank_base[offset];
        dmg->write_page[k] = &bank_base[offset];
    }
}

void cgb_update_wram_bank(struct cgb_state *cgb, struct dmg *dmg)
{
    int k;
    // SVBK 0 is treated as 1
    int bank = cgb->wram_bank ? cgb->wram_bank : 1;
    u8 *bank_base = &dmg->main_ram[bank * 0x1000];

    // Update $D000-$DFFF (pages 0xd0-0xdf)
    for (k = 0xd0; k <= 0xdf; k++) {
        int offset = (k - 0xd0) << 8;
        dmg->read_page[k] = &bank_base[offset];
        dmg->write_page[k] = &bank_base[offset];
    }

    // Update echo RAM $F000-$FDFF (pages 0xf0-0xfd)
    for (k = 0xf0; k <= 0xfd; k++) {
        int offset = (k - 0xf0) << 8;
        dmg->read_page[k] = &bank_base[offset];
        dmg->write_page[k] = &bank_base[offset];
    }
}

// Transfer one 16-byte HDMA chunk
static void hdma_transfer_chunk(struct cgb_state *cgb, struct dmg *dmg)
{
    int k;
    for (k = 0; k < 16; k++) {
        u8 val = dmg_read(dmg, cgb->hdma_src);
        dmg->video_ram[(cgb->vram_bank * 0x2000) + (cgb->hdma_dst & 0x1fff)] = val;
        cgb->hdma_src++;
        cgb->hdma_dst++;
        // Destination wraps within $8000-$9FFF
        if ((cgb->hdma_dst & 0x1fff) == 0) {
            cgb->hdma_dst = 0x8000;
        }
    }

    // Decrement remaining length
    if (cgb->hdma_length == 0) {
        // Transfer complete
        cgb->hdma_active = 0;
        cgb->hdma_length = 0x7f;
    } else {
        cgb->hdma_length--;
    }
}

void cgb_process_hdma(struct cgb_state *cgb, struct dmg *dmg, int current_line)
{
    if (!cgb->hdma_active || cgb->hdma_mode != 1) {
        return;
    }

    // Only transfer during visible scanlines (0-143)
    if (current_line < 144) {
        // Transfer chunks for any lines we've passed since last sync
        while (cgb->hdma_last_line < current_line && cgb->hdma_active) {
            hdma_transfer_chunk(cgb, dmg);
            cgb->hdma_last_line++;
        }
    }
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
