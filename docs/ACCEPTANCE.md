# Hardware acceptance still required

The real f7 FAP builds against Momentum mntm-012 (API 87.1). Host tests execute the actual C game and renderer. They do not establish physical Flipper success.

1. Copy the release's complete `sdcard/` contents using qFlipper; check the optional Windows helper exists at `/ext/apps_data/celeste_classic/Celeste-Windows.exe`.
2. Check arrows, diagonal dash, OK jump, Back dash, and hold Back pause. A long Back hold dashes first because dash is immediate.
3. Set Momentum sound volume, including zero. Music and SFX must follow. Check whether the simplified single-speaker mix remains useful during play.
4. Collect berries/change rooms/die, then save and relaunch: current room entrance, berry flags, death count and double-dash must restore. Save after the summit, reload and touch flag: no extra completion or XP.
5. Restart: lifetime totals remain; run berries/deaths reset. Test full SD, missing SD and interrupted writes: visible error, previous valid slot retained, successful retry on exit.
6. Check 1 XP per saved berry, 10 XP per saved summit, no replay after reload. Momentum test-deed API also resets bad mood and awards stop pending a level-up.
7. Windows unlocked desktop, US keyboard: select Open on computer, then Windows. Verify short Run command, script in PowerShell, hash-checked SD transfer, temporary helper, browser opening, keys, frame rate and progress remaining on Flipper.
8. Close session: helper exits, temporary directory removed, SD original remains. Abrupt termination cleanup is best effort. Wrong layout/blocked PowerShell must never be reported as success.
9. Close/disconnect/reconnect USB and exit the FAP: no stuck movement; original USB interface restored. Locked USB ownership is refused. Host EXE and FAP expected hashes must match.
10. Explicitly click Install separate game on Windows: checksum-verified native ccleste installs under LocalAppData, appears in Start menu, runs without Flipper, and saves independently using its upstream controls. Do not overwrite unrelated installs.
11. Manual Mac/Linux helpers must discover the game serial port after the matching Flipper menu selection. OS permissions remain in effect.

Full firmware emulation was not performed. The local emulator route required patched Unicorn/fapemu. `docs/renderer-contact-sheet.png` and `docs/gameplay-host.gif` are actual host C-renderer captures.
