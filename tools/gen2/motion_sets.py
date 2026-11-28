"""Gen 2 motion sets: which B file (421-492) an AC uses, from its core, arms and legs.
Found 2026-11-27 (session 6) by matching each part's skeleton block to the sets' skeletons
(every part matched exactly one group, all others scored 0). The game loads file 420 + [AC parts
record +0x20] (loader 0x1b20c8 in SLUS_200.14) through the motion-bank loader 0x126d28.

set file = 421 + 24 * core_group + 8 * arms_group + legs_group
legs groups: 0-2 humanoid, 3-4 reverse joint, 5 four-legged, 6 caterpillar, 7 hover
Usage: python3 motion_sets.py CORE_F_ID ARMS_F_ID LEGS_F_ID
"""
import sys
CORE = {
    3420: ('ZCX-F/ROOK', 1),
    3421: ('ECM-XR00', 1),
    3422: ('ECL-ONE', 2),
    3423: ('ZCL-XA/2', 2),
    3424: ('ZCH-GR/1', 0),
    3425: ('ECH-D4', 0),
}
ARMS = {
    3430: ('ZAN-414/SLA', 1),
    3431: ('EAN-S0', 1),
    3432: ('EAN-CEX', 1),
    3433: ('ZAN-616/AUR', 1),
    3434: ('EAN-2112', 1),
    3435: ('ZAN-202/TEM', 2),
    3436: ('EAN-02-BG', 2),
    3437: ('ZAN-303/S', 2),
    3438: ('EAN-1111', 2),
    3439: ('ZAN-707/E', 0),
    3440: ('EAN-MWZ', 0),
    3441: ('ZAN-004/B', 0),
    3442: ('ZAN-AX/VISE', 0),
    3443: ('EAN-MHKS', 0),
    3444: ('ZAW-2/SAMURAI', 0),
    3445: ('EAW-MG-03', 1),
    3446: ('EAW-BZD', 1),
    3447: ('EAW-DC10', 1),
    3448: ('ZAW-XP/T9', 1),
    3449: ('ZAW-SPLASH', 1),
    3450: ('ZAW-LC/NPM', 1),
    3451: ('EAW-H801', 1),
    3452: ('EAW-S604', 1),
}
LEGS = {
    3460: ('ZLN-XX0/TP', 1),
    3461: ('ZLN-XA1/FA', 1),
    3462: ('ELN-2002AT', 1),
    3463: ('ELN-02A', 1),
    3464: ('ZLN-XA2/FF', 1),
    3465: ('ZLN-EK1/SRRT', 2),
    3466: ('ELN-701', 2),
    3467: ('ZLN-WA02/SS', 2),
    3468: ('ZLN-2A/GR', 0),
    3469: ('ELN-STVM', 0),
    3470: ('ZLN-9001/A', 0),
    3471: ('ELB-A1001', 4),
    3472: ('ZLB-3322/OP', 4),
    3473: ('ELB-5005B', 4),
    3474: ('ZLB-7744/VAT', 3),
    3475: ('ELB-XXE-LAPIN', 3),
    3476: ('ZLF-TR05/INTS', 5),
    3477: ('ZLF-A3/CARM', 5),
    3478: ('ELF-DEX-2F', 5),
    3479: ('ELF-SOC61', 5),
    3480: ('ELF-XX33', 5),
    3481: ('ZLC-M2/PIKE', 6),
    3482: ('ELC-MLK0303', 6),
    3483: ('ELC-D1S1', 6),
    3484: ('ELC-HTT0808', 6),
    3485: ('ZLC-C04T/CYOR', 6),
    3486: ('ZLR-K196/RJ', 7),
    3487: ('ZLR-EE/STOL', 7),
    3488: ('ZLR-MOC200/FG', 7),
    3489: ('ZLR-TII/BUD', 7),
    3490: ('ZLR-ZIO/MATRIX', 7),
}

def set_file(core, arms, legs):
    return 421 + 24 * CORE[core][1] + 8 * ARMS[arms][1] + LEGS[legs][1]

if __name__ == '__main__':
    c, a, l = map(int, sys.argv[1:4])
    print(CORE[c][0], ARMS[a][0], LEGS[l][0], '->', '%05d.bin' % set_file(c, a, l))
