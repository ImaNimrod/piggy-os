#include <fs/initrd.h>
#include <fs/vfs.h>
#include <fs/tmpfs.h>
#include <limine.h>
#include <mem/pmm.h>
#include <mem/vmm.h>
#include <types.h>
#include <utils/log.h>
#include <utils/macros.h>
#include <utils/panic.h>
#include <utils/string.h>

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

static volatile struct limine_module_request module_request = {
    .id = LIMINE_MODULE_REQUEST,
    .revision = 0
};

static inline uint64_t oct2int(const char* str, size_t len) {
    uint64_t value = 0;
    while (*str != '\0' && len > 0) {
        value = value * 8 + (*str++ - '0');
        len--;
    }
    return value;
}

UNMAP_AFTER_INIT bool initrd_unpack(void) {
    struct limine_module_response* module_response = module_request.response;
    struct limine_file* initrd_module = NULL;

    for (size_t i = 0; i < module_response->module_count; i++) {
        if (!strcmp(module_response->modules[i]->path, "/boot/initrd.tar")) {
            initrd_module = module_response->modules[i];
            break;
        }
    }

    if (unlikely(initrd_module == NULL)) {
        return false;
    }

    klog("[initrd] started unpacking initrd module at 0x%x (size: %dKiB)\n",
            (uintptr_t) initrd_module, initrd_module->size >> 10);

    size_t file_count = 0;
    struct tar_header* current_file = (struct tar_header*) initrd_module->address;
    char* name_override = NULL;

    while (!strncmp(current_file->magic, "ustar", 5)) {
        char* name = current_file->name;
        if (name_override != NULL) {
            name = name_override;
            name_override = NULL;
        }

        if (!strcmp(name, "./")) {
            continue;
        }

        size_t size = oct2int(current_file->size, sizeof(current_file->size));
        size_t mtime = oct2int(current_file->mtime, sizeof(current_file->mtime));

        struct vfs_node* node = NULL;

        switch (current_file->type) {
            case TAR_FILE_TYPE_NORMAL:
                node = vfs_create(vfs_root, name, S_IFREG);
                if (node == NULL) {
                    kpanic(NULL, true, "failed to allocate initrd node for file `%s`", name);
                }

                if (node->write(node, (void*) ((uintptr_t) current_file + 512), 0, size, 0) != (ssize_t) size) {
                    kpanic(NULL, true, "failed to write initrd data to file `%s`", name);
                }

                break;
            case TAR_FILE_TYPE_DIRECTORY:
                node = vfs_create(vfs_root, name, S_IFDIR);
                if (node == NULL) {
                    kpanic(NULL, true, "failed to allocate initrd node for directory `%s`", name);
                }

                break;
            case TAR_FILE_TYPE_GNU_LONG_PATH:
                name_override = (char*) ((uintptr_t) current_file + 512);
                name_override[size] = '\0';
                break;
        }

        if (node != NULL) {
            node->stat.st_blksize = 512;
            node->stat.st_blocks = DIV_CEIL(size, node->stat.st_blksize);

            struct timespec timestamp = {
                .tv_sec = mtime,
                .tv_nsec = 0,
            };
            node->stat.st_atim = node->stat.st_mtim = node->stat.st_ctim = timestamp;

            file_count++;
        }

        pmm_free((uintptr_t) current_file - HIGH_VMA, (512 + ALIGN_UP(size, 512)) / PAGE_SIZE);
        current_file = (struct tar_header*) ((uintptr_t) current_file + 512 + ALIGN_UP(size, 512));
    }

    klog("[initrd] finished unpacking %u files\n", file_count);
    return true;
}
