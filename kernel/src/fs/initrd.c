#include <errno.h>
#include <fs/initrd.h>
#include <fs/vfs.h>
#include <mem/pmm.h>
#include <mem/paging.h>
#include <types.h>
#include <utils/log.h>
#include <utils/macros.h>
#include <utils/string.h>

#define TAR_BLOCK_SIZE 512

#define TAR_FILE_TYPE_NORMAL        '0'
#define TAR_FILE_TYPE_HARD_LINK     '1'
#define TAR_FILE_TYPE_SYMLINK       '2'
#define TAR_FILE_TYPE_CHAR_DEV      '3'
#define TAR_FILE_TYPE_BLOCK_DEV     '4'
#define TAR_FILE_TYPE_DIRECTORY     '5'
#define TAR_FILE_TYPE_FIFO          '6'
#define TAR_FILE_TYPE_GNU_LONG_PATH 'L'

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
};

static inline uint64_t oct2int(const char* str, size_t len) {
    uint64_t value = 0;
    while (*str != '\0' && len > 0) {
        value = value * 8 + (*str++ - '0');
        len--;
    }
    return value;
}

void initrd_unpack(struct limine_file* initrd_module) {
    klog("[initrd] unpacking initial ramdisk at 0x%lx (size: %zuMiB)\n",
            (uintptr_t) initrd_module->address, initrd_module->size >> 20);

    struct tar_header* current_file = (struct tar_header*) initrd_module->address;
    size_t file_count = 0;
    char* name_override = NULL;

    while (strncmp(current_file->magic, "ustar", 5) == 0) {
        char* name = current_file->name;
        if (name_override != NULL) {
            name = name_override;
            name_override = NULL;
        }

        if (strcmp(name, "./") == 0) {
            continue;
        }

        off_t size = oct2int(current_file->size, sizeof(current_file->size));

        ssize_t error = 0;
        struct vfs_node* node = NULL;

        switch (current_file->type) {
            case TAR_FILE_TYPE_NORMAL:
                error = vfs_create(vfs_root, name, VFS_TYPE_REGULAR, &node);
                if (error < 0) {
                    break;
                }

                error = node->ops->write(node, (const void*) ((uintptr_t) current_file + TAR_BLOCK_SIZE), size, 0, 0);
                break;
            case TAR_FILE_TYPE_DIRECTORY:
                error = vfs_create(vfs_root, name, VFS_TYPE_DIRECTORY, &node);
                break;
            case TAR_FILE_TYPE_GNU_LONG_PATH:
                name_override = (char*) ((uintptr_t) current_file + TAR_BLOCK_SIZE);
                name_override[size] = '\0';
                break;
            default:
                klog("[initrd] file '%s' is an unsupported file type (%d)\n", name, current_file->type);
                error = -EINVAL;
                break;
        }

        if (error < 0) {
            klog("[initrd] failed to unpack file '%s': %d\n", name, error);
        } else {
            time_t mtime = oct2int(current_file->mtime, sizeof(current_file->mtime));
            struct timespec timestamp = { .tv_sec = mtime, .tv_nsec = 0 };

            struct stat stat = { .st_atim = timestamp, .st_mtim = timestamp, .st_ctim = timestamp };
            error = node->ops->setstat(node, &stat, VFS_STAT_ST_ATIM | VFS_STAT_ST_MTIM | VFS_STAT_ST_CTIM);

            node->ops->unlock(node);
            VFS_NODE_UNREF(node);
        }

        file_count++;

        current_file = (struct tar_header*) ((uintptr_t) current_file + TAR_BLOCK_SIZE + ALIGN_UP(size, TAR_BLOCK_SIZE));
    }

    pmm_free((uintptr_t) initrd_module->address - HIGH_VMA, DIV_CEIL(initrd_module->size, PAGE_SIZE_4KB));
    klog("[initrd] finished unpacking %zu files from initial ramdisk\n", file_count);
}
