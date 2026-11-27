/* The hangar main menu's room in 3D (user, 2026-11-27: the flat picture
 * had no depth and nothing touched the AC). Everything is sized from the
 * player's AC (its height and half-width), so the gantry clamps, the
 * walkways, the cables and the maintenance arm's targets meet the AC
 * whatever its legs. Engine world, y up, the AC standing at the origin
 * facing -z (towards the camera). */
#ifndef HANGAR3D_H
#define HANGAR3D_H

typedef struct hangar3d_view {
    float eye[3];        /* the camera */
    float right[3], up[3];   /* its axes, for the steam and sparks (camera-facing) */
} hangar3d_view;

/* The camera for an AC `height` tall and `half_w` wide: low, a little to
 * the side, looking up at it. Fills eye, target (where it looks) and the
 * view's axes. */
void hangar3d_camera(float height, float half_w, float eye[3], float target[3], hangar3d_view *v);

/* The room, the gantry, cables, props and arms: before the AC. `ms` is a
 * running clock. */
void hangar3d_solid(float height, float half_w, unsigned ms);
/* Steam, sparks, light cones and beacon glows: after the AC (see-through,
 * sorted back to front). */
void hangar3d_fx(float height, float half_w, unsigned ms, const hangar3d_view *v);

#endif
