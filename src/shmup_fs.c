#include "filesystem.h"
#include "stdlib.h"
#include "string.h"
#include "stdio.h"
#include "nano_fs.h"

static int g_fs_mount_rc = -1;

int shmup_fs_mount_status(void) {
    return g_fs_mount_rc;
}

bool FS_InitFilesystem(void) {
    if (g_fs_mount_rc == 0) return true;
    g_fs_mount_rc = nano_fs_mount();
    return g_fs_mount_rc == 0;
}

char *FS_Gamedir(void) {
    return "";
}

static void make_hfs_path(const char *filename, char *out, size_t cap) {
    while (*filename == '/') ++filename;
    snprintf(out, cap, "/Apps/Data/Shmup/%s", filename);
}

filehandle_t *FS_OpenFile(const char *filename, char *mode) {
    if (!filename || g_fs_mount_rc != 0) return 0;
    if (mode && (strchr(mode, 'w') || strchr(mode, 'a'))) return 0;

    char path[512];
    make_hfs_path(filename, path, sizeof path);

    nano_file_t *nf = nano_fs_open(path);
    if (!nf) return 0;

    filehandle_t *fh = (filehandle_t *)calloc(1, sizeof(filehandle_t));
    if (!fh) {
        nano_fs_close(nf);
        return 0;
    }
    fh->hFile = nf;
    fh->filesize = (W32)nano_fs_size(nf);
    fh->bLoaded = 0;
    fh->isWritable = 0;
    return fh;
}

int FS_UploadToRAM(filehandle_t *fhandle) {
    if (!fhandle || !fhandle->hFile) return 0;
    if (fhandle->bLoaded) return 1;

    nano_file_t *nf = (nano_file_t *)fhandle->hFile;
    uint32_t sz = (uint32_t)fhandle->filesize;
    uchar *buf = (uchar *)malloc(sz + 1u);
    if (!buf) return 0;

    uint32_t off = 0;
    while (off < sz) {
        uint32_t want = sz - off;
        if (want > 65536u) want = 65536u;
        int got = nano_fs_pread(nf, off, buf + off, want);
        if (got <= 0) {
            free(buf);
            return 0;
        }
        off += (uint32_t)got;
    }
    buf[sz] = 0;
    fhandle->filedata = buf;
    fhandle->ptrStart = buf;
    fhandle->ptrCurrent = buf;
    fhandle->ptrEnd = buf + sz;
    fhandle->bLoaded = 1;
    return 1;
}

void FS_CloseFile(filehandle_t *fhandle) {
    if (!fhandle) return;
    if (fhandle->filedata) { free(fhandle->filedata); fhandle->filedata = 0; }
    if (fhandle->hFile) { nano_fs_close((nano_file_t *)fhandle->hFile); fhandle->hFile = 0; }
    free(fhandle);
}

SW32 FS_Read(void *buffer, W32 size, W32 count, filehandle_t *fhandle) {
    if (!fhandle || !buffer || !size || !count) return 0;
    uint32_t want = (uint32_t)(size * count);
    if (!fhandle->bLoaded) FS_UploadToRAM(fhandle);
    if (fhandle->bLoaded) {
        uint32_t avail = (uint32_t)(fhandle->ptrEnd - fhandle->ptrCurrent);
        if (want > avail) want = avail;
        memcpy(buffer, fhandle->ptrCurrent, want);
        fhandle->ptrCurrent += want;
        return (SW32)(want / size);
    }
    nano_file_t *nf = (nano_file_t *)fhandle->hFile;
    if (!nf) return 0;
    uint32_t off = 0;
    uint8_t *dst = (uint8_t *)buffer;
    while (off < want) {
        uint32_t chunk = want - off;
        if (chunk > 65536u) chunk = 65536u;
        int got = nano_fs_read(nf, dst + off, chunk);
        if (got <= 0) break;
        off += (uint32_t)got;
    }
    return (SW32)(off / size);
}

SW32 FS_Write(const void *buffer, W32 size, W32 count, filehandle_t *stream) {
    (void)buffer; (void)size; (void)count; (void)stream;
    return 0;
}

void *FS_GetLoadedFilePointer(filehandle_t *fhandle, W32 origin) {
    if (!fhandle || !fhandle->bLoaded) return 0;
    if (origin == SEEK_SET) return fhandle->ptrStart;
    if (origin == SEEK_CUR) return fhandle->ptrCurrent;
    if (origin == SEEK_END) return fhandle->ptrEnd;
    return 0;
}

char *FS_GetExtensionAddress(char *string) {
    if (!string) return 0;
    char *dot = strrchr(string, '.');
    return dot ? dot + 1 : 0;
}

char *FS_GetFilenameOnly(char *string) {
    if (!string) return 0;
    char *slash = strrchr(string, '/');
    return slash ? slash + 1 : string;
}
