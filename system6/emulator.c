/* Game Boy emulator for 68k Macs
   emulator.c - entry point */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <Quickdraw.h>
#include <Fonts.h>
#include <Windows.h>
#include <Menus.h>
#include <TextEdit.h>
#include <Dialogs.h>
#include <Memory.h>
#include <ToolUtils.h>
#include <Devices.h>
#include <Timer.h>
#include <Files.h>
#include <SegLoad.h>
#include <Palettes.h>
#include <Resources.h>
#include <Retrace.h>

#include "emulator.h"

#include "dmg.h"
#include "lcd.h"
#include "rom.h"
#include "mbc.h"
#include "audio.h"

#include "debug.h"
#include "dialogs.h"
#include "input.h"
#include "lcd_mac.h"
#include "dispatcher_asm.h"
#include "arena.h"
#include "cache.h"
#include "jit.h"
#include "settings.h"
#include "audio_mac.h"
#include "palette_menu.h"
#include "gb_palettes.h"

#include "compiler.h"

static void UpdateMenuItems(void);

// Called by dmg.c when ROM bank switches
static void on_rom_bank_switch(int new_bank)
{
    jit_ctx.current_rom_bank = (u8) new_bank;
    // force exit to dispatcher ?
    // only way this is needed is if games switch banks and then don't jump
    // or call afterwards...
}

struct rom rom;
struct lcd lcd;
struct audio audio;
struct dmg dmg;

WindowPtr g_wp;
unsigned char app_running;
unsigned char sound_enabled;
unsigned char limit_fps;
int screen_depth;

static u32 last_frame_count;

// VBL sync for frame limiting
static volatile int vbl_flag;
static VBLTask vbl_task;
static int vbl_installed;

static unsigned long soft_reset_release_tick;


static char save_filename[32];
// for GetFInfo/SetFInfo
static Str63 save_filename_p;

// 2x scaled: 336x288 @ 1bpp = 42 bytes per row (168 GB pixels for scroll offset)
char offscreen_buf[42 * 288];
Rect offscreen_rect = { 0, 0, 288, 320 };  // destination size (window)
BitMap offscreen_bmp;

// color/grayscale mode: 336x288 @ 8bpp (168 GB pixels for scroll offset)
char offscreen_color_buf[336 * 288];
PixMap offscreen_pixmap;
CTabHandle offscreen_ctab;

// Status bar - if set, displayed instead of FPS
char status_bar[64];

static void build_save_filename(void)
{
  int len;

  rom_get_title(&rom, save_filename);
  len = strlen(save_filename);

  // build Pascal string
  save_filename_p[0] = len;
  memcpy(&save_filename_p[1], save_filename, len);
}

static pascal void VBLHandler(void)
{
  VBLTaskPtr task;

  // A0 points to the VBLTask structure
  asm volatile("move.l %%a0, %0" : "=g"(task));

  vbl_flag = 1;
  task->vblCount = 1;  // reschedule for next VBL
}

static void InstallVBL(void)
{
  if (vbl_installed)
    return;

  vbl_task.qType = vType;
  vbl_task.vblAddr = VBLHandler;
  vbl_task.vblCount = 1;
  vbl_task.vblPhase = 0;

  if (VInstall((QElemPtr)&vbl_task) == noErr) {
    vbl_installed = 1;
    vbl_flag = 0;
  }
}

static void RemoveVBL(void)
{
  if (!vbl_installed)
    return;

  VRemove((QElemPtr)&vbl_task);
  vbl_installed = 0;
}

void InitToolbox(void)
{
  Handle mbar;
  MenuHandle apple;

  InitGraf(&qd.thePort);
  InitFonts();
  InitWindows();
  InitMenus();
  TEInit();
  InitDialogs(0L);
  InitCursor();

  MaxApplZone();

  // enable keyUp events (not delivered by default)
  SetEventMask(everyEvent);

  mbar = GetNewMBar(MBAR_DEFAULT);
  SetMenuBar(mbar);
  apple = GetMenuHandle(MENU_APPLE);
  InsertMenuItem(apple, "\p(Version " APP_VERSION, 1);
  AppendResMenu(apple, 'DRVR');
  if (!audio_mac_available()) {
    DisableItem(GetMenuHandle(MENU_EDIT), EDIT_SOUND);
  }
  DrawMenuBar();

  app_running = 1;
}

void set_status_bar(const char *str)
{
  int k;
  Str255 pstr;
  Rect statusRect;
  int height;

  if (!strcmp(str, status_bar)) {
    return;
  }

  for (k = 0; k < 63 && str[k]; k++) {
    status_bar[k] = str[k];
  }
  status_bar[k] = '\0';

  height = (screen_scale == 1) ? 144 : 288;
  statusRect.top = height;
  statusRect.left = 0;
  statusRect.bottom = height + 11;
  statusRect.right = (screen_scale == 1) ? 160 : 320;

  EraseRect(&statusRect);
  MoveTo(4, height + 10);
  for (k = 0; k < 255 && status_bar[k]; k++) {
    pstr[k + 1] = status_bar[k];
  }
  pstr[0] = k;
  DrawString(pstr);
}

void DetectScreenDepth(void)
{
  SysEnvRec env;
  screen_depth = 1;

  if (SysEnvirons(1, &env) == noErr && env.hasColorQD) {
    GDHandle mainDev = GetMainDevice();
    if (mainDev) {
      PixMapHandle pm = (*mainDev)->gdPMap;
      screen_depth = (*pm)->pixelSize;
    }
    InitPalettes();
  }
}

void InitColorOffscreen(void)
{
  GDHandle mainDev;
  PixMapHandle screenPM;
  int width;

  // use screen's color table so CopyBits skips color matching
  mainDev = GetMainDevice();
  screenPM = (*mainDev)->gdPMap;
  offscreen_ctab = (*screenPM)->pmTable;

  // buffer width is wider than display for scroll offset (168 or 336)
  width = (screen_scale == 1) ? 168 : 336;

  // PixMap setup - always 8bpp, CopyBits handles depth conversion
  offscreen_pixmap.baseAddr = offscreen_color_buf;
  offscreen_pixmap.rowBytes = width | 0x8000;  // high bit = PixMap flag
  offscreen_pixmap.bounds.top = 0;
  offscreen_pixmap.bounds.left = 0;
  offscreen_pixmap.bounds.bottom = (screen_scale == 1) ? 144 : 288;
  offscreen_pixmap.bounds.right = width;
  offscreen_pixmap.pmVersion = 0;
  offscreen_pixmap.packType = 0;
  offscreen_pixmap.packSize = 0;
  offscreen_pixmap.hRes = 0x00480000;  // 72 dpi
  offscreen_pixmap.vRes = 0x00480000;
  offscreen_pixmap.pixelType = 0;  // chunky
  offscreen_pixmap.pixelSize = 8;
  offscreen_pixmap.cmpCount = 1;
  offscreen_pixmap.cmpSize = 8;
  offscreen_pixmap.pmTable = offscreen_ctab;
  offscreen_pixmap.pmReserved = 0;
}

void SaveGame(void)
{
  if (mbc_save_ram(dmg.rom->mbc, save_filename)) {
    FInfo fndrInfo;
    if (GetFInfo(save_filename_p, 0, &fndrInfo) == noErr) {
      fndrInfo.fdType = 'SRAM';
      fndrInfo.fdCreator = 'MGBE';
      SetFInfo(save_filename_p, 0, &fndrInfo);
    }
  }
}

void StopEmulation(void)
{
  if (!g_wp) {
    return;
  }

  SaveGame();
  RemoveVBL();
  audio_mac_shutdown();
  jit_cleanup();

  if (screen_depth > 1) {
    PaletteHandle pal = GetPalette(g_wp);
    DisposeWindow(g_wp);
    if (pal) {
      DisposePalette(pal);
    }
  } else {
    DisposeWindow(g_wp);
  }

  g_wp = NULL;
  if (rom.data) {
    DisposePtr((Ptr) rom.data);
    rom.data = NULL;
  }
  UpdateMenuItems();
}

void StartEmulation(void)
{
  int width, height;
  Rect bounds;

  // set up dimensions based on scale
  if (screen_scale == 1) {
    width = 160;
    height = 144;
  } else {
    width = 320;
    height = 288;
  }

  bounds.top = WINDOW_Y;
  bounds.left = WINDOW_X;
  bounds.right = WINDOW_X + width;
  bounds.bottom = WINDOW_Y + height + 11;  // +11 for status bar

  offscreen_rect.right = width;
  offscreen_rect.bottom = height;

  if (screen_depth > 1) {
    g_wp = NewCWindow(0, &bounds, save_filename_p, true,
          noGrowDocProc, (WindowPtr) -1, true, 0);
  } else {
    g_wp = NewWindow(0, &bounds, save_filename_p, true,
          noGrowDocProc, (WindowPtr) -1, true, 0);
  }
  SetPort(g_wp);

  memset(&dmg, 0, sizeof(dmg));
  memset(&lcd, 0, sizeof(lcd));
  lcd_new(&lcd);

  dmg_new(&dmg, &rom, &lcd);
  dmg.rom_bank_switch_hook = on_rom_bank_switch;
  mbc_load_ram(dmg.rom->mbc, save_filename);
  audio_init(&audio);
  dmg.audio = &audio;

  offscreen_bmp.baseAddr = offscreen_buf;
  // bounds is full buffer size (168 or 336 pixels wide for scroll offset handling)
  offscreen_bmp.bounds.top = 0;
  offscreen_bmp.bounds.left = 0;
  offscreen_bmp.bounds.bottom = height;
  offscreen_bmp.bounds.right = (width == 320) ? 336 : 168;
  offscreen_bmp.rowBytes = (width == 320) ? 42 : 21;
  if (screen_depth > 1) {
    InitColorOffscreen();
    // init even if indexed isn;t currently selected so it's correct
    // if they change to indexed in settings
    init_indexed_lut(g_wp);
  }

  jit_init(&dmg);

  if (audio_mac_init(&audio) && sound_enabled) {
    audio_mac_start();
  }

  if (limit_fps) {
    InstallVBL();
  }

  UpdateMenuItems();
}

static void CheckPendingTasks(void)
{
  unsigned long now = TickCount();
  if (soft_reset_release_tick && now >= soft_reset_release_tick) {
    dmg_set_button(&dmg, FIELD_ACTION,
        BUTTON_A | BUTTON_B | BUTTON_SELECT | BUTTON_START, 0);
    soft_reset_release_tick = 0;
  }
}

// called on init, on emulation start, on scale change, and on emulation stop
static void UpdateMenuItems(void)
{
  MenuHandle menu;

  menu = GetMenuHandle(MENU_FILE);
  if (g_wp) {
    if (dmg.rom->mbc->has_battery) {
      EnableItem(menu, FILE_SAVE_GAME);
    } else {
      DisableItem(menu, FILE_SAVE_GAME);
    }
    EnableItem(menu, FILE_SCREENSHOT);
    EnableItem(menu, FILE_SOFT_RESET);
  } else {
    DisableItem(menu, FILE_SAVE_GAME);
    DisableItem(menu, FILE_SCREENSHOT);
    DisableItem(menu, FILE_SOFT_RESET);
  }

  menu = GetMenuHandle(MENU_EDIT);
  CheckItem(menu, EDIT_SOUND, sound_enabled);
  CheckItem(menu, EDIT_LIMIT_FPS, limit_fps);
  CheckItem(menu, EDIT_SCALE_1X, screen_scale == 1);
  CheckItem(menu, EDIT_SCALE_2X, screen_scale == 2);
}

void SetScreenScale(int scale)
{
  int width, height;

  if (scale == screen_scale) {
    return;
  }

  screen_scale = scale;

  // update offscreen rect and pixmap for new scale
  if (scale == 1) {
    width = 160;
    height = 144;
  } else {
    width = 320;
    height = 288;
  }

  offscreen_rect.right = width;
  offscreen_rect.bottom = height;
  // bmp bounds is full buffer size (168 or 336 pixels wide for scroll offset)
  offscreen_bmp.bounds.top = 0;
  offscreen_bmp.bounds.left = 0;
  offscreen_bmp.bounds.bottom = height;
  offscreen_bmp.bounds.right = (width == 320) ? 336 : 168;
  offscreen_bmp.rowBytes = (width == 320) ? 42 : 21;
  // pixmap also uses wider buffer
  offscreen_pixmap.bounds.top = 0;
  offscreen_pixmap.bounds.left = 0;
  offscreen_pixmap.bounds.bottom = height;
  offscreen_pixmap.bounds.right = (width == 320) ? 336 : 168;
  offscreen_pixmap.rowBytes = ((width == 320) ? 336 : 168) | 0x8000;

  if (g_wp) {
    Rect newBounds;
    PaletteHandle pal;

    if (screen_depth > 1) {
      pal = GetPalette(g_wp);
      DisposeWindow(g_wp);
      if (pal) {
        DisposePalette(pal);
      }
    } else {
      DisposeWindow(g_wp);
    }

    // create new window with updated size
    newBounds.top = WINDOW_Y;
    newBounds.left = WINDOW_X;
    newBounds.right = WINDOW_X + width;
    newBounds.bottom = WINDOW_Y + height + 11;  // +11 for status bar

    if (screen_depth > 1) {
      g_wp = NewCWindow(0, &newBounds, save_filename_p, true,
            noGrowDocProc, (WindowPtr) -1, true, 0);
    } else {
      g_wp = NewWindow(0, &newBounds, save_filename_p, true,
            noGrowDocProc, (WindowPtr) -1, true, 0);
    }
    SetPort(g_wp);

    if (screen_depth > 1) {
      init_indexed_lut(g_wp);
    }
  }

  UpdateMenuItems();
  SavePreferences();
}

int LoadRom(Str63 fileName, short vRefNum)
{
  int err;
  short fileNo;
  long amtRead;
  FInfo fndrInfo;

  // stop emulation first to free memory before allocating new ROM
  StopEmulation();

  err = FSOpen(fileName, vRefNum, &fileNo);
  
  if(err != noErr) {
    return false;
  }
  
  GetEOF(fileNo, (long *) &rom.length);
  rom.data = (unsigned char *) NewPtr(rom.length);
  if(rom.data == NULL) {
    ShowCenteredAlert(ALRT_NOT_ENOUGH_RAM, "\p", "\p", "\p", "\p", ALERT_NORMAL);
    return false;
  }
  
  amtRead = rom.length;
  FSRead(fileNo, &amtRead, rom.data);
  FSClose(fileNo);

  rom.mbc = mbc_new(rom.data[0x147]);
  if (!rom.mbc) {
    ShowCenteredAlert(
        ALRT_4_LINE,
        "\pThis cartridge type is unsupported.", "\p", "\p", "\p",
        ALERT_NORMAL
    );
    return false;
  }

  if (MaxBlock() < BASE_MEMORY_REQUIRED) {
    ShowCenteredAlert(
        ALRT_4_LINE,
        "\pI don't have much memory left after", 
        "\ploading the ROM. I'll keep going, but", 
        "\ptry giving me more in Get Info from",
        "\pthe Finder for the best performance.",
        ALERT_NORMAL
    );
  }

  if (GetFInfo(fileName, vRefNum, &fndrInfo) == noErr) {
    fndrInfo.fdType = 'GBRM';
    fndrInfo.fdCreator = 'MGBE';
    SetFInfo(fileName, vRefNum, &fndrInfo);
  }

  build_save_filename();

  return true;
}

void OnMenuAction(long action)
{
  short menu, item;
  
  if(action <= 0)
    return;

  HiliteMenu(0);
  
  menu = HiWord(action);
  item = LoWord(action);
  
  if(menu == MENU_APPLE) {
    if(item == APPLE_ABOUT) {
      ShowAboutBox();
    } else {
      Str255 daName;
      GetMenuItemText(GetMenuHandle(MENU_APPLE), item, daName);
      OpenDeskAcc(daName);
    }
  }
  
  else if(menu == MENU_FILE) {
    if(item == FILE_OPEN) {
      if(ShowOpenBox())
        StartEmulation();
    }
    else if (item == FILE_SAVE_GAME) {
      if (g_wp) {
        SaveGame();
      }
    } 
    else if(item == FILE_SCREENSHOT) {
      if (g_wp) {
        SaveScreenshot();
      }
    }
    else if(item == FILE_SOFT_RESET) {
      if (g_wp) {
        dmg_set_button(&dmg, FIELD_ACTION,
            BUTTON_A | BUTTON_B | BUTTON_SELECT | BUTTON_START, 1);
        soft_reset_release_tick = TickCount() + SOFT_RESET_TICKS;
      }
    }
    else if(item == FILE_CLOSE) {
      StopEmulation();
    }
    else if(item == FILE_QUIT) {
      app_running = 0;
    }
  }

  else if (menu == MENU_PALETTES) {
    if (item >= 1 && item <= gb_palette_count) {
      current_palette = item - 1;
      if (g_wp) {
        PaletteHandle pal;
        pal = GetPalette(g_wp);
        if (pal) {
          DisposePalette(pal);
        }
        init_indexed_lut(g_wp);
      }
      SavePreferences();
    }
  }

  else if (menu == MENU_EDIT) {
    if (item == EDIT_SOUND) {
      sound_enabled = !sound_enabled;
      if (sound_enabled) {
        audio_mac_start();
      } else {
        audio_mac_stop();
      }
      CheckItem(GetMenuHandle(MENU_EDIT), EDIT_SOUND, sound_enabled);
      SavePreferences();
    } else if (item == EDIT_LIMIT_FPS) {
      limit_fps = !limit_fps;
      if (limit_fps) {
        InstallVBL();
      } else {
        RemoveVBL();
      }
      CheckItem(GetMenuHandle(MENU_EDIT), EDIT_LIMIT_FPS, limit_fps);
      SavePreferences();
    } else if (item == EDIT_SCALE_1X) {
      SetScreenScale(1);
    } else if (item == EDIT_SCALE_2X) {
      SetScreenScale(2);
    } else if (item == EDIT_KEY_MAPPINGS) {
      ShowKeyMappingsDialog();
    } else if (item == EDIT_PREFERENCES) {
      int old_cycles_per_exit = cycles_per_exit;
      ShowPreferencesDialog();
      if (cycles_per_exit != old_cycles_per_exit && g_wp) {
        if (!jit_clear_all_blocks()) {
          // no-op. failure here causes the JIT to stop with a 
          // status bar message, so no special error handling needed
        }
      }
    }
  }
}

void OnMouseDown(EventRecord *pEvt)
{
  short part;
  WindowPtr clicked;
  long action;
  
  part = FindWindow(pEvt->where, &clicked);
  
  switch(part) {
    case inDrag:
      DragWindow(clicked, pEvt->where, &qd.screenBits.bounds);
      break;
    case inGoAway:
      if(TrackGoAway(clicked, pEvt->where)) {
        StopEmulation();
      }
      break;
    case inContent:
      if(clicked != FrontWindow())
        SelectWindow(clicked);
      break;
    case inMenuBar:
      action = MenuSelect(pEvt->where);
      OnMenuAction(action);
      break;
  }
}

// process pending events, returns 0 if app should quit
static int ProcessEvents(void)
{
  EventRecord evt;

  while (GetNextEvent(everyEvent, &evt)) {
    switch (evt.what) {
      case mouseDown:
        OnMouseDown(&evt);
        break;
      case updateEvt:
        BeginUpdate((WindowPtr) evt.message);
        EndUpdate((WindowPtr) evt.message);
        break;
      case keyDown:
      case autoKey:
        if (evt.modifiers & cmdKey) {
          char ch = evt.message & charCodeMask;
          // Command-T: toggle trace mode
          if (ch == 't' && g_wp) {
            jit_ctx.trace_enabled = !jit_ctx.trace_enabled;
            if (jit_ctx.trace_enabled) {
              set_status_bar("Trace ON");
            } else {
              set_status_bar("Trace OFF");
            }
          } else {
            OnMenuAction(MenuKey(ch));
          }
        } else if (g_wp) {
          int key = (evt.message & keyCodeMask) >> 8;
          HandleKeyEvent(key, 1);
        }
        break;
      case keyUp:
        if (g_wp) {
          int key = (evt.message & keyCodeMask) >> 8;
          HandleKeyEvent(key, 0);
        }
        break;
    }

    if (!app_running) {
      return 0;
    }
  }

  return 1;
}

// check for files passed from Finder on launch
// returns 1 if ROM loaded, 0 if should show open dialog
static int CheckFinderFiles(void)
{
  short action, count;
  AppFile theFile;

  CountAppFiles(&action, &count);
  if (count == 0) {
    return 0;
  }

  GetAppFiles(1, &theFile);

  if (theFile.fType == 'GBRM') {
    if (LoadRom(theFile.fName, theFile.vRefNum)) {
      ClrAppFiles(1);
      return 1;
    }
  } else if (theFile.fType == 'SRAM') {
    ShowCenteredAlert(
        ALRT_4_LINE,
        "\pSave files cannot be opened directly.",
        "\pOpen the ROM instead, and the save",
        "\pwill be loaded automatically.",
        "\p",
        ALERT_CAUTION
    );
    ClrAppFiles(1);
    return 0;
  }

  ClrAppFiles(1);
  return 0;
}

int main(int argc, char *argv[])
{
  int finderResult;

  InitToolbox();
  DetectScreenDepth();
  // if (screen_depth > 1) {
  //   InstallPalettesMenu();
  //   DrawMenuBar();
  // }
  LoadKeyMappings();
  LoadPreferences();
  UpdateMenuItems();

  init_dither_lut();
  lcd_init_lut();

  finderResult = CheckFinderFiles();
  if (finderResult == 1 || ShowOpenBox()) {
    StartEmulation();
  }

  last_frame_count = 0;

  while (app_running) {
    if (!ProcessEvents()) {
      break;
    }

    if (g_wp) {
      CheckPendingTasks();
      jit_run(&dmg);

      if (limit_fps && dmg.frames_rendered != last_frame_count) {
        last_frame_count = dmg.frames_rendered;

        if (sound_enabled) {
          // use audio buffer fill level as frame pacer
          audio_mac_wait_if_ahead();
        } else {
          // wait for VBL interrupt to fire
          while (!vbl_flag)
            ;
          vbl_flag = 0;
        }
      }
    }
  }

  StopEmulation();
  return 0;
}
