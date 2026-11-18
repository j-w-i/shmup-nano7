#!/usr/bin/env python3
"""Write engine_bits.c: verbatim copies of the engine functions the bake needs (frustum tests from
collisions.c, gluPerspective / gluLookAt from renderer.c), plus stubs for game-side calls a bake
never makes. Copied rather than linked: those files pull in the whole game."""
import pathlib, sys
E = pathlib.Path(sys.argv[1]); out = pathlib.Path(sys.argv[2])
def fn(path, start):
    s = (E / path).read_text(errors='replace'); i = s.index(start); i = s.rindex('\n', 0, i) + 1
    j = s.index('{', i); d = 0
    for k in range(j, len(s)):
        d += s[k] == '{'; d -= s[k] == '}'
        if d == 0 and s[k] == '}': return s[i:k + 1]
    raise SystemExit('unbalanced ' + start)
parts = [fn('collisions.c', 'static float COLL_distPointPlan('), fn('collisions.c', 'void COLL_GenerateFrustrum('),
         fn('collisions.c', 'Intersection_test COLL_CheckBoxAgainstPlan('), fn('collisions.c', 'Intersection_test COLL_CheckBoxAgainstFrustrum('),
         fn('renderer.c', 'void gluPerspective('), fn('renderer.c', 'void gluLookAt(')]
hdr = '''#include "globals.h"
#include "collisions.h"
#include "renderer.h"
#include "material.h"
#include "texture.h"
#include "dEngine.h"
#include "player.h"
#include "titles.h"
#include "event.h"
#include "vis.h"
engine_info_t engine; player_t players[2]; texture_t titleTexture;
void EV_AddEvent(event_t* e){(void)e;} void EV_ReadEnemiesEvents(void){} void EV_ReadTextsEvents(void){}
material_t* MATLIB_Get(char* name){(void)name;return 0;}
void MATLIB_MakeAvailable(material_t* m){(void)m;} void TEX_MakeStaticAvailable(texture_t* t){(void)t;} void VIS_Update(void){}
'''
out.write_text(hdr + '\n\n'.join(parts) + '\n')
