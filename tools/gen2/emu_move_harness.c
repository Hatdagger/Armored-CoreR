/* Harness for emu_move.py: exposes acr_gen2_move.c's routines, one case per
 * input line (floats as hex bit patterns), so they can be compared with the
 * game's own code run in an emulator.
 *   cc -O2 tools/gen2/emu_move_harness.c -lm -o g2prim */
#include <stdio.h>
#include "../../src/gen2/acr_gen2_move.c"
static float rf(void){ unsigned u; if (scanf("%x",&u)!=1) return 0; float f; memcpy(&f,&u,4); return f; }
static unsigned bits(float f){ unsigned u; memcpy(&u,&f,4); return u; }
int main(void){
    char op[16];
    while (scanf("%15s", op) == 1) {
        acr_gen2_ac ac; acr_gen2_stats s; acr_gen2_input in;
        memset(&ac,0,sizeof ac); memset(&s,0,sizeof s); memset(&in,0,sizeof in);
        if (!strcmp(op,"turn")) { int t; float acc,max; scanf("%d",&t); ac.yaw_speed=rf(); ac.yaw=rf(); acc=rf(); max=rf(); in.turn=(int8_t)t;
            turn(&ac,&in,acc,max); printf("%08x %08x\n", bits(ac.yaw_speed), bits(ac.yaw)); }
        else if (!strcmp(op,"brake")) { float dec, r; ac.vx=rf(); ac.vz=rf(); dec=rf(); r=brake(&ac,dec); printf("%08x %08x %08x\n", bits(ac.vx), bits(ac.vz), bits(r)); }
        else if (!strcmp(op,"push")) { float ax,az,max; ac.vx=rf(); ac.vz=rf(); ax=rf(); az=rf(); max=rf(); s.flymv_accel=rf(); push(&ac,&s,ax,az,max); printf("%08x %08x\n", bits(ac.vx), bits(ac.vz)); }
        else if (!strcmp(op,"fall")) { float g,max; ac.vy=rf(); g=rf(); max=rf(); fall(&ac,g,max); printf("%08x\n", bits(ac.vy)); }
        else if (!strcmp(op,"rise")) { ac.vy=rf(); s.flyup_accel=rf(); s.flyup_maxspd=rf(); rise(&ac,&s); printf("%08x\n", bits(ac.vy)); }
        else if (!strcmp(op,"dir")) { int f,b,l,r; scanf("%d %d %d %d",&f,&b,&l,&r); in.forward=f; in.back=b; in.left=l; in.right=r; printf("%d\n", acr_gen2_dir(&in)); }
        else if (!strcmp(op,"energy")) { int st,kind,ch,boost,e;
            scanf("%d %d %d %d %d %d %d %d %d %d %d", &st,&kind,&ch,&boost,&e,&s.boost_max,&s.boost_redarea,&s.boost_sub,&s.boost_add,&s.move_boost_add,&s.oboost_sub);
            ac.vx=rf(); ac.vz=rf(); ac.state=(uint8_t)st; s.leg_kind=kind; ac.charging=(uint8_t)ch; in.boost=(uint8_t)boost; ac.energy=e;
            energy(&ac,&s,&in); printf("%d %d\n", ac.energy, ac.charging); }
    }
    return 0;
}
