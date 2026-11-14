# Gen 1 engine map: Master of Arena's arena engine

What is known so far about how the arena battle engine is put together.
Source: Master of Arena (USA, SLUS-01030), `GG/COM/FDAT.T` entries, loaded at
`0x80050DD8`. Addresses below are runtime addresses.

## The battle engines

| FDAT entry | Engine | How identified |
| --- | --- | --- |
| 206 | menus | entry point `0x800A69C8`, called right after loading |
| 208 | link-cable versus | matches the recomp's link netplay hooks (`0x8007E4C8`) |
| 210 | **arena** | matches the recomp's AI Battle hooks (`0x8008D4AC`, `0x80055768`) |
| 207, 209, 211 | other modes (missions, split-screen) | same frame-end code; not yet told apart |

Each engine file starts with a small header: a count, then the entry point
(`0x80052378` in the arena engine), then HUD text such as `ENERGY` and
`MISSION TIME`.

## One arena frame (main loop at `0x80052378`)

1. Read the controllers (boot program `0x80017594`).
2. Clear the low 3 bits of each player's status words.
3. Unless replaying: input and world upkeep (`0x80096AF8`, `0x800796B4`,
   `0x80079C68`), the frame counter (`0x8007F8B0`), then `0x800935FC`.
   When replaying, the controller sampler (`0x8007F69C`) runs instead.
4. Call the current match state through a function pointer (countdown,
   fight, result).
5. Per-player AC work and drawing, then the frame end described in
   [gen1-timing.md](gen1-timing.md).

## Players and controls

- Player blocks: `0x80045198`, 928 bytes each, player 2 straight after.
- `+4`: the button word the AC code reads; `+6`: last frame's buttons.
- `+920`: pointer to the player's AC object (376 bytes each).
- The button word is the PS1 controller word made active-high:
  Select `0x0001`, Start `0x0008`, Up `0x0010`, Right `0x0020`,
  Down `0x0040`, Left `0x0080`, L2 `0x0100`, R2 `0x0200`, L1 `0x0400`,
  R1 `0x0800`, Triangle `0x1000`, Circle `0x2000`, Cross `0x4000`,
  Square `0x8000`.

## Opponent AI

- MoA's AI flies its AC by **writing that same button word**, exactly like a
  controller. This matches our AI design ([../AI.md](../AI.md)).
- The AI picks actions by **scoring** them: a table of action and scoring
  functions at `0x80098890-0x80098A70`, filled per opponent at match start
  (`0x80055768`), run once a frame (`0x8008D4AC`). Scoring functions use
  distances between the ACs (for example `0x8008A078`), so the Gen 1 brain
  can be rebuilt as a utility AI with the same inputs.

## Still to map

- The per-AC update: walking, turning, boosting, jumping, energy, heat.
  Leading candidate: `0x8008E55C`, called once a frame from the main loop at
  `0x80052BA4`, which uses the AC object pointers 12 times.
