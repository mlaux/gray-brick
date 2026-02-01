// Game Boy emulator for 68k Macs
// dialogs.c - open ROM, alerts, key mappings, about

#include <Dialogs.h>
#include <Files.h>
#include <StandardFile.h>
#include <Resources.h>
#include <Menus.h>
#include "emulator.h"
#include "dialogs.h"
#include "settings.h"
#include "gb_palettes.h"

// default mappings: up=W, down=S, left=A, right=D, a=L, b=K, select=N, start=M
static unsigned char defaultMappings[8] = { 0x0d, 0x01, 0x00, 0x02, 0x25, 0x28, 0x2d, 0x2e };

static short gSelectedSlot = -1;
int keyMappings[8];

// compiler needs this so it's defined in there...
// int cycles_per_exit;

int frame_skip;
int video_mode;
int screen_scale;

static int cyclesValues[2] = { 70224, 7296 };

const char *keyNames[128] = {
  "\pA",        // 0x00
  "\pS",        // 0x01
  "\pD",        // 0x02
  "\pF",        // 0x03
  "\pH",        // 0x04
  "\pG",        // 0x05
  "\pZ",        // 0x06
  "\pX",        // 0x07
  "\pC",        // 0x08
  "\pV",        // 0x09
  NULL,         // 0x0a
  "\pB",        // 0x0b
  "\pQ",        // 0x0c
  "\pW",        // 0x0d
  "\pE",        // 0x0e
  "\pR",        // 0x0f
  "\pY",        // 0x10
  "\pT",        // 0x11
  "\p1",        // 0x12
  "\p2",        // 0x13
  "\p3",        // 0x14
  "\p4",        // 0x15
  "\p6",        // 0x16
  "\p5",        // 0x17
  "\p=",        // 0x18
  "\p9",        // 0x19
  "\p7",        // 0x1a
  "\p-",        // 0x1b
  "\p8",        // 0x1c
  "\p0",        // 0x1d
  "\p]",        // 0x1e
  "\pO",        // 0x1f
  "\pU",        // 0x20
  "\p[",        // 0x21
  "\pI",        // 0x22
  "\pP",        // 0x23
  "\pReturn",   // 0x24
  "\pL",        // 0x25
  "\pJ",        // 0x26
  "\p'",        // 0x27
  "\pK",        // 0x28
  "\p;",        // 0x29
  "\p\\",       // 0x2a
  "\p,",        // 0x2b
  "\p/",        // 0x2c
  "\pN",        // 0x2d
  "\pM",        // 0x2e
  "\p.",        // 0x2f
  "\pTab",      // 0x30
  "\pSpace",    // 0x31
  "\p`",        // 0x32
  "\pDelete",   // 0x33
  NULL,         // 0x34
  "\pEsc",      // 0x35
  NULL,         // 0x36
  "\pCmd",      // 0x37
  "\pShift",    // 0x38
  "\pCaps",     // 0x39
  "\pOption",   // 0x3a
  "\pCtrl",     // 0x3b
  NULL,         // 0x3c
  NULL,         // 0x3d
  NULL,         // 0x3e
  NULL,         // 0x3f
  NULL,         // 0x40
  "\pKp .",     // 0x41
  NULL,         // 0x42
  "\pKp *",     // 0x43
  NULL,         // 0x44
  "\pKp +",     // 0x45
  NULL,         // 0x46
  "\pClear",    // 0x47
  NULL,         // 0x48
  NULL,         // 0x49
  NULL,         // 0x4a
  "\pKp /",     // 0x4b
  "\pEnter",    // 0x4c
  NULL,         // 0x4d
  "\pKp -",     // 0x4e
  NULL,         // 0x4f
  NULL,         // 0x50
  "\pKp =",     // 0x51
  "\pKp 0",     // 0x52
  "\pKp 1",     // 0x53
  "\pKp 2",     // 0x54
  "\pKp 3",     // 0x55
  "\pKp 4",     // 0x56
  "\pKp 5",     // 0x57
  "\pKp 6",     // 0x58
  "\pKp 7",     // 0x59
  NULL,         // 0x5a
  "\pKp 8",     // 0x5b
  "\pKp 9",     // 0x5c
  NULL,         // 0x5d
  NULL,         // 0x5e
  NULL,         // 0x5f
  "\pF5",       // 0x60
  "\pF6",       // 0x61
  "\pF7",       // 0x62
  "\pF3",       // 0x63
  "\pF8",       // 0x64
  "\pF9",       // 0x65
  NULL,         // 0x66
  "\pF11",      // 0x67
  NULL,         // 0x68
  "\pF13",      // 0x69
  NULL,         // 0x6a
  "\pF14",      // 0x6b
  NULL,         // 0x6c
  "\pF10",      // 0x6d
  NULL,         // 0x6e
  "\pF12",      // 0x6f
  NULL,         // 0x70
  "\pF15",      // 0x71
  "\pHelp",     // 0x72
  "\pHome",     // 0x73
  "\pPg Up",    // 0x74
  "\pFwd Del",  // 0x75
  "\pF4",       // 0x76
  "\pEnd",      // 0x77
  "\pF2",       // 0x78
  "\pPg Dn",    // 0x79
  "\pF1",       // 0x7a
  "\pLeft",     // 0x7b
  "\pRight",    // 0x7c
  "\pDown",     // 0x7d
  "\pUp",       // 0x7e
  NULL          // 0x7f
};

static void CenterDialog(Handle dlog)
{
  Rect *bounds;
  Rect screen;
  short width, height, dh, dv;

  bounds = (Rect *) *dlog;
  screen = qd.screenBits.bounds;
  screen.top += GetMBarHeight();

  width = bounds->right - bounds->left;
  height = bounds->bottom - bounds->top;

  dh = ((screen.right - screen.left) - width) / 2 - bounds->left;
  dv = ((screen.bottom - screen.top) - height) / 4 - bounds->top + screen.top;

  OffsetRect(bounds, dh, dv);
}

// return true to hide, false to show
static pascal Boolean RomFileFilter(CInfoPBRec *pb)
{
  StringPtr name;
  unsigned char len;

  // always show 'GBRM' files
  if (pb->hFileInfo.ioFlFndrInfo.fdType == 'GBRM') {
    return false;
  }

  // show files ending in .gb or .gbc (case insensitive)
  name = pb->hFileInfo.ioNamePtr;
  len = name[0];
  if (len >= 3) {
    char c1 = name[len - 2];
    char c2 = name[len - 1];
    char c3 = name[len];
    // Check for .gb
    if (c1 == '.' && (c2 == 'g' || c2 == 'G') && (c3 == 'b' || c3 == 'B')) {
      return false;
    }
  }
  if (len >= 4) {
    char c1 = name[len - 3];
    char c2 = name[len - 2];
    char c3 = name[len - 1];
    char c4 = name[len];
    // Check for .gbc
    if (c1 == '.' && (c2 == 'g' || c2 == 'G') && (c3 == 'b' || c3 == 'B') && (c4 == 'c' || c4 == 'C')) {
      return false;
    }
  }

  return true;
}

int ShowOpenBox(void)
{
  SFReply reply;
  Point pt = { 0, 0 };
  const int stdWidth = 348;

  pt.h = qd.screenBits.bounds.right / 2 - stdWidth / 2;

  SFGetFile(pt, NULL, RomFileFilter, -1, NULL, NULL, &reply);
  
  if(reply.good) {
    return LoadRom(reply.fName, reply.vRefNum);
  }
  
  return false;
}

pascal Boolean AboutFilter(DialogPtr dlg, EventRecord *event, short *item)
{
  Point pt;
  short type;
  Handle h;
  Rect r;

  if (event->what == mouseDown) {
    pt = event->where;
    SetPort(dlg);
    GlobalToLocal(&pt);
    GetDialogItem(dlg, 2, &type, &h, &r);
    if (PtInRect(pt, &r)) {
      *item = 2;
      return true;
    }
  }
  return false;
}

void ShowAboutBox(void)
{
  DialogPtr dp;
  EventRecord e;
  DialogItemIndex itemHit;
  
  CenterDialog(GetResource('DLOG', DLOG_ABOUT));
  dp = GetNewDialog(DLOG_ABOUT, 0L, (WindowPtr) -1L);
  ShowWindow(dp);

  do {
    ModalDialog(AboutFilter, &itemHit);
    if (itemHit == 2) {
      Rect rect;
      Handle handle;
      short type;

      GetDialogItem(dp, 2, &type, &handle, &rect);
      SetDialogItem(dp, 2, type, GetIcon(132), &rect);
      DrawDialog(dp);
    }
  } while (itemHit != ok);

  DisposeDialog(dp);
}

void LoadKeyMappings(void)
{
  Handle h;
  int k;

  h = GetResource(RES_KEYS_TYPE, RES_KEYS_ID);
  if (h != nil && GetHandleSize(h) >= 8) {
    for (k = 0; k < 8; k++) {
      keyMappings[k] = ((unsigned char *)*h)[k];
    }
  } else {
    for (k = 0; k < 8; k++) {
      keyMappings[k] = defaultMappings[k];
    }
  }
}

void SaveKeyMappings(void)
{
  Handle h;
  int k;

  h = GetResource(RES_KEYS_TYPE, RES_KEYS_ID);
  if (h == nil) {
    h = NewHandle(8);
    if (h == nil) return;
    for (k = 0; k < 8; k++) {
      ((unsigned char *)*h)[k] = keyMappings[k];
    }
    AddResource(h, RES_KEYS_TYPE, RES_KEYS_ID, "\pKey Mappings");
  } else {
    for (k = 0; k < 8; k++) {
      ((unsigned char *)*h)[k] = keyMappings[k];
    }
    ChangedResource(h);
  }
  WriteResource(h);
}

void LoadPreferences(void)
{
  Handle h;
  int *prefs;
  int incompatibleDirect, incompatibleIndexed;

  h = GetResource(RES_PREFS_TYPE, RES_PREFS_ID);
  if (h != nil && GetHandleSize(h) >= sizeof(int) * 9) {
    prefs = (int *) *h;
    cycles_per_exit = prefs[0];
    frame_skip = prefs[1];
    video_mode = prefs[2];
    screen_scale = prefs[3];
    sound_enabled = prefs[4];
    limit_fps = prefs[5];
    current_palette = prefs[6];
    if (current_palette < 0 || current_palette >= gb_palette_count) {
      current_palette = 0;
    }
    gbc_enabled = prefs[7];
    ignore_double_speed = prefs[8];
  } else {
    cycles_per_exit = cyclesValues[0];
    frame_skip = 4;
    sound_enabled = 0;
    limit_fps = 0;
    current_palette = 0;
    gbc_enabled = 1;
    ignore_double_speed = 0;
    if (screen_depth >= 8) {
      video_mode = VIDEO_INDEXED;
      screen_scale = 1;
    } else {
      video_mode = VIDEO_DITHER_COPYBITS;
      screen_scale = 2;
    }
  }

  // validate video_mode for current screen depth
  incompatibleDirect =
    video_mode == VIDEO_DITHER_DIRECT && screen_depth > 1;
  incompatibleIndexed =
    video_mode == VIDEO_INDEXED && screen_depth == 1;
  if (incompatibleDirect || incompatibleIndexed) {
    video_mode = VIDEO_DITHER_COPYBITS;
  }
}

void SavePreferences(void)
{
  Handle h;
  int *prefs;
  Size needed = sizeof(int) * 9;

  h = GetResource(RES_PREFS_TYPE, RES_PREFS_ID);
  if (h == nil) {
    h = NewHandle(needed);
    if (h == nil) {
      return;
    }
    AddResource(h, RES_PREFS_TYPE, RES_PREFS_ID, "\pPreferences");
  } else if (GetHandleSize(h) < needed) {
    SetHandleSize(h, needed);
  }

  prefs = (int *) *h;
  prefs[0] = cycles_per_exit;
  prefs[1] = frame_skip;
  prefs[2] = video_mode;
  prefs[3] = screen_scale;
  prefs[4] = sound_enabled;
  prefs[5] = limit_fps;
  prefs[6] = current_palette;
  prefs[7] = gbc_enabled;
  prefs[8] = ignore_double_speed;
  ChangedResource(h);
  WriteResource(h);
}

pascal Boolean KeyMapFilter(DialogPtr dlg, EventRecord *event, short *item)
{
  Point pt;
  Rect r;
  Handle h;
  short type;
  int k;

  if (event->what == mouseDown) {
    pt = event->where;
    SetPort(dlg);
    GlobalToLocal(&pt);

    for (k = 3; k <= 10; k++) {
      GetDialogItem(dlg, k, &type, &h, &r);
      if (PtInRect(pt, &r)) {
        gSelectedSlot = k;
        *item = k;
        return true;
      }
    }
  }
  else if (event->what == keyDown && gSelectedSlot != -1) {
    int keyNum = (event->message & keyCodeMask) >> 8;
    if (keyNum < 128 && keyNames[keyNum]) {
      keyMappings[gSelectedSlot - 3] = keyNum;
      *item = gSelectedSlot;
      gSelectedSlot = -1;
      return true;
    }
  }
  return false;
}

pascal void DrawKeySlot(DialogPtr dlg, short item)
{
  Rect r;
  Handle h;
  short type;
  FontInfo fi;
  short oldFont, oldSize, oldMode;

  SetPort(dlg);
  GetDialogItem(dlg, item, &type, &h, &r);

  oldFont = dlg->txFont;
  oldSize = dlg->txSize;
  oldMode = dlg->txMode;

  TextFont(3);
  TextSize(9);
  TextMode(srcXor);
  GetFontInfo(&fi);

  if (gSelectedSlot == item) {
    PaintRect(&r);

    MoveTo(r.left + 3, r.top + fi.ascent + 1);
    DrawString("\p<press>");
  } else {
    EraseRect(&r);
    FrameRect(&r);

    MoveTo(r.left + 3, r.top + fi.ascent + 1);
    DrawString(keyNames[keyMappings[item - 3]]);
  }

  TextFont(oldFont);
  TextSize(oldSize);
  TextMode(oldMode);
}

pascal void FrameSaveButton(DialogPtr dlg, short item)
{
  Rect rect;
  Handle handle;
  short type;

  GetDialogItem(dlg, 1, &type, &handle, &rect);
  PenSize(3, 3);
  InsetRect(&rect, -4, -4);
  FrameRoundRect(&rect, 16, 16);
  PenNormal();
}

void ShowKeyMappingsDialog(void)
{
  DialogPtr dp;
  EventRecord e;
  DialogItemIndex itemHit;
  int k;

  Rect rect;
  Handle handle;
  short type;

  // this can be left over from previous invocations if the user closed
  // the dialog box while a key mapping control is active
  gSelectedSlot = -1;

  CenterDialog(GetResource('DLOG', DLOG_KEY_MAPPINGS));

  dp = GetNewDialog(DLOG_KEY_MAPPINGS, 0L, (WindowPtr) -1L);
  GetDialogItem(dp, 19, &type, &handle, &rect);
  SetDialogItem(dp, 19, type, (Handle) FrameSaveButton, &rect);

  for (k = 3; k <= 10; k++) {
    GetDialogItem(dp, k, &type, &handle, &rect);
    SetDialogItem(dp, k, type, (Handle) DrawKeySlot, &rect);
  }
  ShowWindow(dp);

  do {
    ModalDialog(KeyMapFilter, &itemHit);

    if (itemHit >= 3 && itemHit <= 10) {
      for (k = 3; k <= 10; k++) {
        DrawKeySlot(dp, k);
      }
    }
  } while (itemHit != ok && itemHit != cancel);

  if (itemHit == ok) {
    SaveKeyMappings();
  }

  DisposeDialog(dp);
}

static void SetRadioGroup(DialogPtr dp, int first, int last, int selected)
{
  int k;
  Rect rect;
  Handle handle;
  short type;

  for (k = first; k <= last; k++) {
    GetDialogItem(dp, k, &type, &handle, &rect);
    SetControlValue((ControlHandle) handle, k == selected ? 1 : 0);
  }
}

void ShowPreferencesDialog(void)
{
  DialogPtr dp;
  DialogItemIndex itemHit;
  int cyclesItem, videoModeItem, frameSkipItem;
  int k;

  Rect rect;
  Handle handle;
  short type;

  // map current settings to dialog items
  cyclesItem = 3;
  for (k = 0; k < 2; k++) {
    if (cycles_per_exit == cyclesValues[k]) {
      cyclesItem = 3 + k;
      break;
    }
  }
  videoModeItem = 5 + video_mode;
  frameSkipItem = 8 + frame_skip;

  CenterDialog(GetResource('DLOG', DLOG_PREFERENCES));

  dp = GetNewDialog(DLOG_PREFERENCES, 0L, (WindowPtr) -1L);
  GetDialogItem(dp, 18, &type, &handle, &rect);
  SetDialogItem(dp, 18, type, (Handle) FrameSaveButton, &rect);

  // set initial radio button states
  SetRadioGroup(dp, 3, 4, cyclesItem);
  SetRadioGroup(dp, 5, 7, videoModeItem);
  SetRadioGroup(dp, 8, 12, frameSkipItem);

  // disable incompatible video modes
  if (screen_depth > 1) {
    GetDialogItem(dp, 6, &type, &handle, &rect);
    HiliteControl((ControlHandle) handle, 255);
  }
  if (screen_depth == 1) {
    GetDialogItem(dp, 7, &type, &handle, &rect);
    HiliteControl((ControlHandle) handle, 255);
  }

  ShowWindow(dp);

  do {
    ModalDialog(NULL, &itemHit);

    if (itemHit >= 3 && itemHit <= 4) {
      cyclesItem = itemHit;
      SetRadioGroup(dp, 3, 4, cyclesItem);
    } else if (itemHit >= 5 && itemHit <= 7) {
      videoModeItem = itemHit;
      SetRadioGroup(dp, 5, 7, videoModeItem);
    } else if (itemHit >= 8 && itemHit <= 12) {
      frameSkipItem = itemHit;
      SetRadioGroup(dp, 8, 12, frameSkipItem);
    }
  } while (itemHit != ok && itemHit != cancel);

  if (itemHit == ok) {
    cycles_per_exit = cyclesValues[cyclesItem - 3];
    video_mode = videoModeItem - 5;
    frame_skip = frameSkipItem - 8;
    SavePreferences();
  }

  DisposeDialog(dp);
}

static short AlertWrapper(short alertID) { return Alert(alertID, NULL); }
static short CautionAlertWrapper(short alertID) { return CautionAlert(alertID, NULL); }
static short NoteAlertWrapper(short alertID) { return NoteAlert(alertID, NULL); }
static short StopAlertWrapper(short alertID) { return StopAlert(alertID, NULL); }

short ShowCenteredAlert(
    short alertID,
    const char *s0,
    const char *s1,
    const char *s2,
    const char *s3,
    int alertType
) {
  Handle alrt;
  Rect *bounds;
  Rect screen;
  short width, height, dh, dv;
  AlertProc funcs[] = { AlertWrapper, NoteAlertWrapper, CautionAlertWrapper, StopAlertWrapper };
  
  ParamText(s0, s1, s2, s3);

  alrt = GetResource('ALRT', alertID);
  if (alrt == nil) {
    return funcs[alertType](alertID);
  }

  bounds = (Rect *) *alrt;
  screen = qd.screenBits.bounds;
  screen.top += GetMBarHeight();

  width = bounds->right - bounds->left;
  height = bounds->bottom - bounds->top;

  // center horizontally, position 1/4 down vertically
  dh = ((screen.right - screen.left) - width) / 2 - bounds->left;
  dv = ((screen.bottom - screen.top) - height) / 4 - bounds->top + screen.top;

  OffsetRect(bounds, dh, dv);

  return funcs[alertType](alertID);
}

int SaveScreenshot(void)
{
  SFReply reply;
  Point pt = { 0, 0 };
  short refNum;
  long count;
  PicHandle pic;
  Rect picFrame;
  RgnHandle oldClip;
  char header[512];
  int k;
  OSErr err;
  const int stdWidth = 348;

  pt.h = qd.screenBits.bounds.right / 2 - stdWidth / 2;

  SFPutFile(pt, "\pSave screenshot as:", "\pScreenshot", NULL, &reply);
  if (!reply.good) {
    return 0;
  }

  /* delete existing file if any, then create new one */
  FSDelete(reply.fName, reply.vRefNum);
  err = Create(reply.fName, reply.vRefNum, 'ttxt', 'PICT');
  if (err != noErr) {
    return 0;
  }

  err = FSOpen(reply.fName, reply.vRefNum, &refNum);
  if (err != noErr) {
    return 0;
  }

  /* write 512-byte header (all zeroes) */
  for (k = 0; k < 512; k++) {
    header[k] = 0;
  }
  count = 512;
  FSWrite(refNum, &count, header);

  /* create picture by drawing offscreen bitmap */
  SetPort(g_wp);
  picFrame = offscreen_rect;

  /* save and set clip to picture bounds */
  oldClip = NewRgn();
  GetClip(oldClip);
  ClipRect(&picFrame);

  pic = OpenPicture(&picFrame);
  if (screen_depth > 1) {
    CGrafPtr port = (CGrafPtr) g_wp;
    CopyBits(
        (BitMap *) &offscreen_pixmap,
        (BitMap *) *port->portPixMap,
        &offscreen_rect, &offscreen_rect, srcCopy, NULL
    );
  } else {
    CopyBits(&offscreen_bmp, &g_wp->portBits, &offscreen_rect, &offscreen_rect, srcCopy, NULL);
  }
  ClosePicture();

  SetClip(oldClip);
  DisposeRgn(oldClip);

  /* write picture data */
  HLock((Handle) pic);
  count = GetHandleSize((Handle) pic);
  FSWrite(refNum, &count, *pic);
  HUnlock((Handle) pic);

  KillPicture(pic);
  FSClose(refNum);

  return 1;
}
