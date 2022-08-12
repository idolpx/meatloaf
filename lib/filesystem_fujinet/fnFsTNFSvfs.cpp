/* These are the "driver" functions needed to register 
    with the ESP-IDF VFS
*/

#include "fnFsTNFSvfs.h"

#include <dirent.h>
#include <esp_vfs.h>

#include "../../include/debug.h"

#include "tnfslib.h"

/*
    These are the 23 functions that can be registered (not including 6 functions for select())
    from esp_vfs.h:

    IMPLEMENTED:
    int (*open_p)(void* ctx, const char * path, int flags, int mode);
    int (*close_p)(void* ctx, int fd);
    ssize_t (*read_p)(void* ctx, int fd, void * dst, size_t size);
    int (*stat_p)(void* ctx, const char * path, struct stat * st);
    ssize_t (*write_p)(void* p, int fd, const void * data, size_t size);
    off_t (*lseek_p)(void* p, int fd, off_t size, int mode);
    int (*fstat_p)(void* ctx, int fd, struct stat * st);
    int (*unlink_p)(void* ctx, const char *path);
    int (*rename_p)(void* ctx, const char *src, const char *dst);
    int (*mkdir_p)(void* ctx, const char* name, mode_t mode);
    int (*rmdir_p)(void* ctx, const char* name);

    NOT IMPLEMENTED:
    DIR* (*opendir_p)(void* ctx, const char* name);
    int (*closedir_p)(void* ctx, DIR* pdir);
    struct dirent* (*readdir_p)(void* ctx, DIR* pdir);
    int (*readdir_r_p)(void* ctx, DIR* pdir, struct dirent* entry, struct dirent** out_dirent);
    long (*telldir_p)(void* ctx, DIR* pdir);
    void (*seekdir_p)(void* ctx, DIR* pdir, long offset);

    int (*access_p)(void* ctx, const char *path, int amode);
    int (*truncate_p)(void* ctx, const char *path, off_t length);

    int (*link_p)(void* ctx, const char* n1, const char* n2);
    int (*fcntl_p)(void* ctx, int fd, int cmd, va_list args);
    int (*ioctl_p)(void* ctx, int fd, int cmd, va_list args);
    int (*fsync_p)(void* ctx, int fd);
*/

/**
 * @brief tnfs DIR structure
 */
typedef struct {
    DIR dir;            /*!< VFS DIR struct */
    tnfs_dir_t d;        /*!< tnfs DIR struct */
    struct dirent e;    /*!< Last open dirent */
    long offset;        /*!< Offset of the current dirent */
    char *path;         /*!< Requested directory name */
} vfs_tnfs_dir_t;


int vfs_tnfs_mkdir(void* ctx, const char* name, mode_t mode)
{
    tnfsMountInfo *mi = (tnfsMountInfo *)ctx;

    // Note that we ignore 'mode'
    int result = tnfs_mkdir(mi, name);
    if(result != TNFS_RESULT_SUCCESS)
    {
        //Debug_printf("vfs_tnfs_mkdir = %d\n", result);
        errno = tnfs_code_to_errno(result);
        return -1;
    }
    errno = 0;
    return 0;
}

int vfs_tnfs_rmdir(void* ctx, const char* name)
{
    tnfsMountInfo *mi = (tnfsMountInfo *)ctx;

    int result = tnfs_rmdir(mi, name);
    if(result != TNFS_RESULT_SUCCESS)
    {
        //Debug_printf("vfs_tnfs_rmdir = %d\n", result);
        errno = tnfs_code_to_errno(result);
        return -1;
    }
    errno = 0;
    return 0;
}

int vfs_tnfs_unlink(void* ctx, const char *path)
{
    tnfsMountInfo *mi = (tnfsMountInfo *)ctx;

    int result = tnfs_unlink(mi, path);
    if(result != TNFS_RESULT_SUCCESS)
    {
        //Debug_printf("vfs_tnfs_unlink = %d\n", result);
        errno = tnfs_code_to_errno(result);
        return -1;
    }
    errno = 0;
    return 0;
}

int vfs_tnfs_rename(void* ctx, const char *src, const char *dst)
{
    tnfsMountInfo *mi = (tnfsMountInfo *)ctx;

    int result = tnfs_rename(mi, src, dst);
    if(result != TNFS_RESULT_SUCCESS)
    {
        //Debug_printf("vfs_tnfs_rename = %d\n", result);
        errno = tnfs_code_to_errno(result);
        return -1;
    }
    errno = 0;
    return 0;
}

int vfs_tnfs_open(void* ctx, const char * path, int flags, int mode)
{
    tnfsMountInfo *mi = (tnfsMountInfo *)ctx;

    int16_t handle;
    // Translate the flags
    uint16_t tflags = 0;
    if(flags == 0)
        tflags = TNFS_OPENMODE_READ;
    else
    {
        tflags |= (flags & O_WRONLY) ? TNFS_OPENMODE_WRITE : 0;
        tflags |= (flags & O_CREAT) ? TNFS_OPENMODE_WRITE_CREATE : 0;
        tflags |= (flags & O_TRUNC) ? TNFS_OPENMODE_WRITE_TRUNCATE : 0;
        tflags |= (flags & O_APPEND) ? TNFS_OPENMODE_WRITE_APPEND : 0;
        tflags |= (flags & O_RDWR) ? TNFS_OPENMODE_READWRITE : 0;
        tflags |= (flags & O_EXCL) ? TNFS_OPENMODE_CREATE_EXCLUSIVE : 0;
    }

    int result = tnfs_open(mi, path, tflags, mode, &handle);
    if(result != TNFS_RESULT_SUCCESS)
    {
        #ifdef DEBUG
        //Debug_printf("vfs_tnfs_open = %d\n", result);
        #endif
        errno = tnfs_code_to_errno(result);
        return -1;
    }
    errno = 0;
    return handle;
}

int vfs_tnfs_close(void* ctx, int fd)
{
    tnfsMountInfo *mi = (tnfsMountInfo *)ctx;

    int result = tnfs_close(mi, fd);
    if(result != TNFS_RESULT_SUCCESS)
    {
        errno = tnfs_code_to_errno(result);
        return -1;
    }
    errno = 0;
    return 0;
}

ssize_t vfs_tnfs_read(void* ctx, int fd, void * dst, size_t size)
{
    tnfsMountInfo *mi = (tnfsMountInfo *)ctx;

    uint16_t readcount;
    int result = tnfs_read(mi, fd, (uint8_t *)dst, size, &readcount);

    if(result == TNFS_RESULT_SUCCESS || (result == TNFS_RESULT_END_OF_FILE && readcount > 0))
    {
        errno = 0;
        return readcount;
    }
    errno = tnfs_code_to_errno(result);

    return -1;
}

ssize_t vfs_tnfs_write(void* ctx, int fd, const void * data, size_t size)
{
    tnfsMountInfo *mi = (tnfsMountInfo *)ctx;

    uint16_t writecount;
    int result = tnfs_write(mi, fd, (uint8_t *)data, size, &writecount);

    if(result != TNFS_RESULT_SUCCESS)
    {
        errno = tnfs_code_to_errno(result);
        return -1;
    }
    errno = 0;
    return writecount;
}

off_t vfs_tnfs_lseek(void* ctx, int fd, off_t size, int mode)
{
    tnfsMountInfo *mi = (tnfsMountInfo *)ctx;

    // Debug_printf("vfs_tnfs_lseek: fd=%d, off=%ld, mod=%d\n", fd, size, mode);
    uint32_t new_pos;
    int result = tnfs_lseek(mi, fd, size, mode, &new_pos);

    if(result != TNFS_RESULT_SUCCESS)
    {
        errno = tnfs_code_to_errno(result);
        return -1;
    }
    errno = 0;
    //Debug_printf("\treturning %u\n", new_pos);
    return new_pos;
}


int vfs_tnfs_stat(void* ctx, const char * path, struct stat * st)
{
    tnfsMountInfo *mi = (tnfsMountInfo *)ctx;

    tnfsStat tstat;

    //Debug_printf("vfs_tnfs_stat: \"%s\"\n", path);

    int result = tnfs_stat(mi, &tstat, path);
    if(result != TNFS_RESULT_SUCCESS)
    {
        errno = tnfs_code_to_errno(result);
        return -1;
    }

    memset(st, 0, sizeof(struct stat));
    st->st_size = tstat.filesize;
    st->st_atime = tstat.a_time;
    st->st_mtime = tstat.m_time;
    st->st_ctime = tstat.c_time;
    st->st_mode = tstat.isDir ? S_IFDIR : S_IFREG;

    errno = 0;
    return 0;
}

int vfs_tnfs_fstat(void* ctx, int fd, struct stat * st)
{
    //Debug_printf("vfs_tnfs_fstat: %d\n", fd);    
    tnfsMountInfo *mi = (tnfsMountInfo *)ctx;

    const char *path = tnfs_filepath(mi, fd);
    return vfs_tnfs_stat(mi, path, st);
}

////////////////////////////////////////////////////////////////////////////////////////////



#ifdef CONFIG_VFS_SUPPORT_DIR
// static int vfs_tnfs_stat(void* ctx, const char * path, struct stat * st) {
//     assert(path);
//     esp_littlefs_t * efs = (esp_littlefs_t *)ctx;
//     struct lfs_info info;
//     int res;

//     memset(st, 0, sizeof(struct stat));
//     st->st_blksize = efs->cfg.block_size;

//     sem_take(efs);
//     res = lfs_stat(efs->fs, path, &info);
//     if (res < 0) {
//         errno = lfs_errno_remap(res);
//         sem_give(efs);
//         /* Not strictly an error, since stat can be used to check
//          * if a file exists */
//         ESP_LOGV(TAG, "Failed to stat path \"%s\". Error %s (%d)",
//                 path, esp_littlefs_errno(res), res);
//         return -1;
//     }
// #if CONFIG_LITTLEFS_USE_MTIME    
//     st->st_mtime = vfs_tnfs_get_mtime(efs, path);
// #endif
//     sem_give(efs);
//     st->st_size = info.size;
//     st->st_mode = ((info.type==LFS_TYPE_REG)?S_IFREG:S_IFDIR);
//     return 0;
// }

// static int vfs_tnfs_unlink(void* ctx, const char *path) {
// #define fail_str_1 "Failed to unlink path \"%s\"."
//     assert(path);
//     esp_littlefs_t * efs = (esp_littlefs_t *)ctx;
//     struct lfs_info info;
//     int res;

//     sem_take(efs);
//     res = lfs_stat(efs->fs, path, &info);
//     if (res < 0) {
//         errno = lfs_errno_remap(res);
//         sem_give(efs);
//         ESP_LOGV(TAG, fail_str_1 " Error %s (%d)",
//                 path, esp_littlefs_errno(res), res);
//         return -1;
//     }

//     if(esp_littlefs_get_fd_by_name(efs, path) >= 0) {
//         sem_give(efs);
//         ESP_LOGE(TAG, fail_str_1 " Has open FD.", path);
//         errno = EBUSY;
//         return -1;
//     }

//     if (info.type == LFS_TYPE_DIR) {
//         sem_give(efs);
//         ESP_LOGV(TAG, "Cannot unlink a directory.");
//         errno = EISDIR;
//         return -1;
//     }

//     res = lfs_remove(efs->fs, path);
//     if (res < 0) {
//         errno = lfs_errno_remap(res);
//         sem_give(efs);
//         ESP_LOGV(TAG, fail_str_1 " Error %s (%d)",
//                 path, esp_littlefs_errno(res), res);
//         return -1;
//     }

//     sem_give(efs);

//     return 0;
// #undef fail_str_1
// }

// static int vfs_tnfs_rename(void* ctx, const char *src, const char *dst) {
//     esp_littlefs_t * efs = (esp_littlefs_t *)ctx;
//     int res;

//     sem_take(efs);

//     if(esp_littlefs_get_fd_by_name(efs, src) >= 0){
//         sem_give(efs);
//         ESP_LOGE(TAG, "Cannot rename; src \"%s\" is open.", src);
//         errno = EBUSY;
//         return -1;
//     }
//     else if(esp_littlefs_get_fd_by_name(efs, dst) >= 0){
//         sem_give(efs);
//         ESP_LOGE(TAG, "Cannot rename; dst \"%s\" is open.", dst);
//         errno = EBUSY;
//         return -1;
//     }

//     res = lfs_rename(efs->fs, src, dst);
//     if (res < 0) {
//         errno = lfs_errno_remap(res);
//         sem_give(efs);
//         ESP_LOGV(TAG, "Failed to rename \"%s\" -> \"%s\". Error %s (%d)",
//                 src, dst, esp_littlefs_errno(res), res);
//         return -1;
//     }

//     sem_give(efs);

//     return 0;
// }

static DIR* vfs_tnfs_opendir(void* ctx, const char* name) {
    //esp_littlefs_t * efs = (esp_littlefs_t *)ctx;
    tnfsMountInfo *mi = (tnfsMountInfo *)ctx;
    int res;
    vfs_tnfs_dir_t *dir = NULL;

    dir = calloc(1, sizeof(vfs_tnfs_dir_t));
    if( dir == NULL ) {
        ESP_LOGE(TAG, "dir struct could not be malloced");
        errno = ENOMEM;
        goto exit;
    }

    dir->path = strdup(name);
    if(dir->path == NULL){
        errno = ENOMEM;
        ESP_LOGE(TAG, "dir path name could not be malloced");
        goto exit;
    }

    sem_take(efs);
    res = lfs_dir_open(efs->fs, &dir->d, dir->path);
    res = tnfs_opendirx(mi->get_filehandleinfo, &dir->d)
    sem_give(efs);
    if (res < 0) {
        errno = lfs_errno_remap(res);
        ESP_LOGV(TAG, "Failed to opendir \"%s\". Error %d", dir->path, res);
        goto exit;
    }

    return (DIR *)dir;

exit:
    esp_littlefs_dir_free(dir);
    return NULL;
}

static int vfs_tnfs_closedir(void* ctx, DIR* pdir) {
    assert(pdir);
    esp_littlefs_t * efs = (esp_littlefs_t *)ctx;
    vfs_tnfs_dir_t * dir = (vfs_tnfs_dir_t *) pdir;
    int res;

    sem_take(efs);
    res = lfs_dir_close(efs->fs, &dir->d);
    sem_give(efs);
    if (res < 0) {
        errno = lfs_errno_remap(res);
        ESP_LOGV(TAG, "Failed to closedir \"%s\". Error %d", dir->path, res);
        return res;
    }

    esp_littlefs_dir_free(dir);
    return 0;
}

static struct dirent* vfs_tnfs_readdir(void* ctx, DIR* pdir) {
    assert(pdir);
    vfs_tnfs_dir_t * dir = (vfs_tnfs_dir_t *) pdir;
    int res;
    struct dirent* out_dirent;

    res = vfs_tnfs_readdir_r(ctx, pdir, &dir->e, &out_dirent);
    if (res != 0) return NULL;
    return out_dirent;
}

// static int vfs_tnfs_readdir_r(void* ctx, DIR* pdir,
//         struct dirent* entry, struct dirent** out_dirent) {
//     assert(pdir);
//     esp_littlefs_t * efs = (esp_littlefs_t *)ctx;
//     vfs_tnfs_dir_t * dir = (vfs_tnfs_dir_t *) pdir;
//     int res;
//     struct lfs_info info = { 0 };

//     sem_take(efs);
//     do{ /* Read until we get a real object name */
//         res = lfs_dir_read(efs->fs, &dir->d, &info);
//     }while( res>0 && (strcmp(info.name, ".") == 0 || strcmp(info.name, "..") == 0));
//     sem_give(efs);
//     if (res < 0) {
//         errno = lfs_errno_remap(res);
// #ifndef CONFIG_LITTLEFS_USE_ONLY_HASH 
//         ESP_LOGV(TAG, "Failed to readdir \"%s\". Error %s (%d)",
//                 dir->path, esp_littlefs_errno(res), res);
// #else
//         ESP_LOGV(TAG, "Failed to readdir \"%s\". Error %d", dir->path, res);
// #endif
//         return -1;
//     }

//     if(info.type == LFS_TYPE_REG) {
//         ESP_LOGV(TAG, "readdir a file of size %d named \"%s\"",
//                 info.size, info.name);
//     }
//     else {
//         ESP_LOGV(TAG, "readdir a dir named \"%s\"", info.name);
//     }

//     if(res == 0) {
//         /* End of Objs */
//         ESP_LOGV(TAG, "Reached the end of the directory.");
//         *out_dirent = NULL;
//     }
//     else {
//         entry->d_ino = 0;
//         entry->d_type = info.type == LFS_TYPE_REG ? DT_REG : DT_DIR;
//         strncpy(entry->d_name, info.name, sizeof(entry->d_name));
//         *out_dirent = entry;
//     }
//     dir->offset++;

//     return 0;
// }

// // static long vfs_tnfs_telldir(void* ctx, DIR* pdir) {
// //     assert(pdir);
// //     vfs_tnfs_dir_t * dir = (vfs_tnfs_dir_t *) pdir;
// //     return dir->offset;
// // }

// // static void vfs_tnfs_seekdir(void* ctx, DIR* pdir, long offset) {
// //     assert(pdir);
// //     esp_littlefs_t * efs = (esp_littlefs_t *)ctx;
// //     vfs_tnfs_dir_t * dir = (vfs_tnfs_dir_t *) pdir;
// //     int res;

// //     if (offset < dir->offset) {
// //         /* close and re-open dir to rewind to beginning */
// //         sem_take(efs);
// //         res = lfs_dir_rewind(efs->fs, &dir->d);
// //         sem_give(efs);
// //         if (res < 0) {
// //             errno = lfs_errno_remap(res);
// //             ESP_LOGV(TAG, "Failed to rewind dir \"%s\". Error %s (%d)",
// //                     dir->path, esp_littlefs_errno(res), res);
// //             return;
// //         }
// //         dir->offset = 0;
// //     }

// //     while(dir->offset < offset){
// //         struct dirent *out_dirent;
// //         res = vfs_tnfs_readdir_r(ctx, pdir, &dir->e, &out_dirent);
// //         if( res != 0 ){
// //             ESP_LOGE(TAG, "Error readdir_r");
// //             return;
// //         }
// //     }
// // }

// // static int vfs_tnfs_mkdir(void* ctx, const char* name, mode_t mode) {
// //     /* Note: mode is currently unused */
// //     esp_littlefs_t * efs = (esp_littlefs_t *)ctx;
// //     int res;
// //     ESP_LOGV(TAG, "mkdir \"%s\"", name);

// //     sem_take(efs);
// //     res = lfs_mkdir(efs->fs, name);
// //     sem_give(efs);
// //     if (res < 0) {
// //         errno = lfs_errno_remap(res);
// //         ESP_LOGV(TAG, "Failed to mkdir \"%s\". Error %s (%d)",
// //                 name, esp_littlefs_errno(res), res);
// //         return -1;
// //     }
// //     return 0;
// // }

// // static int vfs_tnfs_rmdir(void* ctx, const char* name) {
// //     esp_littlefs_t * efs = (esp_littlefs_t *)ctx;
// //     struct lfs_info info;
// //     int res;

// //     /* Error Checking */
// //     sem_take(efs);
// //     res = lfs_stat(efs->fs, name, &info);
// //     if (res < 0) {
// //         errno = lfs_errno_remap(res);
// //         sem_give(efs);
// //         ESP_LOGV(TAG, "\"%s\" doesn't exist.", name);
// //         return -1;
// //     }

// //     if (info.type != LFS_TYPE_DIR) {
// //         sem_give(efs);
// //         ESP_LOGV(TAG, "\"%s\" is not a directory.", name);
// //         errno = ENOTDIR;
// //         return -1;
// //     }

// //     /* Unlink the dir */
// //     res = lfs_remove(efs->fs, name);
// //     sem_give(efs);
// //     if ( res < 0) {
// //         errno = lfs_errno_remap(res);
// //         ESP_LOGV(TAG, "Failed to unlink path \"%s\". Error %s (%d)",
// //                 name, esp_littlefs_errno(res), res);
// //         return -1;
// //     }

// //     return 0;
// // }

// static ssize_t vfs_tnfs_truncate( void *ctx, const char *path, off_t size )
// {
//     esp_littlefs_t * efs = (esp_littlefs_t *)ctx;
//     ssize_t res = -1;
//     vfs_tnfs_file_t *file = NULL;

//     int fd = vfs_tnfs_open( ctx, path, LFS_O_RDWR, 438 );

//     sem_take(efs);
//     if((uint32_t)fd > efs->cache_size)
//     {
//         sem_give(efs);
//         ESP_LOGE(TAG, "FD %d must be <%d.", fd, efs->cache_size);
//         errno = EBADF;
//         return -1;
//     }
//     else
//     {
//         file = efs->cache[fd];
//         res = lfs_file_truncate( efs->fs, &file->file, size );
//         sem_give(efs);

//         if(res < 0)
//         {
//             errno = lfs_errno_remap(res);
//     #ifndef CONFIG_LITTLEFS_USE_ONLY_HASH
//             ESP_LOGV(TAG, "Failed to truncate file \"%s\". Error %s (%d)",
//                     file->path, esp_littlefs_errno(res), res);
//     #else
//             ESP_LOGV(TAG, "Failed to truncate FD %d. Error %s (%d)",
//                     fd, esp_littlefs_errno(res), res);
//     #endif
//             res = -1;
//         }
//         else
//         {
//             ESP_LOGV( TAG, "Truncated file %s to %u bytes", path, (uint32_t) size );
//         }
//     }
//     vfs_tnfs_close( ctx, fd );
//     return res;
// }
#endif //CONFIG_VFS_SUPPORT_DIR



////////////////////////////////////////////////////////////////////////////////////////////


// Register our functions and use tnfsMountInfo as our context
// New basepath will be stored in basepath
esp_err_t vfs_tnfs_register(tnfsMountInfo &m_info, char *basepath, int basepathlen)
{
    // Trying to initialze the struct as coumented (e.g. ".write_p = &function")
    // results in compiloer error "non-trivial desginated initializers not supported"
    esp_vfs_t vfs;
    memset(&vfs, 0, sizeof(vfs));
    const esp_vfs_t vfs = {
        .flags       = ESP_VFS_FLAG_CONTEXT_PTR,
        .write_p     = &vfs_tnfs_write,
        .pwrite_p    = NULL, // &vfs_littlefs_pwrite,
        .lseek_p     = &vfs_tnfs_lseek,
        .read_p      = &vfs_tnfs_read,
        .pread_p     = NULL, // &vfs_littlefs_pread,
        .open_p      = &vfs_tnfs_open,
        .close_p     = &vfs_tnfs_close,
        .fstat_p     = &vfs_tnfs_fstat,

#ifdef CONFIG_VFS_SUPPORT_DIR
        .stat_p      = NULL, // &vfs_tnfs_stat,
        .link_p      = NULL, /* Not Supported */
        .unlink_p    = NULL, // &vfs_tnfs_unlink,
        .rename_p    = NULL, // &vfs_tnfs_rename,
        .opendir_p   = &vfs_tnfs_opendir,
        .closedir_p  = &vfs_tnfs_closedir,
        .readdir_p   = &vfs_tnfs_readdir,
        .readdir_r_p = NULL, // &vfs_tnfs_readdir_r,
        .seekdir_p   = NULL, // &vfs_tnfs_seekdir,
        .telldir_p   = NULL, // &vfs_tnfs_telldir,
        .mkdir_p     = NULL, // &vfs_tnfs_mkdir,
        .rmdir_p     = NULL, // &vfs_tnfs_rmdir,
        .fsync_p     = NULL, // &vfs_tnfs_fsync,
		.truncate_p  = NULL, // &vfs_tnfs_truncate,
        .utime_p     = NULL,
#endif
    };

    // We'll use the address of our tnfsMountInfo to provide a unique base path
    // for this instance wihtout keeping track of how many we create
    snprintf(basepath, basepathlen, "/tnfs%p", &m_info);
    esp_err_t e = esp_vfs_register(basepath, &vfs, &m_info);

    Debug_printf("vfs_tnfs_register \"%s\" @ %p = %d \"%s\"\n", basepath, &m_info, e, esp_err_to_name(e));
    
    return e;
}

// Remove our driver from VFS
esp_err_t vfs_tnfs_unregister(const char * basepath)
{
    esp_err_t e = esp_vfs_unregister(basepath);

    #ifdef DEBUG
    Debug_printf("vfs_tnfs_unregister \"%s\" = %d \"%s\"\n", basepath, e, esp_err_to_name(e));
    #endif
    return e;
}
