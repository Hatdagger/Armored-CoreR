# AC2 vs Another Age: shop catalogue (2026-11-27, session 4)

Source: F blocks split with `tools/gen2/split_bin.py`; compared with `tools/gen2/compare_shop.py AC2_F AA_F`.
AC2 has 234 shop parts, Another Age 270.

## New in Another Age: 26 shop parts, none removed
Heads EHD-SCOPEEYE, ZHD-102/ROCK · legs ELN-02SLD, ELN-070, ZLN-01/BARREL, ZLR-ARROW · inside INW-DM-MV, INW-RV-08, INW-OM-PRT · extensions BEX-BT180, BEX-AA00, EXM-DD02 · back units EWM-NAP-02, EWM-M03, ZWM-M55/6, ZWM-GT/00, ZWR-M/30, EWR-M40, ZWC-CN/500, EWC-GN-81, ZWC-XP02/QL · right-arm EWG-RF-10LB · left-arm EWG-XC213, ZWG-XC/01, EWG-HM-04, ELS-7880.
(SLUS_202.49 has 23 new model codes; the inside/extension parts account for the difference.)
EHD-WACS, ZXR-S/STEALTH and ZES-99/MIRROR are AC2 parts (they were truncated in the old extraction), not AA-only.

## Changed stats: 57 existing parts
Record offsets (u16): +114 weight, +116 energy drain; the others are not named yet.
- Weight cuts: ZCL-XA/2 1773→1553, ZAW-XP/T9 1900→1694, EAW-H801 2148→1920, back cannons (EWC-CNG4000, ZWC-LQ/2552, ZWC-IR/FLUX, EWX-GCN77-4), EWG-MGA2 706→626.
- Drain cuts: radiators RRX-COT-1550 1060→760, RRX-COT-GK10 580→442, RBG-CLX5000 925→525.
- Cores: +138 changed (ZCX-F/ROOK 580→660, ECM-XR00 660→580, ECL-ONE 640→580, ZCH-GR/1 710→760, ECH-D4 780→700).
- Legs: four-legged +140 up ~10% (e.g. ZLF-TR05/INTS 487→541); float (ZLR) +130/+132 down ~10%, +140 up ~20%; reverse-joint tweaks.
- Boosters: +128 down (ZBT-Z1/ARTERE 21000→19300, EBT-GR4300 8800→7100), EBT-GR4300 +130 2500→3200. Generator HOY-B1000 +128 13000→11740, +130 34000→42000.
- Shields: +130 down ~30% (ZES-500/SU 3400→2450, ZES-99/MIRROR 3250→2280). Blades: +136 23→39 on all.
- Extensions: AA inserts a field before the weapon-row u16, so their record layout shifts by 2 bytes (not a stat change).
