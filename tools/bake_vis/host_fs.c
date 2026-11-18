/* Host (macOS) file layer for the SHMUP visibility bake: plain stdio relative to the data
 * root (cwd). The engine's hard-coded output dir /Users/fabiensanglard/tmp/ maps to $BAKE_OUT. */
#include "filesystem.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
bool FS_InitFilesystem(void){return 1;}
char *FS_Gamedir(void){return "";}
static void map_path(const char *in,char *out,size_t cap){
    const char *tmp="/Users/fabiensanglard/tmp/";
    if(!strncmp(in,tmp,strlen(tmp))){const char *o=getenv("BAKE_OUT");snprintf(out,cap,"%s/%s",o?o:".",in+strlen(tmp));return;}
    while(*in=='/')++in; snprintf(out,cap,"%s",in);
}
filehandle_t *FS_OpenFile(const char *filename,char *mode){
    char p[1024];map_path(filename,p,sizeof p);
    int w=mode&&(strchr(mode,'w')||strchr(mode,'a'));
    FILE *f=fopen(p,w?"wb":"rb");if(!f)return 0;
    filehandle_t *h=calloc(1,sizeof *h);h->hFile=f;h->isWritable=(uchar)w;
    if(!w){fseek(f,0,SEEK_END);h->filesize=(W32)ftell(f);fseek(f,0,SEEK_SET);}
    return h;
}
int FS_UploadToRAM(filehandle_t *h){
    if(!h||!h->hFile)return 0;if(h->bLoaded)return 1;
    uchar *b=malloc(h->filesize+1);fseek((FILE*)h->hFile,0,SEEK_SET);size_t n=fread(b,1,h->filesize,(FILE*)h->hFile);b[n]=0;
    h->filedata=b;h->ptrStart=h->ptrCurrent=b;h->ptrEnd=b+n;h->bLoaded=1;return 1;
}
void FS_CloseFile(filehandle_t *h){if(!h)return;if(h->filedata)free(h->filedata);if(h->hFile)fclose((FILE*)h->hFile);free(h);}
SW32 FS_Read(void *buf,W32 size,W32 count,filehandle_t *h){
    if(!h||!size)return 0;if(!h->bLoaded)FS_UploadToRAM(h);
    size_t want=size*count,avail=(size_t)(h->ptrEnd-h->ptrCurrent);if(want>avail)want=avail;
    memcpy(buf,h->ptrCurrent,want);h->ptrCurrent+=want;return (SW32)(want/size);
}
SW32 FS_Write(const void *buf,W32 size,W32 count,filehandle_t *h){return h&&h->hFile?(SW32)fwrite(buf,size,count,(FILE*)h->hFile):0;}
void *FS_GetLoadedFilePointer(filehandle_t *h,W32 origin){
    if(!h||!h->bLoaded)return 0;if(origin==SEEK_SET)return h->ptrStart;if(origin==SEEK_CUR)return h->ptrCurrent;if(origin==SEEK_END)return h->ptrEnd;return 0;}
char *FS_GetExtensionAddress(char *s){char *d=strrchr(s,'.');return d?d+1:0;}
char *FS_GetFilenameOnly(char *s){char *d=strrchr(s,'/');return d?d+1:s;}
