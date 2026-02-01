#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "types.h"
#include "mbc.h"
#include "dmg.h"
#include "rom.h"

#include <OSUtils.h>

#define RTC_SAVE_SIZE 48

// storage with padding for manual 8KB alignment
static u8 mbc_ram_storage[RAM_SIZE + 8192];

static int is_mbc2(int type)
{
  return type == 0x05 || type == 0x06;
}

static int is_mbc3(int type)
{
  return type >= 0x0f && type <= 0x13;
}

static int is_mbc5(int type)
{
  return type >= 0x19 && type <= 0x1e;
}

static int mbc1_write(struct mbc *mbc, struct dmg *dmg, u16 addr, u8 data)
{
  if (addr >= 0 && addr <= 0x1fff) {
    int was_enabled = mbc->ram_enabled;
    mbc->ram_enabled = (data & 0xf) == 0xa;
    if (mbc->ram_enabled != was_enabled) {
      u8 *ram_base = mbc->ram_enabled ? &mbc->ram[0x2000 * mbc->ram_bank] : NULL;
      dmg_update_ram_bank(dmg, ram_base);
    }
    return 1;
  } else if (addr >= 0x2000 && addr <= 0x3fff) {
    mbc->rom_bank = data & 0x1f;
    int use_bank = mbc->rom_bank ? mbc->rom_bank : 1;
    dmg_update_rom_bank(dmg, use_bank);
    return 1;
  } else if (addr >= 0x4000 && addr <= 0x5fff) {
    mbc->ram_bank = data & 0x03;
    if (mbc->ram_enabled) {
      dmg_update_ram_bank(dmg, &mbc->ram[0x2000 * mbc->ram_bank]);
    }
    return 1;
  } else if (addr >= 0x6000 && addr <= 0x7fff) {
    //printf("sel %d\n", data);
    return 1;
  }
  return 0;
}

static int mbc2_write(struct mbc *mbc, struct dmg *dmg, u16 addr, u8 data)
{
  if (addr <= 0x3fff) {
    // Bit 8 of address determines RAM enable vs ROM bank
    if ((addr & 0x0100) == 0) {
      // RAM enable (only lower 4 bits of data matter)
      mbc->ram_enabled = (data & 0x0f) == 0x0a;
      // MBC2 RAM is accessed through mbc_ram_read/write, no bank pointer
    } else {
      // ROM bank number (lower 4 bits, 0 becomes 1)
      mbc->rom_bank = data & 0x0f;
      int use_bank = mbc->rom_bank ? mbc->rom_bank : 1;
      dmg_update_rom_bank(dmg, use_bank);
    }
    return 1;
  }
  return 0;
}

// Get current Mac system time in seconds since Jan 1, 1904
static u32 get_mac_seconds(void)
{
  unsigned long secs;
  GetDateTime(&secs);
  return (u32)secs;
}

// Reset RTC reference point to current time with current register values
static void mbc3_reset_rtc_reference(struct mbc *mbc)
{
  mbc->rtc_base_secs = get_mac_seconds();
  mbc->rtc_base_days = ((mbc->rtc_dh & 0x01) << 8) | mbc->rtc_dl;
  mbc->rtc_base_h = mbc->rtc_h;
  mbc->rtc_base_m = mbc->rtc_m;
  mbc->rtc_base_s = mbc->rtc_s;
  mbc->rtc_halted = (mbc->rtc_dh & 0x40) ? 1 : 0;
}

// Compute current RTC values from elapsed time since reference
static void mbc3_update_rtc(struct mbc *mbc)
{
  u32 elapsed_secs;
  u32 total_secs;
  u16 days;

  // If halted, don't update - registers stay at their set values
  if (mbc->rtc_halted) {
    return;
  }

  // Calculate seconds elapsed since reference point
  elapsed_secs = get_mac_seconds() - mbc->rtc_base_secs;

  // Convert base time to total seconds
  total_secs = (u32)mbc->rtc_base_days * 86400UL
             + (u32)mbc->rtc_base_h * 3600UL
             + (u32)mbc->rtc_base_m * 60UL
             + (u32)mbc->rtc_base_s;

  // Add elapsed time
  total_secs += elapsed_secs;

  // Extract components
  mbc->rtc_s = total_secs % 60;
  total_secs /= 60;
  mbc->rtc_m = total_secs % 60;
  total_secs /= 60;
  mbc->rtc_h = total_secs % 24;
  total_secs /= 24;
  days = (u16)total_secs;

  // Day counter is 9 bits (0-511), with carry flag if overflow
  mbc->rtc_dl = days & 0xff;
  mbc->rtc_dh = (mbc->rtc_dh & 0x40)    // preserve halt flag
              | ((days >> 8) & 0x01)     // day counter bit 8
              | ((days > 511) ? 0x80 : 0);  // carry flag if >511 days
}

static void mbc3_latch_rtc(struct mbc *mbc)
{
  // First compute current RTC values from Mac time
  if (mbc->has_rtc) {
    mbc3_update_rtc(mbc);
  }

  // Then copy to latched registers
  mbc->rtc_latched[0] = mbc->rtc_s;
  mbc->rtc_latched[1] = mbc->rtc_m;
  mbc->rtc_latched[2] = mbc->rtc_h;
  mbc->rtc_latched[3] = mbc->rtc_dl;
  mbc->rtc_latched[4] = mbc->rtc_dh;
}

static int mbc3_write(struct mbc *mbc, struct dmg *dmg, u16 addr, u8 data)
{
  if (addr <= 0x1fff) {
    // RAM and Timer Enable
    int was_enabled = mbc->ram_enabled;
    mbc->ram_enabled = (data & 0xf) == 0xa;
    if (mbc->ram_enabled != was_enabled) {
      if (mbc->ram_enabled && mbc->rtc_select < 0) {
        dmg_update_ram_bank(dmg, &mbc->ram[0x2000 * mbc->ram_bank]);
      } else {
        dmg_update_ram_bank(dmg, NULL);
      }
    }
    return 1;
  }

  if (addr >= 0x2000 && addr <= 0x3fff) {
    // ROM Bank Number (7 bits)
    mbc->rom_bank = data & 0x7f;
    int use_bank = mbc->rom_bank ? mbc->rom_bank : 1;
    dmg_update_rom_bank(dmg, use_bank);
    return 1;
  }

  if (addr >= 0x4000 && addr <= 0x5fff) {
    // RAM Bank Number or RTC Register Select
    if (data <= 0x03) {
      // Select RAM bank
      mbc->ram_bank = data;
      mbc->rtc_select = -1;
      if (mbc->ram_enabled) {
        dmg_update_ram_bank(dmg, &mbc->ram[0x2000 * mbc->ram_bank]);
      }
    } else if (data >= 0x08 && data <= 0x0c) {
      // Select RTC register
      mbc->rtc_select = data;
      // RTC reads go through mbc_read, so set RAM to NULL
      dmg_update_ram_bank(dmg, NULL);
    }
    return 1;
  }

  if (addr >= 0x6000 && addr <= 0x7fff) {
    // Latch Clock Data
    if (mbc->rtc_latch_state == 0x00 && data == 0x01) {
      mbc3_latch_rtc(mbc);
    }
    mbc->rtc_latch_state = data;
    return 1;
  }

  return 0;
}

static int mbc5_write(struct mbc *mbc, struct dmg *dmg, u16 addr, u8 data)
{
  if (addr <= 0x1fff) {
    // RAM Enable
    int was_enabled = mbc->ram_enabled;
    mbc->ram_enabled = (data & 0x0f) == 0x0a;
    if (mbc->ram_enabled != was_enabled) {
      u8 *ram_base = mbc->ram_enabled ? &mbc->ram[0x2000 * mbc->ram_bank] : NULL;
      dmg_update_ram_bank(dmg, ram_base);
    }
    return 1;
  }

  if (addr >= 0x2000 && addr <= 0x2fff) {
    // Low 8 bits of ROM bank number
    mbc->rom_bank = (mbc->rom_bank & 0x100) | data;
    dmg_update_rom_bank(dmg, mbc->rom_bank);
    return 1;
  }

  if (addr >= 0x3000 && addr <= 0x3fff) {
    // 9th bit of ROM bank number
    mbc->rom_bank = (mbc->rom_bank & 0xff) | ((data & 0x01) << 8);
    dmg_update_rom_bank(dmg, mbc->rom_bank);
    return 1;
  }

  if (addr >= 0x4000 && addr <= 0x5fff) {
    // RAM bank number (0x00-0x0f)
    mbc->ram_bank = data & 0x0f;
    if (mbc->ram_enabled) {
      dmg_update_ram_bank(dmg, &mbc->ram[0x2000 * mbc->ram_bank]);
    }
    return 1;
  }

  return 0;
}

struct mbc *mbc_new(int type)
{
  static struct mbc mbc;

  // MBC1 types: 0x00-0x03
  // MBC2 types: 0x05-0x06
  // MBC3 types: 0x0f-0x13
  // MBC5 types: 0x19-0x1e
  if (type > 3 && !is_mbc2(type) && !is_mbc3(type) && !is_mbc5(type)) {
    return NULL;
  }

  memset(&mbc, 0, sizeof mbc);
  mbc.type = type;
  mbc.rtc_select = -1;
  // manually align since linker may not honor __attribute__((aligned))
  mbc.ram = (u8 *)(((u32)mbc_ram_storage + 8191) & ~8191);

  if (type == 3) {
    // MBC1+RAM+BATTERY
    mbc.has_battery = 1;
  } else if (is_mbc2(type)) {
    // 0x05: MBC2
    // 0x06: MBC2+BATTERY
    mbc.has_battery = (type == 0x06);
  } else if (is_mbc3(type)) {
    // 0x0f: MBC3+TIMER+BATTERY
    // 0x10: MBC3+TIMER+RAM+BATTERY
    // 0x11: MBC3
    // 0x12: MBC3+RAM
    // 0x13: MBC3+RAM+BATTERY
    mbc.has_rtc = (type == 0x0f || type == 0x10);
    mbc.has_battery = (type == 0x0f || type == 0x10 || type == 0x13);
    // Initialize RTC base timestamp (will be overwritten by mbc_load_ram if save exists)
    if (mbc.has_rtc) {
      mbc.rtc_base_secs = get_mac_seconds();
    }
  } else if (is_mbc5(type)) {
    // 0x19: MBC5
    // 0x1a: MBC5+RAM
    // 0x1b: MBC5+RAM+BATTERY
    // 0x1c: MBC5+RUMBLE
    // 0x1d: MBC5+RUMBLE+RAM
    // 0x1e: MBC5+RUMBLE+RAM+BATTERY
    mbc.has_battery = (type == 0x1b || type == 0x1e);
  }

  return &mbc;
}

int mbc_write(struct mbc *mbc, struct dmg *dmg, u16 addr, u8 data)
{
  if (mbc->type == 0) {
    return 0;
  }
  if (is_mbc2(mbc->type)) {
    return mbc2_write(mbc, dmg, addr, data);
  }
  if (is_mbc3(mbc->type)) {
    return mbc3_write(mbc, dmg, addr, data);
  }
  if (is_mbc5(mbc->type)) {
    return mbc5_write(mbc, dmg, addr, data);
  }
  return mbc1_write(mbc, dmg, addr, data);
}

int mbc_ram_read(struct mbc *mbc, u16 addr, u8 *out)
{
  if (!mbc->ram_enabled) {
    return 0;
  }

  if (is_mbc2(mbc->type)) {
    // MBC2: 512×4 bits, only bottom 9 bits of address used
    int index = addr & 0x1ff;
    *out = mbc->ram[index] | 0xf0;  // Upper 4 bits undefined, set to 1
    return 1;
  }

  if (!is_mbc3(mbc->type) || mbc->rtc_select < 0) {
    return 0;
  }

  // RTC register read
  switch (mbc->rtc_select) {
    case 0x08: *out = mbc->rtc_latched[0]; return 1;
    case 0x09: *out = mbc->rtc_latched[1]; return 1;
    case 0x0a: *out = mbc->rtc_latched[2]; return 1;
    case 0x0b: *out = mbc->rtc_latched[3]; return 1;
    case 0x0c: *out = mbc->rtc_latched[4]; return 1;
  }
  return 0;
}

int mbc_ram_write(struct mbc *mbc, u16 addr, u8 data)
{
  if (!mbc->ram_enabled) {
    return 0;
  }

  if (is_mbc2(mbc->type)) {
    // MBC2: 512×4 bits, only bottom 9 bits of address used
    int index = addr & 0x1ff;
    mbc->ram[index] = data & 0x0f;  // Only lower 4 bits stored
    return 1;
  }

  if (!is_mbc3(mbc->type) || mbc->rtc_select < 0) {
    return 0;
  }

  // RTC register write - update register and reset reference point
  switch (mbc->rtc_select) {
    case 0x08:
      mbc->rtc_s = data & 0x3f;
      mbc3_reset_rtc_reference(mbc);
      return 1;
    case 0x09:
      mbc->rtc_m = data & 0x3f;
      mbc3_reset_rtc_reference(mbc);
      return 1;
    case 0x0a:
      mbc->rtc_h = data & 0x1f;
      mbc3_reset_rtc_reference(mbc);
      return 1;
    case 0x0b:
      mbc->rtc_dl = data;
      mbc3_reset_rtc_reference(mbc);
      return 1;
    case 0x0c:
      mbc->rtc_dh = data & 0xc1;
      mbc->rtc_halted = (data & 0x40) ? 1 : 0;
      mbc3_reset_rtc_reference(mbc);
      return 1;
  }
  return 0;
}

int mbc_save_ram(struct mbc *mbc, const char *filename)
{
  FILE *fp;
  u8 rtc_data[RTC_SAVE_SIZE];

  if (!mbc->has_battery) {
    return 0;
  }

  fp = fopen(filename, "w");
  if (!fp) {
    return 0;
  }

  if (fwrite(mbc->ram, 1, RAM_SIZE, fp) < RAM_SIZE) {
    fclose(fp);
    return 0;
  }

  // Write RTC data if this cartridge has RTC
  if (mbc->has_rtc) {
    // Update RTC to current time before saving
    mbc3_update_rtc(mbc);

    memset(rtc_data, 0, RTC_SAVE_SIZE);
    rtc_data[0] = mbc->rtc_s;
    rtc_data[1] = mbc->rtc_m;
    rtc_data[2] = mbc->rtc_h;
    rtc_data[3] = mbc->rtc_dl;
    rtc_data[4] = mbc->rtc_dh;
    rtc_data[5] = mbc->rtc_halted;
    // Store save timestamp at offset 8 (4 bytes, big endian)
    {
      u32 ts = get_mac_seconds();
      rtc_data[8] = (ts >> 24) & 0xff;
      rtc_data[9] = (ts >> 16) & 0xff;
      rtc_data[10] = (ts >> 8) & 0xff;
      rtc_data[11] = ts & 0xff;
    }

    if (fwrite(rtc_data, 1, RTC_SAVE_SIZE, fp) < RTC_SAVE_SIZE) {
      fclose(fp);
      return 0;
    }
  }

  fclose(fp);
  return 1;
}

int mbc_load_ram(struct mbc *mbc, const char *filename)
{
  FILE *fp;
  u8 rtc_data[RTC_SAVE_SIZE];
  long file_size;

  if (!mbc->has_battery) {
    return 0;
  }

  fp = fopen(filename, "r");
  if (!fp) {
    // No save file exists - initialize RTC to midnight if has_rtc
    if (mbc->has_rtc) {
      mbc->rtc_s = 0;
      mbc->rtc_m = 0;
      mbc->rtc_h = 0;
      mbc->rtc_dl = 0;
      mbc->rtc_dh = 0;
      mbc->rtc_halted = 0;
      mbc3_reset_rtc_reference(mbc);
    }
    return 0;
  }
  
  // Get file size to check for RTC data
  fseek(fp, 0, SEEK_END);
  file_size = ftell(fp);
  fseek(fp, 0, SEEK_SET);
  
  if (fread(mbc->ram, 1, RAM_SIZE, fp) < RAM_SIZE) {
    fclose(fp);
    return 0;
  }

  // Try to read RTC data if cartridge has RTC
  if (mbc->has_rtc) {
    if (file_size >= RAM_SIZE + RTC_SAVE_SIZE &&
        fread(rtc_data, 1, RTC_SAVE_SIZE, fp) == RTC_SAVE_SIZE) {
      // New format with RTC data
      u32 save_timestamp;

      mbc->rtc_s = rtc_data[0];
      mbc->rtc_m = rtc_data[1];
      mbc->rtc_h = rtc_data[2];
      mbc->rtc_dl = rtc_data[3];
      mbc->rtc_dh = rtc_data[4];
      mbc->rtc_halted = rtc_data[5];

      // Read save timestamp (big endian)
      save_timestamp = ((u32)rtc_data[8] << 24)
                     | ((u32)rtc_data[9] << 16)
                     | ((u32)rtc_data[10] << 8)
                     | (u32)rtc_data[11];

      // Set base to the saved timestamp so elapsed time is automatically added
      mbc->rtc_base_secs = save_timestamp;
      mbc->rtc_base_days = ((mbc->rtc_dh & 0x01) << 8) | mbc->rtc_dl;
      mbc->rtc_base_h = mbc->rtc_h;
      mbc->rtc_base_m = mbc->rtc_m;
      mbc->rtc_base_s = mbc->rtc_s;
    }
  }

  fclose(fp);
  return 1;
}