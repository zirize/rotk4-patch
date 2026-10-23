# SAN4W95.EXE internals

PE32 GUI, image base 0x400000, sections `.text` 0x401000 (file 0x400), `.data` 0x528000
(file 0x9c000), `.idata` 0x545000 (file 0xb9000). File offset of a `.text` VA:
`va - 0x401000 + 0x400`. Imports only WINMM, KERNEL32, USER32, GDI32, DDRAW
(`DirectDrawCreate` only); about 130 functions. `SAN4SND.S4` is a PE DLL loaded with
`LoadLibraryA`.

## Startup checks (WinMain around 0x401500)

| Where | Check | Failure |
|---|---|---|
| 0x401542 | `GetSystemMetrics(SM_MOUSEPRESENT)` | "마우스가 인스톨되어 있지 않습니다" |
| 0x401558 → 0x401473 | at least one `GetDriveTypeA == DRIVE_CDROM` over A:–Z: | "CD-ROM 드라이브가 없는 곳에서는…" |
| 0x40157a | `GetDeviceCaps(RASTERCAPS) & RC_PALETTE` and `SIZEPALETTE == 256`, **before** DirectDraw exists | "[색상표]를 256색으로…" (string 0x528060) |
| 0x4015c1 | screen at least 640×400 | — |

cnc-ddraw `fake_mode=640x480x8` makes the GetDeviceCaps check pass, but only if cnc-ddraw is
loaded before WinMain (the shim loads it in `DllMain`).

## DirectDraw setup (0x4931ef …)

- `SetCooperativeLevel(hwnd, 0x15)` = FULLSCREEN | NOWINDOWCHANGES | EXCLUSIVE. The game
  **never calls `SetDisplayMode`**: it assumes the desktop already is 8 bpp. On a 32 bpp
  desktop `SetPalette` on the primary fails with 0x88760091 (DDERR_INVALIDPIXELFORMAT) and the
  game exits with code 1 and no message. Shim fix: `SetDisplayMode(<game size>, 8)` after an
  exclusive `SetCooperativeLevel`.
- `CreatePalette` (vtbl+0x14) then 0x49334b applies it; 0x49334b returns E_FAIL unless the
  state flags allow it.
- After the opening the game releases DirectDraw and calls `DirectDrawCreate` again (0x492e43
  onwards: SetCooperativeLevel, primary 0x543e64, back 0x543e68, clipper 0x543e70, work
  surface 0x543e6c). cnc-ddraw starts every new object in the desktop mode, so the shim sets
  the 8 bpp mode again for each object. If any step fails, 0x49324a clears the pointers but the
  caller (0x49145b → 0x4934ab) still Locks 0x543e6c → NULL read at 0x4934dc.

## Window size and drawing origin

- Size: WinMain calls 0x490c8e(hwnd, 640, h) with h = min(480, usable & ~15), usable =
  `SM_CYSCREEN - SM_CYCAPTION - SM_CYMENU` (0x490e3d). Width is fixed at 640; variables
  0x520bb0 (width) / 0x5229dc (height). With
  cnc-ddraw's 640x480 fake screen and Wine's 67 px caption+menu this gave 640x400 while
  cnc-ddraw scales the mouse for 640x480. Shim fix: `GetSystemMetrics(SM_CYSCREEN)` reports at
  least 592 + caption + menu (IAT hook, re-applied with the MCI re-hook; see the prompt below).
- Screen size prompt (after the opening, 0x458c8b): if 0x490e3d's width (`SM_CXSCREEN`) is at
  least 800 (0x4583c0), 0x458ad7 shows a menu "화면 크기를 결정해 주십시오." with "640×480" /
  "800×600" (strings 0x5438f8 / 0x543908 via table 0x5339a0, title 0x543918). 800×600 gives
  width 800 and height min(592, usable & ~15); 640×480 gives 640 and min(480, …). Otherwise
  0x458c2f takes 640×480 silently. Then 0x490cd0(w, h) resizes and 0x490d0d(w, h, 1) sets
  up DirectDraw again (the second `DirectDrawCreate`). The 86Box install (1024x768 desktop)
  runs the same EXE (identical MD5) and shows the prompt; under cnc-ddraw `fake_mode=640x480x8`
  the screen is 640 wide, so the prompt would never appear. Shim fix: `GetSystemMetrics`
  reports at least 800 x (592 + caption + menu), and each DirectDraw object gets an 8 bpp mode
  of the game's own size read from 0x520bb0 / 0x5229dc (640x480, or 800x592 — the same client
  size as on Win98, so no black strip and exact mouse mapping); movies fill that size.
  0x490cd0 stores w/h in 0x520bb0 / 0x5229dc first and only then (if a size was set
  before) releases (0x490f77) and recreates DirectDraw (0x490e6a → 0x492de7), which sizes
  its surfaces from those globals (0x492e9e, 0x492f7e); so at the second
  `SetCooperativeLevel` the chosen size can be read from them.
- Origin: the game blits to the primary at the client area's screen position (OffsetRect by
  0x520cb8 / 0x520cbc at 0x493453 and 0x493913), stored from WM_MOVE lParam in the app
  wndproc (0x492999, skipped when 0x543e94 bit 0x8 is set). cnc-ddraw maps the primary to the
  client area and swallows WM_MOVE, so the value from window creation (below caption and
  menu, y = 67) stuck: picture 67 lines down, bottom cut off, clicks off by as much. Shim fix:
  send WM_MOVE(0, 0) before each `SetCooperativeLevel`, while the game's own wndproc still
  receives it.
- Mouse: GetCursorPos + ScreenToClient (0x40137f, 0x49248a, 0x4927ae), clamped to
  0x520bb0 / 0x5229dc; cnc-ddraw's fakes return game coordinates (640x480 or 800x592).
- Menu bar (resource APPMENU): "종료(X)" (40001) and popup "도움말(H)" with "버전정보(A)"
  (40005). Shim fix (`scale.c`): a popup "화면(V)" with x1/x2/x3 (IDs 0x7e51–0x7e53) is
  inserted first (the game only touches the system menu's SC_CLOSE, never bar positions);
  the window client becomes the game size times that integer (largest that fits the work
  area if the choice does not), with `resizable=false` in `[SAN4W95]`. cnc-ddraw
  takes a new window size only from a user drag, so the shim sends WM_ENTERSIZEMOVE,
  WM_SIZING (new window rect), WM_EXITSIZEMOVE; it re-applies after each exclusive
  `SetCooperativeLevel`, since 800x600 makes a new DirectDraw object. cnc-ddraw patches the
  imports of every module in the game folder, the shim's included (GetProcAddress too): its
  GetWindowRect returns a 0,0-based game-size rect, so the shim reads GetWindowInfo.
- Mouse capture: in windowed mode cnc-ddraw confines and hides the cursor after a click in
  the window (ClipCursor); Ctrl+Tab or Right Alt+Right Ctrl releases it (`keyunlockcursor1/2`),
  leaving the window releases it too. `devmode=true` never locks: the cursor is free and the
  game still gets scaled coordinates (fake GetCursorPos maps whenever devmode is set; clamped
  to the edge when the pointer is outside). Shim fix (`scale.c`): "화면" menu item "마우스
  가두기 - 다음 실행부터" (ID 0x7e5f) writes `devmode` in `[SAN4W95]`; it cannot act at once,
  because cnc-ddraw reads devmode only at load into an unexported global and its window
  procedure (in front of the shim's) drops mouse input while unlocked unless devmode is set.

## State flags dword 0x543e94

| Bit | Meaning | Set / cleared |
|---|---|---|
| 0x1 | app active | set by WM_ACTIVATEAPP(TRUE) handler (0x492b5c); also from `GetActiveWindow()` at 0x490f37 |
| 0x2 | minimised / suspended | set with 0x4 by the WM_DISPLAYCHANGE handler (0x492c1e) when the new size differs from the size recorded at startup (0x5224e0 / 0x520c94), which also minimises the window |
| 0x10000 | — | 0x491329 |

Win95 DirectDraw showed and activated the (hidden) window on exclusive mode; wrappers do not,
so bit 0 stays clear and palette setup fails. Shim fix: `ShowWindow` + `SetForegroundWindow`
after `SetCooperativeLevel`. Our own `SetDisplayMode` sends WM_DISPLAYCHANGE, which would set
bit 1; the shim subclasses the window to swallow WM_DISPLAYCHANGE during that call. The
activation re-enters `SetCooperativeLevel` from the game's WM_ACTIVATEAPP handler (0x492bd4 →
0x493318), so the shim guards against recursion.

## CD identification and music (MCI wrappers 0x434575–0x434a6c)

- Device ID variable 0x52b9d4 (-1 when closed). Commands used on `cdaudio`: OPEN (type,
  shareable), CLOSE, STATUS NUMBER_OF_TRACKS / MODE (compared with MCI_MODE_PLAY 0x20e) /
  MEDIA_PRESENT, SET TIME FORMAT MSF then STATUS LENGTH+TRACK, SET TMSF then
  PLAY FROM|TO|NOTIFY (callback = main window 0x49a004; MM_MCINOTIFY 0x3b9 handled in the
  app wndproc 0x40128b), STOP.
- CD check 0x4655be → 0x465543: for tracks 2..12, `m*60+s` of the track length must lie in
  `[min, max]` from tables 0x5374e0 / 0x537510. Otherwise the retry box with string 0x5376ac
  ("…CD-ROM을 넣어 주십시오") loops. The game CD image (`Audio.cue`, 12 tracks) passes all 11.
- Scene music: 0x4657bb(scene) skips if the same scene is still playing (0x465905), stops
  the current track (0x46588d → STOP/CLOSE 0x43479c), then plays only if option flag
  0x536bd8 & 0x2 (BGM on) is set; track table at 0x537540 (10 bytes per scene). The title
  music does not check that flag. 0x536bd8 is game state (options dialog 0x4091d0 shows it as
  bytes 0x528d58..).
- Other MCI devices: `waveaudio` (OPEN TYPE|ELEMENT at 0x434a03, sound effects) and
  `avivideo` (movies, below). These go to the real winmm.
- cnc-ddraw hooks `mciSendCommandA` in the EXE IAT during DirectDraw setup, and again
  (`hook_init`) whenever an MCI call newly loads msvfw32 — the first avivideo open. Each
  time it overwrites earlier hooks, so the shim re-applies its hook after `DirectDrawCreate`,
  `SetCooperativeLevel` and **after every MCI call**, chaining to whatever it replaced.
  Without the per-call re-hook every MCI call after the first movie bypasses the shim.

## Movies (avivideo, 0x495138 and caller 0x492685)

- AVIs are Cinepak 24 bpp 320x240 at 10–15 fps (letterboxed content, ~186 rows),
  PCM audio. `iccvid` must be present (Wine builtin; Windows ships it).
- 0x4950cf: OPEN TYPE "avivideo" only, as an availability check; closed via
  `mciGetDeviceIDA("avivideo")` at 0x495110.
- 0x495138: OPEN ELEMENT|DGV_OPEN_PARENT|DGV_OPEN_WS (style WS_CHILD, parent = main
  window), WINDOW STATE SW_SHOW, STATUS DGV_STATUS_HWND → 0x520c90, then either the
  caller's rect or WHERE SOURCE, optionally centred, and `MoveWindow(hwnd, x, y, w, h)`.
  Then PLAY (0x495298); the loop polls keys/mouse to skip; CLOSE at 0x49526e.
- The caller passes a rect from 0x490e3d: `SM_CXSCREEN` × (`SM_CYSCREEN` − `SM_CYCAPTION`
  − `SM_CYMENU`), centred. On the 1024x768 reference desktop that is the full 640x480
  client (picture 2x); with the screen faked to 640x480 it becomes 578x434 at (31,-1).
- Palette: before a movie the game only colour-fills the primary black (0x493411); it never
  sets a movie palette. After CLOSE it re-enters exclusive mode (0x493318) and re-applies
  its palette (0x49334b). On Win95 MCIAVI's realised palette replaced the hardware palette
  in between.
- Under cnc-ddraw (`fixchilds`), GDI blits into windows of class MCIAVI are redirected into
  the 8 bpp primary's DC, so frames are mapped by nearest colour (no dither) to the
  current DirectDraw palette.
- Shim fix (`movie.c`, `palette.c`): on PLAY of the AVI device, `MoveWindow` the movie window
  to 0,0 at the game size (640x480 or 800x592) and `MCI_PUT DESTINATION` the same (Wine's MCIAVI does not stretch to the
  window by itself); load a 254-colour median-cut palette built from 48 frames of that AVI
  (AVIFile, 24 bpp) into the game's palette (captured from `CreatePalette`); restore the
  saved entries on CLOSE. Fixed palettes (6x6x6 cube, 3-3-2) band badly on the sepia movies.
- Without cnc-ddraw in the path, Windows 11 MCIAVI may not stretch: under otvdm it accepted
  `MCI_PUT DESTINATION` for its own default popup (read back by `MCI_WHERE`) yet drew the
  320x240 frame at the top-left; giving it a window of one's own via `MCI_WINDOW` +
  `MCI_DGV_WINDOW_HWND`, then sizing it and PUT DESTINATION, worked (measured by rotk5,
  2026-10-23: `../rotk5/docs/sam5-internals.md`, "Opening movie"). rotk4 is unaffected: its
  movie window is a child that cnc-ddraw redraws into the primary, and it fills the scaled
  window on Windows 11.

## Fonts

The only `CreateFontA` (0x493695) asks for "ＭＳ ゴシック" (MS Gothic, cp932 bytes) with
SHIFTJIS_CHARSET, height -16 — a Japanese leftover. Window title, menus and message boxes are
cp949 and need the Korean locale and a Korean font in the Wine prefix (see the README).
