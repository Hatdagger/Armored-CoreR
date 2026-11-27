# SP-M/AUTO (reworked, 2026-11-27)

An optional part. Fires the AC's missiles on its own, without the missile
launcher being the selected weapon.

## Rules
- **Fully automatic.** Every equipped missile launcher that is not the
  selected weapon locks and fires by itself.
- **Hidden FCS.** Each launcher runs the missile lock with the equipped FCS's
  missile settings (lock box, range, lock time, lock count). None of it is
  drawn: no missile sight, no lock markers, no lock beeps, no camera swing.
- **Hold Select** (0.5 s, `ACR_AUTO_HOLD_MS`) switches the auto-fire sequence
  on and off. A tap of Select still switches target.
- **Manual takes over.** Switching to a missile launcher by hand stops auto
  fire. Only holding Select turns it back on.
- **Yellow box.** If a hidden missile lock is building or held while the
  pilot's own sight has no lock, the FCS box turns yellow. Lock and fire carry
  on as normal.
- **Several launchers.** Each launcher keeps its own lock, refire wait and
  volley (`acr_gen1_auto_lock`); they never sync up.
- **Fires** when its locks are full (or its ammo is down to the locks held),
  then waits out its refire wait and locks again. Stops when empty.
- **Starts on** at the beginning of a match.
- **Not compatible** with SP-MODERN or Human PLUS (`acr_gen1_auto_active`).

## HUD
- **SP-M/AUTO** label, bottom right above the warning boxes (ARMOR LOW,
  LEAVING AREA, ...). Green while auto fire is on, red while it is off. Only
  shown when the part is working.

## Online
- The toggle travels in the pad (bit 18, still 3 bytes); the auto state is in
  the match checksum.
