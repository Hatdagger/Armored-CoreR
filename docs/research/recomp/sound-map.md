# Sound and music

Every sound the game makes, where it sits on the disc, how it is loaded and what plays it. Static read of the
USA disc 1 (SLUS-01030) on 2026-11-19. Nothing here is tested in game yet; `?` marks guesses. Routine
addresses are the same on this fresh recomp (same disc); file names like `moa_ui_sound.c` refer to the old
AC1Recomp fork.

## 1. What is on the disc

| Source | What | Format | Loaded / played by |
|---|---|---|---|
| `GG/MS/MENU_VAB.T` 0 + 1 | **menu sound effects** (1 program, 13 tones, 12 samples) | VAB | every menu start, `0x8005803C` |
| `GG/MS/MENU_VAB.T` 2 + 3 | instruments of the menu song (4 programs, 13 tones) | VAB | song task `0x80064F24(2)` → `0x80058700(2)` |
| `GG/MS/MENU_VAB.T` 4 | **the menu song** (one SEQ, 480 ticks/beat, 126 BPM, 4/4) | SEQ | same task: SsSeqOpen / SsSeqPlay endless |
| `GG/COM/FDAT.T` 205, blocks 0 + 1 | **shared fight bank** (4 programs, 51 tones, 38 samples) = bank slot 0 | VAB | every fight engine at load (VS `0x800559C4`) |
| `FDAT.T` 205, block 2 | **sound-id table** (256 ids × 10 bytes, after 448 other bytes) | data | copied to `0x80044598`; ids at `0x80044758` |
| `FDAT.T` 205, blocks 3 / 4 / 5 | the 250 messages, one block per COM voice type | text | carry voice and music codes (§7, §8) |
| `FDAT.T` 5s + 4, block 5 | **the map's banks**: slot 1 (stage) + enemy banks (slots 3–60) | bank list | every engine at map load (VS `0x80055B64` → `0x800528DC`) |
| `FDAT.T` 5s + 4, blocks 13–18 | three VABs, one per COM voice type → slot 2 | VAB × 3 | missions engine `0x80055A08`; **nothing plays slot 2** |
| `GG/BGM/BGM00–03.XA` | **music**: 8 stereo songs per file, 32 in all | XA | fight engines and missions (§7) |
| `GG/BGM/TALK00–06.XA` | **mission radio voices**: 16 clips per file, 112 | XA mono 37.8 kHz | message code `X` |
| `GG/BGM/CPU00–03, 10–13, 20–23.XA` | **AC computer (COM) voice**, 3 types × 64 clips | XA mono 37.8 kHz | message code `V` |
| `GG/STR/*.STR` | movie sound (ACOPA, ACEND, DEMOPLAY, STAFF/STAFF2) | STR | not read |

The exe and the engines (FDAT 206–211) hold no sound data of their own.

## 2. How the fight engines play a sound

Every engine (missions 207, link 208, VS 209, arena 210, replay 211) has its own copy of the same code, same RAM:

| RAM | What |
|---|---|
| `0x800441D8` / `0x800441EC` | listener (camera eyes, written by `0x8005301C` in VS) |
| `0x80044218` + 8 × slot | **bank slots**: s16 SPU VAB id (−1 none, 254 skip) + header pointer |
| `0x80044428` | 22 voice records × 16 bytes (+0 handle, bit 15 keyed; +2 SPU voice; +3 louder side; +4/+5 note, fine; +6 id; +8 weight × volume) |
| `0x80044598` | FDAT 205 block 2 (3,008 bytes) |
| `0x80044758` + 10 × id | **sound-id record**: s16 bank slot, program, tone, note (60), u16 weight |
| `0x80045178` | one bit per id: already played this frame `?` |

**Play routine** (id, handle, vol L, vol R, pitch offset): VS `0x80053284`, arena `0x8005376C`, missions
`0x80053320`, link `0x80053498`, replay `0x800531EC`. id 255 = none. Picks a voice (`0x80053154` VS), keys it
via `0x80022C74` (or repitches a handle, `0x80022344`). Wrappers (VS): `0x80052B18`, `0x80052D9C` (3D),
`0x80052E70`, `0x800530AC`, `0x800530D8`, `0x80053130`, `0x80053DD0` (list). Arena: `0x80052F2C`, `0x80053190`,
`0x80053380`, `0x8005357C`, `0x800535A8`, `0x80053618`, `0x800542B8`.

**Bank open** `0x80052834(slot, header, body)` (VS; arena `0x80052D50`, missions `0x80052948`). **Bank list**
`0x800528DC` (arena `0x80052DF8`, missions `0x800529F0`): u32 count, size-prefixed headers, then bodies each
starting with its slot number.

## 3. Sound ids

| Ids | Bank | Notes |
|---|---|---|
| 0–63 | slot 0, shared fight bank | 24–29, 60–63 repeat tone 0; 19, 33, 34, 36, 37, 49 silent |
| 64–127 | slot 1, map bank | programs 0–2 |
| 128–193 | slots 3–60, enemy banks | one slot per enemy kind |
| 194–255 | slot 0 filler | |

Constant ids (VS addresses):

| Id | Sample | Played by |
|---|---|---|
| 0 | v12 | cannon fire `0x80081B18` |
| 1 | v9 | `0x8007E698` (movement) `?` |
| 2, 12 | v18, v19 | missile update `0x8008907C` (launch / flight loop) |
| 4 | v21 rifle shot | projectile kinds |
| 6 | v5 | `0x8008D0A4`; hit table |
| 7 | v34 | `0x80089B64`, `0x8008E8FC` (wreck) |
| 8, 9, 40, 41, 59 | v27, v28, v30, v29, v11 | `0x8005675C` (warnings `?`; arena `0x80056F9C`) |
| 14 | v13 | `0x8008389C`, `0x80085D40` |
| 15 | v23 | blade `0x8007EB48`, `0x8007F264` |
| 30–32 | v8, v16, v17 | missions only `0x80095078` |
| 35 | v26 | missions only `0x80091064` |
| 38, 39 | v24, v25 | missions only `0x8009115C` |
| 43, 44 | v32, v33 | missile lock: new / last free (`0x80080638`) |
| 45 | v11 | `0x80083030`, `0x800828BC` (alarm `?`) |
| 46 | v11 | gun / cannon lock `0x800800E4` |
| 47 | v11 | missile lock tick every 4 steps |
| 52 | v6 | `0x8008F034` |
| 53 | v22 | mines (gun fire `0x80081250`), cannon fire |
| 54, 55 | v16, v30 | VS main loop `0x80051F4C` (start/end `?`) |
| 57 | v36 | message readers (arena `0x8005A2C4`, missions `0x800592BC`) |

Footsteps: `0x8007E1DC` / `0x8007E2DC` / `0x8007E3E0` play byte **+17 of the AC's leg record**. Shot sounds come
from the shot tables (`weapon-map.md` §7). Hits: 8-byte records at `0x800930A0` (ids 6, 22 at volume 90 / 50).

**Enemy banks**: slot 3 (ids 134–138, missions 0, 9), 4 (128; 6, 12, 13, 16), 5 (129; 3, 7, 13), 8 (146; 2, 5),
9 (131; 1), 11 (153–158; 8, 16), 13 (160–161; 6), 19 (167–171; 7), 23 (188; 4), 24 (189; 8, 16), 25 (190; 9, 10),
26/27 (192/191; 7), 59 (181–183; 3), 60 (184–187; AC TEST). Slots 6, 7, 10, 14, 15, 17, 18, 20–22, 53, 54, 56–58
have no bank on the disc (silent).

## 4. Map banks

FDAT 5s + 4: s = 0–19 missions, 20–39 VS / arena stages, 40 AC TEST. Samples per slot-1 bank — missions:
0:3 1:9 2:5 3:2 4:7 5:4 6:10 7:9 8:4 9:5 10:1 11:1 12:5 13:6 14:1 15:4 16:7 17:3 18:4 19:4; stages: 20:1 21:8
22:none 23:none 24:4 25:1 26:6 27:3 28:4 29:1 30:1 31:5 32:4 33:1 34:6 35:3 36:1 37:4 38:1 39:5 40:none.
Every stage sample is also in a mission bank. VS maps 1–10 = stages 20, 21, 22, 23, 30, 33, 34, 35, 36, 37;
arena stage list `0x800D59F0` (20, 22, 24, 25, 27, 28, 29, 31, 33, 36, 37, 39).

Slot 2: the missions engine opens one of three VABs (blocks 13/14, 15/16, 17/18) by **COM voice type** (player
block +0x27A) at `0x80055AC4`. No id uses slot 2: likely a leftover `?`.

## 5. Menu sounds

`SsUtKeyOn` (exe `0x800227D4`) on the menu bank (vab s16 at `0x801B1A80`), program 0, note 60:

| Tone | Sample | Calls | When |
|---|---|---|---|
| 0 | v10 | ~60 | back / cancel |
| 1 | v1 | ~110 | cursor |
| 2 | v2 | ~50 | OK |
| 3–5 | v3–v5 | none | unused `?` |
| 6 | v11 | ~100 | window opens |
| 7 | v6 | ~20 | message box; refused |
| 8 | v8 | 1 (`0x80055548`) | hub `?` |
| 9 | v7 | 2 (`0x80095B50`, `0x800A3BE0`) | `?` |
| 10 | v9 | 2 | part fitted |
| 11 | v12 | — | part removed |
| 12 | v4 | 4 | ASSEMBLY row cursor |

**Menu song**: `0x80064F24(2)` loads MENU_VAB 2/3, then SsSeqOpen, SsSeqSetVol (BGM option × 2), SsSeqPlay
endless; `0x801D0AFC` = 1 while playing. Started by the hub open (`0x80056DBC`), the VS menu (`0x800B0FE4`) and
the link menu (`0x800ACF10`). The only SEQ on the disc.

## 6. XA streams

The exe registers 23 XA files at `0x80036C74` (36-byte records: 16 u16, then CdlLOC). **XA id = file × 16 +
channel**: files 0–3 BGM00–03 (ids 0–63), 4–10 TALK00–06 (64–175), 11–14 CPU00–03 (176–239, voice type 0),
15–18 CPU10–13 (240–303, type 1), 19–22 CPU20–23 (304–367, type 2). XA play (id; 0xFFFF stop): VS `0x80053C04`,
arena `0x800540EC`, missions `0x80053CA0`, link `0x80053E18`, replay `0x80053B6C`. Stop on exit (`0x800526EC` VS).

## 7. Music

Fight music = byte **`0x8003EA29`** (XA id, 255 none), played at fight start (VS `0x8008FA80`, arena `0x800969B8`
at step 44, missions `0x800911B8`):

| Mode | Value | Where |
|---|---|---|
| VS | random of **16, 17, 19, 20, 23, 32, 33, 38, 50, 54** | VS START `0x800B348C`, table `0x800538A8` |
| Arena | same 10 | arena launch `0x800C0878` |
| Missions | MIS.T entry 0, T+1160[slot]: mission 2 → 38, 5 → 35, 8 → 52, else 255 | `0x800976CC` |
| AC TEST, RANKER TEST | 255 | `0x8007DB44`, `0x8007DE4C` |
| `0x800AFEDC` (link launch `?`) | 50 | |

In-mission changes come from message code `B`n (missions `0x80059628`, arena `0x8005A630`): 20 msg 29; 21 msg 101;
32 msgs 11, 119; 33 msg 36; 34 msg 45; 36 msg 61; 37 msg 49; 38 msgs 28, 120, 121; 39 msg 25; 49 msg 6;
50 msgs 8, 21, 32, 40, 43, 47, 51, 58 (enemy Raven appears); 52 msg 44; 54 msg 118.
**Never referenced:** 0–7 (all of BGM00), 18, 22, 48, 51, 53, 55.

## 8. Voices

Message strings start with codes (readers: missions `0x800592BC`, arena `0x8005A2C4`): `M`n lines `?`; `X`n radio
voice XA 64 + n; `V`n COM voice XA 176 + 64 × type + n; `B`n music; `N`n box style `?`; `H`, `T` text only.

**COM voice type** = player block **+0x27A** (compiled +0x2A = head byte +0x32): picks the CPU file set, the
message block and the slot-2 VAB. It's the head's voice. Radio: X0–X97 used (messages 3–123, 147–148). COM: only
V0, V2, V3, V16, V37, V38, V39, V43 are used (messages 151–158).

## 9. Open items

- Name the samples by ear (listening pack `moa-sound-pack.zip`, made in chat, not in the repo).
- What `0x8005675C` plays ids 8, 9, 40, 41, 59 for; ids 54/55 in the VS loop.
- Confirm nothing plays slot 2 or the unused COM / radio clips.
- BGM00 and the other unreferenced songs; movie audio.
- The 448 bytes before the id table (`0x80044598`); the u16 values in the XA file records.
