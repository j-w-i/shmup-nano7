/* SHMUP visibility bake on the host: loads a scene's map with the engine's own loaders and
 * runs the engine's own preprocessor (preproc.c PREPROC_ConvertCp1Tocp2b) to regenerate the
 * camera path's per-frame visibility sets (.cp2b) for a given screen shape.
 *   bake SCENE CP2B_IN ASPECT_W ASPECT_H   (cwd = data root; output in $BAKE_OUT)
 * The camera frames (time, position, orientation, key-frame marks) are read from the shipped
 * .cp2b and kept exactly: the shipped .cp text sources are not the paths the game was baked with
 * (act1.cp is a longer route), and events are timed to the shipped path. Only the per-frame
 * visibility is recomputed, with the engine's PREPROC_PopulateRawFaceSet / ConvertPrecToRuntime.
 * The vertical field of view is widened so the horizontal view stays the original 320:480
 * one: tan(fovy'/2) = tan(fovy/2) * (320/480) / (W/H). ASPECT 320 480 reproduces the original. */
#include "globals.h"
#include "renderer.h"
#include "camera.h"
#include "world.h"
#include "preproc.c"   /* the engine's own preprocessor, compiled in (its tables are file-scope) */
#include "filesystem.h"
#include "entities.h"
#include <stdlib.h>
#include <math.h>
static void up_entity(entity_t *e){(void)e;}
renderer_t renderer;
int main(int argc,char **argv){
    if(argc<5){fprintf(stderr,"usage: bake SCENE CPFILE W H\n");return 2;}
    renderer.UpLoadEntityToGPU=up_entity;
    ENT_InitCacheSystem();
    World_OpenScene(argv[1]);
    float a0=320.f/480.f,a1=(float)atof(argv[3])/(float)atof(argv[4]);
    float fov0=camera.fov;
    float fov1=2.f*atanf(tanf(fov0*(float)M_PI/360.f)*a0/a1)*180.f/(float)M_PI;
    camera.aspect=a1;camera.fov=fov1;
    fprintf(stderr,"scene %s: fov %.3f -> %.3f, aspect %.4f, near %g far %g, %d map entities\n",argv[1],fov0,fov1,a1,camera.zNear,camera.zFar,num_map_entities);
    /* read the shipped frames */
    FILE *f=fopen(argv[2],"rb");if(!f){fprintf(stderr,"cannot open %s\n",argv[2]);return 1;}
    fseek(f,0,SEEK_END);long L=ftell(f);fseek(f,0,SEEK_SET);unsigned char *d=malloc(L);fread(d,1,L,f);fclose(f);
    long o=4;int n;memcpy(&n,d+o,4);o+=4;
    prec_camera_frame_t *root=0,*last=0;
    for(int i=0;i<n;++i){
        prec_camera_frame_t *fr=calloc(1,sizeof *fr);
        memcpy(&fr->time,d+o,4);o+=4;memcpy(fr->position,d+o,12);o+=12;memcpy(fr->orientation,d+o,16);o+=16;
        fr->visSet.isKey=(char)d[o];o+=1;unsigned short nv;memcpy(&nv,d+o,2);o+=2;
        for(int k=0;k<nv;++k){o+=2;unsigned short a;memcpy(&a,d+o,2);o+=2;
            if(fr->visSet.isKey)o+=2*a;else{o+=2*a;unsigned short r;memcpy(&r,d+o,2);o+=2+2*r;}}
        if(last)last->next=fr;else root=fr;last=fr;
    }
    if(o!=L){fprintf(stderr,"cp2b parse mismatch %ld/%ld\n",o,L);return 1;}
    /* PREPROC_ConvertCp1Tocp2b, from its tables onwards */
    entityIndiceToModelIndice=calloc(num_map_entities,sizeof(ushort*));modelIndiceToEntityIndice=calloc(num_map_entities,sizeof(ushort*));
    for(int i=0;i<num_map_entities;i++){entityIndiceToModelIndice[i]=calloc(map[i].numIndices,sizeof(ushort));modelIndiceToEntityIndice[i]=calloc(map[i].numIndices,sizeof(ushort));}
    indicesPerObjectId=calloc(num_map_entities,sizeof(ushort));
    camera_frame_t rootRT;memset(&rootRT,0,sizeof rootRT);camera_frame_t *prevRT=&rootRT;prec_camera_frame_t *prev=0,*cur=root;
    while(cur){
        cur->visSet.visFaces=calloc(sizeof(prec_face_t),MAX_POLY_VIS_PER_FRAME+1);
        PREPROC_PopulateRawFaceSet(cur);
        camera_frame_t *rt=calloc(1,sizeof *rt);PREPROC_ConvertPrecToRuntime(prev,cur,rt);
        if(prev){free(prev->visSet.visFaces);free(prev);}
        prev=cur;cur=cur->next;prevRT->next=rt;prevRT=rt;
    }
    static char out[300];snprintf(out,sizeof out,"%s",FS_GetFilenameOnly(argv[2]));
    PREPROC_SaveFramesToCP2Binary(out,rootRT.next);
    return 0;
}
