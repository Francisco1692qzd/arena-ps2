# TEAM BATTLE ARENA 3D — PS2 Full Port

**Web source:** `../index.html` — `Three.js` 4-min 4v4 FPS.  
**This port:** `main.c` mirrors web logic in `ps2sdk/gsKit/libpad` for real EE hardware (640×448 @ 60i, `PSM_CT16` 16-bit, 64×64 TIM2, 32MB budget).

## What's ported (1:1 with web)

* **3 real maps** `MAPS[3]` (`index.html:347`): `NEON 80×80` 9 covers, `DESERT 90×90` 9 covers, `INDUSTRIAL 70×70` 12 covers + `baseA/baseB` + `BarrierStats {w,d,h,hp,life,supply}`. `buildMap(idx)` clears/rebuilds `coverBoxes[]`.
* **Oriented barriers (fair)** `pointInOrientedBarrier()` / `rayBoxDist()` / `lineIntersectsBox()` rotate by `-yaw` — `hasLOS()` / `collidesWorld()` / `shootRay()` use same test for player and bots (no shooting through `Q/△` walls).
* **Guns/Knives shop** `GUNS[4]` `KNIVES[3]` (`index.html:346`): `P-9 34/7 FREE`, `CARBINE 42/5 $450`, `RAIDER SG 66/14 3p $850`, `RAIL 78/18 $1300` ; `COMBAT 80/2.6 $350`, `TANTO 110/2.9 $750`. `RAY` pellets + spread, `knifeStab()` cone `0.52` + `hasLOS`, both respect barriers.
* **Distinct bot names** `BOT_POOL 30` shuffled `pickBotNames()` 7, never `store.playerName`. Player name `store.playerName[13]` upper, persistent.
* **Economy** `store.cash` `+120` gun kill `+160` knife `+100` nade `+400` round win `+100` draw. Saved to `mc0:TBASAVE.DAT` (falls back `host:TBASAVE.DAT` for PCSX2 via `ps2client`). `mc` save is stub — in-memory persists per boot if no MC.
* **DualShock2** `libpad` `pollPad()`: `L-Stick` move dead `20`, `R-Stick` look `0.0009*dt`, `R1/R2/Square` shoot, `L1` grenade, `△` barrier, `R3` knife, `✕` jump, `L2` sprint, `SELECT` armory, `START` pause.

## Build

Requires `ps2dev` toolchain `https://github.com/ps2dev/ps2dev`.

**WSL Ubuntu (recommended):**
```bash
wsl --install
# in Ubuntu:
sudo apt update && sudo apt install git make patch
bash -c "$(curl -fsSL https://raw.githubusercontent.com/ps2dev/ps2dev/master/tools/install.sh)"
export PS2SDK=/usr/local/ps2dev/ps2sdk
export GSKIT=$PS2SDK/ports
cd /mnt/c/Users/Francisco/Documents/Default\ Project/team-battle-arena-3d/ps2-port
make          # -> team-battle-arena.elf
make iso      # -> arena.iso (TBAARENA.ELF + SYSTEM.CNF)
```

**Docker alternative:**
```bash
docker run -it --rm -v "C:/Users/Francisco/Documents/Default Project:/work" ps2dev/ps2dev bash
cd /work/team-battle-arena-3d/ps2-port && make && make iso
```

## Test & Deploy

* **PCSX2:** `CDVD -> ISO Selector -> Browse -> arena.iso` `System -> Boot ISO (fast)`. Check `640×448` `MENU` `[] {}` changes map, `SELECT` armory `X` buy/equip, `R3` knife, barriers block both sides.
* **Real PS2:** `FreeMcBoot/FreeDVDBoot` + `OPL` `USB: arena.iso -> USB:/DVD/SLUS_000.01` or burn `arena.iso` to `DVD-R` 4x `IMGBURN`. `host:` saves work in emulator; on hardware saves to `mc0:` if card present.

## EE Budget

* 16-bit `GS_PSM_CT16` + `GS_PSMZ_16`, `DoubleBuffering ON`, `dmaKit` GIF `RCYC_8`, `1/32` vertex quantize, 64×64 TIM2, max `7 bots/12 barriers/6 grenades/18 tracers` — fits 4MB VRAM / 32MB RAM.

## Next

* Add TIM2 textures via `bin2o` / `tex2tim2` in `Makefile`, stream from `cdrom`.
* Replace `scr_printf` HUD with `gsKit_font` quads for retail look.
