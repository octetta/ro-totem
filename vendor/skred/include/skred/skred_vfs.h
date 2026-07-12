#ifndef SKRED_VFS_H
#define SKRED_VFS_H

#include <stdio.h>
#include <stdbool.h>
#include <stddef.h>

/* --- Types --- */

typedef struct SkredFile SkredFile;
typedef struct SkredDir SkredDir;

typedef struct {
    char d_name[256];
    bool is_directory;
} SkredDirent;

typedef enum {
    SKRED_VFS_DISK = 0,
    SKRED_VFS_ZIP = 1,
    SKRED_VFS_ZIP_MEMORY = 2
} skred_vfs_mode_t;

/* --- Lifecycle --- */

// Initialize the VFS. Path can be a directory or a .zip file.
bool skred_vfs_init(const char *path);
bool skred_vfs_mount(const char *path);
bool skred_vfs_mount_zip_memory(const void *data, size_t size, const char *label);
void skred_vfs_unmount(void);
void skred_vfs_shutdown(void);
skred_vfs_mode_t skred_vfs_mode(void);
const char* skred_vfs_root(void);
const char* skred_vfs_status(void);

/* --- Environment State --- */

bool        skred_chdir(const char *path);
const char* skred_getcwd(void);

/* --- File I/O --- */

SkredFile* skred_fopen(const char *filepath, const char *mode);
size_t     skred_fread(void *ptr, size_t size, size_t count, SkredFile *stream);
size_t     skred_fwrite(const void *ptr, size_t size, size_t count, SkredFile *stream);
long       skred_ftell(SkredFile *stream);
int        skred_fseek(SkredFile *stream, long offset, int origin);
int        skred_fclose(SkredFile *stream);

/* --- Directory Iteration --- */

SkredDir*    skred_opendir(const char *dirpath);
SkredDirent* skred_readdir(SkredDir *dirp);
int          skred_closedir(SkredDir *dirp);

/* --- Whole-file helpers --- */

bool skred_vfs_read_file(const char *filepath, void **data, size_t *size);
bool skred_vfs_read_real_file(const char *filepath, void **data, size_t *size);
void skred_vfs_free_file(void *data);

#endif // SKRED_VFS_H
