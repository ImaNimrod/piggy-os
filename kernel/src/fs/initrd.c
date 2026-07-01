#include <errno.h>
#include <fs/initrd.h>
#include <fs/vfs.h>
#include <mem/pmm.h>
#include <mem/paging.h>
#include <mem/slab.h>
#include <pdgzip.h>
#include <utils/log.h>
#include <utils/macros.h>
#include <utils/string.h>

#define TAR_BLOCK_SIZE 512

#define TAR_FILE_TYPE_NORMAL        '\0'
#define TAR_FILE_TYPE_ANORMAL       '0'
#define TAR_FILE_TYPE_HARD_LINK     '1'
#define TAR_FILE_TYPE_SYMLINK       '2'
#define TAR_FILE_TYPE_CHAR_DEV      '3'
#define TAR_FILE_TYPE_BLOCK_DEV     '4'
#define TAR_FILE_TYPE_DIRECTORY     '5'
#define TAR_FILE_TYPE_FIFO          '6'
#define TAR_FILE_TYPE_GNU_LONG_PATH 'L'

struct gzip_source {
    const uint8_t* data;
    size_t size;
    size_t offset;
};

struct tar_header {
    char name[100];
    char mode[8];
    char uid[8];
    char gid[8];
    char size[12];
    char mtime[12];
    char checksum[8];
    char type;
    char linkname[100];
    char magic[6];
    char version[2];
    char owner[32];
    char group[32];
    char devmajor[8];
    char devminor[8];
    char prefix[155];
    char padding[12];
};

static inline uint64_t oct2int(const char* str, size_t len) {
    uint64_t value = 0;
    while (*str != '\0' && len > 0) {
        value = value * 8 + (*str++ - '0');
        len--;
    }
    return value;
}

static size_t gzip_read(void* user, void* buf, size_t count) {
    struct gzip_source* src = user;

    size_t remaining = src->size - src->offset;
    if (remaining > count) {
        remaining = count;
    }

    memcpy(buf, src->data + src->offset, remaining);
    src->offset += remaining;

    return remaining;
}

static int gz_read_exact(pdgzip_t* gz, void* buf, size_t count) {
    uint8_t* ptr = buf;

    while (count > 0) {
        int64_t ret = pdgzip_read(gz, ptr, count);
        if (ret <= 0) {
            return -EIO;
        }

        ptr += ret;
        count -= ret;
    }

    return 0;
}

static int gz_skip(pdgzip_t* gz, size_t count) {
    char buffer[TAR_BLOCK_SIZE];

    while (count > 0) {
        size_t chunk = MIN(count, sizeof(buffer));
        if (gz_read_exact(gz, buffer, chunk) < 0) {
            return -EIO;
        }

        count -= chunk;
    }

    return 0;
}

void initrd_unpack(struct limine_file* initrd_module) {
    klog("[initrd] unpacking initial ramdisk at 0x%lx (size: %zuMiB)\n",
            (uintptr_t) initrd_module->address, initrd_module->size >> 20);

    struct gzip_source source = {
        .data = initrd_module->address,
        .size = initrd_module->size,
        .offset = 0
    };

    size_t gzip_state_size = pdgzip_state_size();
    size_t gzip_state_page_count = DIV_CEIL(gzip_state_size, PAGE_SIZE_4KB);

    uintptr_t gzip_state_paddr = pmm_alloc(gzip_state_page_count);

    pdgzip_cfg_t cfg = {
        .read = gzip_read,
        .user = &source,
        .concat = 0,
    };

    pdgzip_t* gz = pdgzip_init((void*) (gzip_state_paddr + HIGH_VMA), &cfg);
    if (unlikely(!gz)) {
        kpanic(NULL, false, "failed to initialize pdgzip");
    }

    size_t file_count = 0;
    char* long_name = NULL;

    struct tar_header header;

    for (;;) {
        if (gz_read_exact(gz, &header, sizeof(header)) < 0) {
            break;
        }

        bool empty = true;

        for (size_t i = 0; i < sizeof(header); i++) {
            if (((uint8_t*) &header)[i] != 0) {
                empty = false;
                break;
            }
        }

        if (empty) {
            break;
        }

        if (strncmp(header.magic, "ustar", 5) != 0) {
            klog("[initrd] invalid TAR header\n");
            break;
        }

        char* name = long_name ? long_name : header.name;
        off_t size = oct2int(header.size, sizeof(header.size));

        ssize_t error = 0;
        size_t padding = 0;

        struct vfs_node* node = NULL;

        switch (header.type) {
            case TAR_FILE_TYPE_NORMAL:
            case TAR_FILE_TYPE_ANORMAL:
                error = vfs_create(vfs_root, name, VFS_TYPE_REGULAR, &node);
                if (error < 0) {
                    gz_skip(gz, ALIGN_UP(size, TAR_BLOCK_SIZE));
                    break;
                }

                char buffer[PAGE_SIZE_4KB];
                off_t offset = 0;

                while (offset < size) {
                    size_t chunk = MIN(size - offset, (off_t) sizeof(buffer));

                    error = gz_read_exact(gz, buffer, chunk);
                    if (error < 0) {
                        break;
                    }

                    offset += chunk;

                    error = node->ops->write(node, buffer, chunk, offset - chunk, 0);
                    if (error < 0) {
                        break;
                    }
                }

                if (offset < size) {
                    gz_skip(gz, size - offset);
                }

                padding = ALIGN_UP(size, TAR_BLOCK_SIZE) - size;
                if (padding > 0) {
                    gz_skip(gz, padding);
                }

                break;
            case TAR_FILE_TYPE_HARD_LINK:
                error = vfs_link(vfs_root, header.linkname, vfs_root, name);
                break;
            case TAR_FILE_TYPE_SYMLINK:
                error = vfs_symlink(vfs_root, name, header.linkname);
                break;
            case TAR_FILE_TYPE_DIRECTORY:
                error = vfs_create(vfs_root, name, VFS_TYPE_DIRECTORY, &node);
                break;
            case TAR_FILE_TYPE_GNU_LONG_PATH:
                if (long_name) {
                    kfree(long_name);
                    long_name = NULL;
                }

                long_name = kmalloc(size + 1);
                if (unlikely(!long_name)) {
                    kpanic(NULL, false, "failed to allocate memory for TAR long filename");
                }

                if ((error = gz_read_exact(gz, long_name, size)) < 0) {
                    break;
                }

                long_name[size] = '\0';

                padding = ALIGN_UP(size, TAR_BLOCK_SIZE) - size;
                if (padding > 0) {
                    gz_skip(gz, padding);
                }

                continue;
            default:
                klog("[initrd] unsupported file type '%c' (%d): %s\n", header.type, header.type, name);
                gz_skip(gz, ALIGN_UP(size, TAR_BLOCK_SIZE));
                error = -EINVAL;
                break;
        }

        if (error < 0) {
            klog("[initrd] failed to unpack '%s': %d\n", name, error);
        } else if (node) {
            time_t mtime = oct2int(header.mtime, sizeof(header.mtime));
            struct timespec timestamp = { .tv_sec = mtime, .tv_nsec = 0 };

            struct stat stat = { .st_atim = timestamp, .st_mtim = timestamp, .st_ctim = timestamp };
            error = node->ops->setstat(node, &stat, VFS_STAT_ST_ATIM | VFS_STAT_ST_MTIM | VFS_STAT_ST_CTIM);

            node->ops->unlock(node);
            VFS_NODE_UNREF(node);
        }

        if (long_name) {
            kfree(long_name);
            long_name = NULL;
        }

        file_count++;
    }

    if (long_name) {
        kfree(long_name);
    }

    pmm_free(gzip_state_paddr, gzip_state_page_count);
    pmm_free((uintptr_t) initrd_module->address - HIGH_VMA, DIV_CEIL(initrd_module->size, PAGE_SIZE_4KB));

    klog("[initrd] finished unpacking %zu files from initial ramdisk\n", file_count);
}
