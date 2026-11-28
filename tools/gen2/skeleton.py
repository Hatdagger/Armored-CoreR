"""Gen 2 biped motion skeleton: which motion node drives which part mesh (session 8, Gen 2).
Found by matching each node's rest offset (motion 0, set 453 = ZCX-F/ROOK, EAN-S0, ELN-STVM) to the
differences between the test AC's mesh pivots, then checked against which nodes move in walk/land/kneel.
The clip's byte 2 is NOT the parent (it is always index - 3); parents below are inferred from the offsets.
Sides: +x is the AC's LEFT (glTF export puts the right hand at -x).
Usage: python3 skeleton.py   (prints the table)
"""
# node: (name, parent node, part, mesh index or None)
BIPED = {
    0: ('root_motion', None, None, None),       # world translation track (moves 30 units in a walk loop)
    1: ('centre', 0, 'legs', 0),                # hip height bob (y -7); legs mesh 0
    2: ('unused_2', 0, None, None),
    3: ('waist', 1, 'core', 0),                 # upper-body twist; core mesh 0
    4: ('core_L', 3, 'core', 1), 5: ('core_R', 3, 'core', 2),
    6: ('core_L2', 3, 'core', 3), 7: ('core_R2', 3, 'core', 4),
    8: ('booster_L', 3, 'booster', 1), 9: ('booster_R', 3, 'booster', 0),
    10: ('back_mount_L', 3, None, None), 11: ('back_mount_R', 3, None, None),   # 60 deg pitched mounts
    12: ('head', 3, 'head', None),              # top centre of core, shoulder height
    13: ('shoulder_L', 3, 'arms', 0), 14: ('upperarm_L', 13, 'arms', 1),
    15: ('forearm_L', 14, 'arms', 2), 16: ('hand_L', 15, 'arms', 3),
    17: ('shoulder_R', 3, 'arms', 4), 18: ('upperarm_R', 17, 'arms', 5),
    19: ('forearm_R', 18, 'arms', 6), 20: ('hand_R', 19, 'arms', 7),
    21: ('unused_21', 0, None, None),
    22: ('pelvis', 1, None, None),              # rotates a little in walk/land
    23: ('thigh_L', 22, 'legs', 1), 24: ('shin_L', 23, 'legs', 2),
    25: ('foot_L', 24, 'legs', 3), 26: ('toe_L', 24, 'legs', 4),
    27: ('thigh_R', 22, 'legs', 5), 28: ('shin_R', 27, 'legs', 6),
    29: ('foot_R', 28, 'legs', 7), 30: ('toe_R', 28, 'legs', 8),
    31: ('leg_back', 1, 'legs', 9),
    32: ('ground', 0, None, None),              # small vertical track in walk (shadow / foot height?)
}
if __name__ == '__main__':
    for n, (name, p, part, m) in BIPED.items():
        print('%2d %-14s parent %-4s %s' % (n, name, p, '' if part is None else '%s mesh %s' % (part, m)))
