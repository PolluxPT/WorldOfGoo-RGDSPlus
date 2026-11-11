# World of Goo for Anbernic RG DS Plus

Standalone v1.1.0 for stock RG DS Plus Linux. The original Linux x86-64 game
runs through Box64 with a native ARM64 SDL/GLES bridge. PortMaster is unnecessary.
This package contains the port and its runtime. Supply your own game files.

## Install

1. Install or extract the GOG **Linux x86-64** edition of World of Goo on a computer.
2. Find the installed directory that directly contains `WorldOfGoo.bin.x86_64`,
   `lib64`, and a game data folder named `game`. Copy the contents of that
   installed directory into `APPS/WorldOfGoo_RGDSPlus/game/` in this kit.
   The resulting layout is:

   ```text
   APPS/WorldOfGoo_RGDSPlus/game/WorldOfGoo.bin.x86_64
   APPS/WorldOfGoo_RGDSPlus/game/lib64/
   APPS/WorldOfGoo_RGDSPlus/game/game/
   ```

3. Copy `WorldOfGoo.sh` and `WorldOfGoo_RGDSPlus` from `APPS` into
   `Roms/APPS` on the games card. Merge the existing folder when updating
   to preserve `savedata`.
4. Open WorldOfGoo from the handheld's APPS menu.

This release verifies one GOG Linux build before creating a patched local
copy. The supported executable SHA-256 starts with `9f8e6a907f6cc2ea`.
Other GOG builds, Windows releases, and Humble builds need separate verification.
If launch fails, `WorldOfGoo_RGDSPlus/logs/Launcher.txt` identifies missing or
mismatched files.

## Controls

| Button | Action |
| --- | --- |
| D-pad or left stick | Move pointer |
| A | Click, or hold to drag |
| L1 | Slow pointer movement |
| Start | Pause or resume |
| Y | Open or close the name keyboard |
| Start + Select (hold) | Exit |
| Volume + / − | Adjust volume |

Select the name field with A, then press Y. In the keyboard, D-pad selects a
letter, A types it, B deletes, and Start submits.

## Display and audio

The game presents on the top 1024 × 768 panel. The bottom backlight turns off
once the top window receives a frame. Small textures retain more detail.
Frame notifications wake the top presenter directly. Focus changes from the
presenter are filtered so the game's own focus handler cannot pause audio.
Volume uses the same quadratic gain curve as Yokoi.

Source and build notes are in `Source/`. See `THIRD_PARTY_SOURCES.md` and
`THIRD_PARTY_NOTICES.md` inside the app folder for component details.
