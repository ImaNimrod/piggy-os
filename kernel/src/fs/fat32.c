#include <errno.h>
#include <fs/vfs.h>
#include <mem/paging.h>
#include <mem/pmm.h>
#include <mem/slab.h>
#include <utils/hashmap.h>
#include <utils/log.h>
#include <utils/macros.h>
#include <utils/mutex.h>
#include <utils/string.h>
#include <utils/usercopy.h>

#define FAT32_ATTR_READ_ONLY    (1 << 0)
#define FAT32_ATTR_HIDDEN       (1 << 1)
#define FAT32_ATTR_SYSTEM       (1 << 2)
#define FAT32_ATTR_VOLUME_ID    (1 << 3)
#define FAT32_ATTR_DIRECTORY    (1 << 4)
#define FAT32_ATTR_ARCHIVE      (1 << 5)
#define FAT32_ATTR_LFN          0x0f

#define FAT32_CLUSTER_FREE  0x00000000
#define FAT32_CLUSTER_BAD   0x0ffffff7
#define FAT32_CLUSTER_END   0x0ffffff8
#define FAT32_CLUSTER_MASK  0x0fffffff

#define FSINFO_LEAD_SIG     0x41615252
#define FSINFO_STRUCT_SIG   0x61417272
#define FSINFO_TRAIL_SIG    0xaa550000

struct fat32_bpb {
    uint8_t jump[3];
    char oem[8];
    uint16_t bytes_per_sector;
    uint8_t sectors_per_cluster;
    uint16_t reserved_sector_count;
    uint8_t fat_count;
    uint16_t root_entry_count;
    uint16_t total_sectors_16;
    uint8_t media;
    uint16_t fat_size_16;
    uint16_t sectors_per_track;
    uint16_t num_heads;
    uint32_t hidden_sectors;
    uint32_t total_sectors_32;
    uint32_t fat_size_32;
    uint16_t extended_flags;
    uint16_t version;
    uint32_t root_cluster;
    uint16_t fsinfo_sector;
    uint16_t bootsect;
    uint8_t reserved[12];
    uint8_t drive_num;
    uint8_t reserved2;
    uint8_t signature;
    uint32_t serial_number;
    uint8_t label[11];
    uint8_t type[8];
} __attribute__((packed));

struct fat32_dirent {
    uint8_t name[11];
    uint8_t attributes;
    uint8_t reserved;
    uint8_t create_time_tenth;
    uint16_t creation_time;
    uint16_t creation_date;
    uint16_t access_date;
    uint16_t cluster_high;
    uint16_t last_mod_time;
    uint16_t last_mod_date;
    uint16_t cluster_low;
    uint32_t size;
} __attribute__((packed));

struct fat32_fsinfo {
    uint32_t lead_sig;
    uint8_t  reserved1[480];
    uint32_t struct_sig;
    uint32_t free_count;
    uint32_t next_free;
    uint8_t  reserved2[12];
    uint32_t trail_sig;
};

struct fat32_filesystem {
    struct vfs_filesystem;

    struct vfs_node* backing;

    uint32_t root_cluster;
    uint32_t cluster_size;
    uint32_t fat_offset;
    uint32_t fat_size;
    uint32_t data_offset;

    hashmap_t* node_map;
    mutex_t node_map_mutex;

    mutex_t root_mutex;
};

struct fat32_node {
    struct vfs_node;

    uint32_t cluster;
    uint32_t size;

    size_t dirent_disk_offset;

    struct vfs_node* parent_dir;

    mutex_t mutex;
};

static struct slab_cache* fat32_node_cache;

static int fat32_mount(struct vfs_node* backing, struct vfs_node* target, struct vfs_filesystem** result);
static int fat32_root(struct vfs_filesystem* filesystem, struct vfs_node** result);

static struct vfs_ops fat32_ops = {
    .mount = fat32_mount,
    .root = fat32_root,
};

static int fat32_parent(struct vfs_node* node, struct vfs_node** result);
static int fat32_create(struct vfs_node* parent, const char* name, vfs_type_t type, struct vfs_node** result);
static int fat32_lookup(struct vfs_node* parent, const char* name, struct vfs_node** result);
static int fat32_rename(struct vfs_node* src_dir, struct vfs_node* src, const char* old_name, struct vfs_node* target_dir, const char* new_name);
static int fat32_link(struct vfs_node* dir, const char* name, struct vfs_node* node);
static int fat32_symlink(struct vfs_node* dir, const char* name, const char* target);
static ssize_t fat32_readlink(struct vfs_node* node, char* buf, size_t length);
static int fat32_unlink(struct vfs_node* parent, struct vfs_node* child, const char* name);
static ssize_t fat32_read(struct vfs_node* node, void* buf, size_t count, off_t offset, int flags);
static ssize_t fat32_write(struct vfs_node* node, const void* buf, size_t count, off_t offset, int flags);
static int fat32_ioctl(struct vfs_node* node, int request, void* argp);
static int fat32_truncate(struct vfs_node* node, off_t length);
static short fat32_poll(struct vfs_node* node, short events, struct poll_table* pt);
static int fat32_sync(struct vfs_node* node);
static int fat32_mmap(struct vfs_node* node, void* addr, off_t offset, int flags, uint64_t pte_flags);
static int fat32_munmap(struct vfs_node* node, void* addr, off_t offset);
static ssize_t fat32_getdents(struct vfs_node* node, struct dirent* buf, size_t count, off_t offset);
static int fat32_getstat(struct vfs_node* node, struct stat* stat);
static int fat32_setstat(struct vfs_node* node, const struct stat* stat, int flags);
static int fat32_lock(struct vfs_node* node);
static int fat32_unlock(struct vfs_node* node);
static void fat32_inactive(struct vfs_node* node);

static struct vfs_node_ops fat32_node_ops = {
    .parent = fat32_parent,
    .create = fat32_create,
    .lookup = fat32_lookup,
    .rename = fat32_rename,
    .link = fat32_link,
    .symlink = fat32_symlink,
    .readlink = fat32_readlink,
    .unlink = fat32_unlink,
    .read = fat32_read,
    .write = fat32_write,
    .ioctl = fat32_ioctl,
    .truncate = fat32_truncate,
    .poll = fat32_poll,
    .sync = fat32_sync,
    .mmap = fat32_mmap,
    .munmap = fat32_munmap,
    .getdents = fat32_getdents,
    .getstat = fat32_getstat,
    .setstat = fat32_setstat,
    .lock = fat32_lock,
    .unlock = fat32_unlock,
    .inactive = fat32_inactive,
};

static inline uint64_t cluster_offset(struct fat32_filesystem* fatfs, uint32_t cluster) {
    return fatfs->data_offset + ((uint64_t) (cluster - 2) * fatfs->cluster_size);
}

static inline bool is_leap_year(int year) {
    return !(year % 4) && ((year % 100) || !(year % 400));
}

static int read_cluster(struct fat32_filesystem* fatfs, uint32_t cluster, void* buf);
static int next_cluster(struct fat32_filesystem* fatfs, uint32_t current, uint32_t* next);
static void sfn_to_cstr(struct fat32_dirent* dirent, char* out);

static struct fat32_node* create_node(struct vfs_filesystem* filesystem, vfs_type_t type) {
    struct fat32_node* node = slab_cache_alloc(fat32_node_cache);
    if (unlikely(!node)) {
        return NULL;
    }

    node->type = type;
    node->ops = &fat32_node_ops;
    node->filesystem = filesystem;
    node->refcount = 1;

    mutex_init(&node->mutex);

    return node;
}

static int cluster_chain_length(struct fat32_filesystem* fatfs, uint32_t cluster, size_t* chain_length) {
    size_t count = 0;

    uint32_t current = cluster;

    while (current < FAT32_CLUSTER_END) {
        if (current == FAT32_CLUSTER_FREE || current == FAT32_CLUSTER_BAD) {
            return -EIO;
        }

        count++;

        uint32_t next;

        int ret = next_cluster(fatfs, current, &next);
        if (ret < 0) {
            return ret;
        }

        if (next >= FAT32_CLUSTER_END) {
            break;
        }

        current = next;
    }

    *chain_length = count;
    return 0;
}

static time_t days_before_month(int year, int month) {
    static const int days[12] = {
        0, 31, 59, 90, 120, 151,
        181, 212, 243, 273, 304, 334,
    };

    int result = days[month];

    if (month >= 2 && is_leap_year(year)) {
        result++;
    }

    return result;
}

static time_t days_before_year(int year) {
    int y = year - 1;
    return (365 * (year - 1970)) + ((y / 4) - (1969 / 4)) - ((y / 100) - (1969 / 100)) + ((y / 400) - (1969 / 400));
}

static int directory_lookup(struct fat32_filesystem* fatfs, struct fat32_node* directory, const char* name, struct fat32_dirent* dirent, size_t* dirent_disk_offset) {
    uint32_t cluster = directory->cluster;
    uint32_t cluster_size = fatfs->cluster_size;
    uint32_t entries_per_cluster = cluster_size / sizeof(struct fat32_dirent);

    while (cluster < FAT32_CLUSTER_END) {
        size_t cluster_buf_pages = DIV_CEIL(fatfs->cluster_size, PAGE_SIZE_4KB);
        uintptr_t cluster_buf_paddr = pmm_alloc(cluster_buf_pages);
        void* cluster_buf = (void*) (cluster_buf_paddr + HIGH_VMA);

        int ret = read_cluster(fatfs, cluster, cluster_buf);
        if (ret < 0) {
            pmm_free(cluster_buf_paddr, cluster_buf_pages);
            return ret;
        }

        struct fat32_dirent* entries = (struct fat32_dirent*) cluster_buf;

        uint64_t cluster_base = cluster_offset(fatfs, cluster);

        for (uint32_t i = 0; i < entries_per_cluster; i++) {
            struct fat32_dirent* e = &entries[i];

            uint8_t first = e->name[0];
            if (first == 0x00) {
                pmm_free(cluster_buf_paddr, cluster_buf_pages);
                return -ENOENT;
            }

            if (first == 0xe5) {
                continue;
            }

            if (e->attributes == FAT32_ATTR_LFN) {
                continue;
            }

            char converted[13];
            sfn_to_cstr(e, converted);

            if (strcmp(converted, name) != 0) {
                continue;
            }

            memcpy(dirent, e, sizeof(struct fat32_dirent));
            *dirent_disk_offset = cluster_base + (i * sizeof(struct fat32_dirent));

            pmm_free(cluster_buf_paddr, cluster_buf_pages);
            return 0;
        }

        pmm_free(cluster_buf_paddr, cluster_buf_pages);

        ret = next_cluster(fatfs, cluster, &cluster);
        if (ret < 0) {
            return ret;
        }
    }

    return -ENOENT;
}

static struct timespec dirent_datetime_to_timespec(uint16_t fat_date, uint16_t fat_time, uint8_t time_tenth) {
    int day = fat_date & 0x1f;
    int month = ((fat_date >> 5) & 0x0f);
    int year = 1980 + ((fat_date >> 9) & 0x7f);

    int second = (fat_time & 0x1f) * 2;
    int minute = (fat_time >> 5) & 0x3f;
    int hour = (fat_time >> 11) & 0x1f;

    time_t days = days_before_year(year) + days_before_month(year, month - 1) + (day - 1);
    time_t total_seconds = (days * 86400) + (hour * 3600) + (minute * 60) + second;

    return (struct timespec) { total_seconds, time_tenth * 10000000L };
}

static bool is_valid_sfn(struct fat32_dirent* dirent) {
    if (dirent->name[0] == 0x00 || dirent->name[0] == 0xe5) {
        return false;
    }

    if ((dirent->attributes & FAT32_ATTR_VOLUME_ID) || dirent->attributes == FAT32_ATTR_LFN) {
        return false;
    }

    bool basename_nonspace = false;

    for (int i = 0; i < 8; i++) {
        if (dirent->name[i] != ' ') {
            basename_nonspace = true;
            break;
        }
    }

    return basename_nonspace;
}

static int next_cluster(struct fat32_filesystem* fatfs, uint32_t current, uint32_t* next) {
    uint32_t value;

    fatfs->backing->ops->lock(fatfs->backing);
    ssize_t ret = fatfs->backing->ops->read(fatfs->backing, &value, sizeof(uint32_t), fatfs->fat_offset + ((uint64_t) current * sizeof(uint32_t)), 0);
    fatfs->backing->ops->unlock(fatfs->backing);

    if (ret < 0) {
        return ret;
    }

    if (ret != sizeof(uint32_t)) {
        return -EIO;
    }

    value &= FAT32_CLUSTER_MASK;

    *next = value;
    return 0;
}

static int read_cluster(struct fat32_filesystem* fatfs, uint32_t cluster, void* buf) {
    fatfs->backing->ops->lock(fatfs->backing);
    ssize_t ret = fatfs->backing->ops->read(fatfs->backing, buf, fatfs->cluster_size, cluster_offset(fatfs, cluster), 0);
    fatfs->backing->ops->unlock(fatfs->backing);

    if (ret < 0) {
        return ret;
    }

    if ((uint32_t) ret != fatfs->cluster_size) {
        return -EIO;
    }

    return 0;
}

static void sfn_to_cstr(struct fat32_dirent* dirent, char* out) {
    size_t pos = 0;

    for (int i = 0; i < 8; i++) {
        char c = dirent->name[i];

        if (c == ' ') {
            break;
        }

        if (c >= 'A' && c <= 'Z') {
            c += 32;
        }

        out[pos++] = c;
    }

    if (dirent->name[8] != ' ') {
        out[pos++] = '.';

        for (int i = 8; i < 11; i++) {
            char c = dirent->name[i];

            if (c == ' ') {
                break;
            }

            if (c >= 'A' && c <= 'Z') {
                c += 32;
            }

            out[pos++] = c;
        }
    }

    out[pos] = '\0';
}

static int fat32_mount(struct vfs_node* backing, struct vfs_node* target, struct vfs_filesystem** result) {
    (void) target;

    if (!backing) {
        return -EINVAL;
    }

    struct fat32_bpb bpb;

    backing->ops->lock(backing);
    int ret = backing->ops->read(backing, &bpb, sizeof(bpb), 0, 0);
    backing->ops->unlock(backing);

    if (ret < 0) {
        return ret;
    }

    if (bpb.fat_size_16 != 0 ||  bpb.signature != 0x29) {
        return -EINVAL;
    }

    if (bpb.reserved_sector_count == 0 || bpb.fat_count == 0) {
        return -EINVAL;
    }

    if (bpb.media < 0xf0) {
        return -EINVAL;
    }

    struct fat32_fsinfo fsinfo;

    backing->ops->lock(backing);
    ret = backing->ops->read(backing, &fsinfo, sizeof(fsinfo), bpb.fsinfo_sector * bpb.bytes_per_sector, 0);
    backing->ops->unlock(backing);

    if (ret < 0) {
        return ret;
    }

    if (fsinfo.lead_sig != FSINFO_LEAD_SIG || fsinfo.struct_sig != FSINFO_STRUCT_SIG || fsinfo.trail_sig != FSINFO_TRAIL_SIG) {
        return -EINVAL;
    }

    struct fat32_filesystem* fatfs = kmallocz(sizeof(struct fat32_filesystem));
    if (unlikely(!fatfs)) {
        return -ENOMEM;
    }
    fatfs->ops = &fat32_ops;

    fatfs->backing = backing;
    VFS_NODE_REF(backing);

    fatfs->root_cluster = bpb.root_cluster;
    fatfs->cluster_size = bpb.bytes_per_sector * bpb.sectors_per_cluster;
    fatfs->fat_offset = bpb.reserved_sector_count * bpb.bytes_per_sector;
    fatfs->fat_size = bpb.fat_size_32 * bpb.bytes_per_sector;
    fatfs->data_offset = fatfs->fat_offset + bpb.fat_count * fatfs->fat_size;

    fatfs->node_map = hashmap_create(1024);
    if (unlikely(!fatfs->node_map)) {
        kfree(fatfs);
        return -ENOMEM;
    }

    mutex_init(&fatfs->node_map_mutex);
    mutex_init(&fatfs->root_mutex);

    *result = (struct vfs_filesystem*) fatfs;
    return 0;
}

static int fat32_root(struct vfs_filesystem* filesystem, struct vfs_node** result) {
    struct fat32_filesystem* fatfs = (struct fat32_filesystem*) filesystem;

    mutex_acquire(&fatfs->root_mutex);

    int ret = 0;

    if (fatfs->root) {
        *result = (struct vfs_node*) fatfs->root;
        goto end;
    }

    struct fat32_node* root = create_node(filesystem, VFS_TYPE_DIRECTORY);
    if (unlikely(!root)) {
        ret = -ENOMEM;
        goto end;
    }
    root->flags |= VFS_NODE_FLAG_ROOT;

    root->cluster = fatfs->root_cluster;
    
    size_t chain_length;
    if ((ret = cluster_chain_length(fatfs, root->cluster, &chain_length)) < 0) {
        slab_cache_free(fat32_node_cache, root);
        goto end;
    }

    root->size = chain_length * fatfs->cluster_size;

    fatfs->root = (struct vfs_node*) root;
    VFS_NODE_REF(root);

    *result = (struct vfs_node*) root;

end:
    mutex_release(&fatfs->root_mutex);
    return ret;
}

static int fat32_parent(struct vfs_node* node, struct vfs_node** result) {
    struct fat32_node* fnode = (struct fat32_node*) node;

    struct vfs_node* parent = (struct vfs_node*) fnode->parent_dir;
    VFS_NODE_REF(parent);

    *result = parent;
    return 0;
}

static int fat32_create(struct vfs_node* parent, const char* name, vfs_type_t type, struct vfs_node** result) {
    (void) parent;
    (void) name;
    (void) type;
    (void) result;
    return -EROFS;
}

static int fat32_lookup(struct vfs_node* parent, const char* name, struct vfs_node** result) {
    struct fat32_node* fparent = (struct fat32_node*) parent;
    struct fat32_filesystem* fatfs = (struct fat32_filesystem*) parent->filesystem;

    if (strcmp(name, ".") == 0 || (strcmp(name, "..") == 0 && fatfs->root == parent)) {
        VFS_NODE_REF(parent);
        *result = parent;
        return 0;
    }

    if (strcmp(name, "..") == 0) {
        if (!fparent->parent_dir) {
            return -ENOENT;
        }

        *result = fparent->parent_dir;

        VFS_NODE_REF(*result);
        (*result)->ops->lock(*result);
        return 0;
    }

    struct fat32_dirent dirent;
    size_t dirent_disk_offset;

    int ret = directory_lookup(fatfs, fparent, name, &dirent, &dirent_disk_offset);
    if (ret < 0) {
        return ret;
    }

    struct fat32_node* fnode = NULL;

    mutex_acquire(&fatfs->node_map_mutex);

    void* v;
    if (hashmap_get(fatfs->node_map, &dirent_disk_offset, sizeof(dirent_disk_offset), &v)) {
        fnode = v;
        VFS_NODE_REF((struct vfs_node*) fnode);
    } else {
        fnode = create_node((struct vfs_filesystem*) fatfs, (dirent.attributes & FAT32_ATTR_DIRECTORY) ? VFS_TYPE_DIRECTORY : VFS_TYPE_REGULAR);
        if (unlikely(!fnode)) {
            mutex_release(&fatfs->node_map_mutex);
            return -ENOMEM;
        }

        if (!hashmap_set(fatfs->node_map, &dirent_disk_offset, sizeof(dirent_disk_offset), fnode)) {
            slab_cache_free(fat32_node_cache, fnode);
            mutex_release(&fatfs->node_map_mutex);
            return -ENOMEM;
        }

        fnode->parent_dir = parent;
        VFS_NODE_REF(parent);

        fnode->cluster = ((uint32_t) dirent.cluster_high << 16) | dirent.cluster_low;

        if (fnode->type == VFS_TYPE_DIRECTORY) {
            size_t chain_length;

            if ((ret = cluster_chain_length(fatfs, fnode->cluster, &chain_length)) < 0) {
                slab_cache_free(fat32_node_cache, fnode);
                mutex_release(&fatfs->node_map_mutex);
                return -ENOMEM;
            }

            fnode->size = chain_length * fatfs->cluster_size;
        } else {
            fnode->size = dirent.size;
        }

        fnode->dirent_disk_offset = dirent_disk_offset;

        VFS_NODE_REF((struct vfs_node*) fnode);
    }

    mutex_release(&fatfs->node_map_mutex);

    *result = (struct vfs_node*) fnode;
    fnode->ops->lock((struct vfs_node*) fnode);

    return 0;
}

static int fat32_rename(struct vfs_node* src_dir, struct vfs_node* src, const char* old_name, struct vfs_node* target_dir, const char* new_name) {
    (void) src_dir;
    (void) src;
    (void) old_name;
    (void) target_dir;
    (void) new_name;
    return -EROFS;
}

static int fat32_link(struct vfs_node* dir, const char* name, struct vfs_node* node) {
    (void) dir;
    (void) name;
    (void) node;
    return -ENOTSUP;
}

static int fat32_symlink(struct vfs_node* dir, const char* name, const char* target) {
    (void) dir;
    (void) name;
    (void) target;
    return -ENOTSUP;
}

static ssize_t fat32_readlink(struct vfs_node* node, char* buf, size_t length) {
    (void) node;
    (void) buf;
    (void) length;
    return -ENOTSUP;
}

static int fat32_unlink(struct vfs_node* parent, struct vfs_node* child, const char* name) {
    (void) parent;
    (void) child;
    (void) name;
    return -EROFS;
}

static ssize_t fat32_read(struct vfs_node* node, void* buf, size_t count, off_t offset, int flags) {
    (void) flags;

    if (node->type == VFS_TYPE_DIRECTORY) {
        return -EISDIR;
    }

    if (node->type != VFS_TYPE_REGULAR) {
        return -EINVAL;
    }

    struct fat32_node* fnode = (struct fat32_node*) node;
    struct fat32_filesystem* fatfs = (struct fat32_filesystem*) node->filesystem;

    off_t end_offset = offset + count;

    if (offset >= (off_t) fnode->size) {
        return 0;
    }

    if (offset > end_offset) {
        end_offset = -1;
    }

    if (end_offset > fnode->size) {
        end_offset = fnode->size;
        count = end_offset - offset;
    }

    if (count == 0) {
        return 0;
    }

    uint32_t target_cluster_index = offset / fatfs->cluster_size;
    uint32_t cluster_offset_in_file = offset % fatfs->cluster_size;

    uint32_t cluster = fnode->cluster;
    uint32_t cluster_index = 0;

    while (cluster_index < target_cluster_index) {
        int ret = next_cluster(fatfs, cluster, &cluster);
        if (ret < 0) {
            return ret;
        }

        if (cluster >= FAT32_CLUSTER_END) {
            return -EIO;
        }

        cluster_index++;
    }

    size_t cluster_buf_pages = DIV_CEIL(fatfs->cluster_size, PAGE_SIZE_4KB);
    uintptr_t cluster_buf_paddr = pmm_alloc(cluster_buf_pages);
    void* cluster_buf = (void*) (cluster_buf_paddr + HIGH_VMA);

    size_t total_read = 0;

    while (total_read < count) {
        int ret = read_cluster(fatfs, cluster, cluster_buf);
        if (ret < 0) {
            pmm_free(cluster_buf_paddr, cluster_buf_pages);
            return ret;
        }

        size_t bytes_to_copy = MIN(fatfs->cluster_size - cluster_offset_in_file, count - total_read);

        if ((ret = USER_MEMCPY_MAYBE_TO_USER((uint8_t*) buf + total_read, (uint8_t*) cluster_buf + cluster_offset_in_file, bytes_to_copy)) < 0) {
            pmm_free(cluster_buf_paddr, cluster_buf_pages);
            return ret;
        }

        total_read += bytes_to_copy;
        cluster_offset_in_file = 0;

        if (total_read >= count) {
            break;
        }

        uint32_t next;

        ret = next_cluster(fatfs, cluster, &next);
        if (ret < 0) {
            pmm_free(cluster_buf_paddr, cluster_buf_pages);
            return ret;
        }

        if (next >= FAT32_CLUSTER_END) {
            break;
        }

        cluster = next;
        cluster_index++;
    }

    pmm_free(cluster_buf_paddr, cluster_buf_pages);
    return total_read;
}

static ssize_t fat32_write(struct vfs_node* node, const void* buf, size_t count, off_t offset, int flags) {
    (void) node;
    (void) buf;
    (void) count;
    (void) offset;
    (void) flags;
    return -EROFS;
}

static int fat32_ioctl(struct vfs_node* node, int request, void* argp) {
    (void) node;
    (void) request;
    (void) argp;
    return -ENOTTY;
}

static int fat32_truncate(struct vfs_node* node, off_t length) {
    (void) node;
    (void) length;
    return -EROFS;
}

static short fat32_poll(struct vfs_node* node, short events, struct poll_table* pt) {
    (void) node;
    (void) pt;

    short revents = 0;

    if (events & POLLIN) {
        revents |=  POLLIN;
    }

    if (events & POLLOUT) {
        revents |=  POLLOUT;
    }

    return revents;
}

static int fat32_sync(struct vfs_node* node) {
    (void) node;
    return 0;
}

static int fat32_mmap(struct vfs_node* node, void* addr, off_t offset, int flags, uint64_t pte_flags) {
    (void) node;
    (void) addr;
    (void) offset;
    (void) flags;
    (void) pte_flags;
    return -ENOTSUP;
}

static int fat32_munmap(struct vfs_node* node, void* addr, off_t offset) {
    (void) node;
    (void) addr;
    (void) offset;
    return -ENOTSUP;
}

static ssize_t fat32_getdents(struct vfs_node* node, struct dirent* buf, size_t count, off_t offset) {
    if (node->type != VFS_TYPE_DIRECTORY) {
        return -ENOTDIR;
    }

    if (offset < 0) {
        return -EINVAL;
    }

    struct fat32_node* fnode = (struct fat32_node*) node;
    struct fat32_filesystem* fatfs = (struct fat32_filesystem*) node->filesystem;

    size_t entries_written = 0;

    if (offset <= 0 && entries_written < count) {
        struct dirent ent = {
            .d_ino = fnode->dirent_disk_offset,
            .d_off = 1,
            .d_reclen = sizeof(struct dirent),
            .d_type = DT_DIR,
            .d_name = ".",
        };

        int ret = USER_MEMCPY_MAYBE_TO_USER(&buf[entries_written], &ent, sizeof(ent));
        if (ret < 0) {
            return ret;
        }

        entries_written++;
    }

    if (offset <= 1 && entries_written < count) {
        struct dirent ent = {
            .d_ino = fnode->parent_dir ? ((struct fat32_node*) fnode->parent_dir)->dirent_disk_offset : fnode->dirent_disk_offset,
            .d_off = 2,
            .d_reclen = sizeof(struct dirent),
            .d_type = DT_DIR,
            .d_name = "..",
        };

        int ret = USER_MEMCPY_MAYBE_TO_USER(&buf[entries_written], &ent, sizeof(ent));
        if (ret < 0) {
            return ret;
        }

        entries_written++;
    }

    off_t fat_offset = offset - 2;
    if (fat_offset < 0) {
        fat_offset = 0;
    }

    uint32_t cluster = fnode->cluster;
    size_t entries_per_cluster = fatfs->cluster_size / sizeof(struct fat32_dirent);

    size_t cluster_buf_pages = DIV_CEIL(fatfs->cluster_size, PAGE_SIZE_4KB);
    uintptr_t cluster_buf_paddr = pmm_alloc(cluster_buf_pages);
    void* cluster_buf = (void*) (cluster_buf_paddr + HIGH_VMA);

    off_t visible_offset = 0;

    while (cluster < FAT32_CLUSTER_END) {
        int ret = read_cluster(fatfs, cluster, cluster_buf);
        if (ret < 0) {
            pmm_free(cluster_buf_paddr, cluster_buf_pages);
            return ret;
        }

        struct fat32_dirent* entries = (struct fat32_dirent*) cluster_buf;

        for (size_t i = 0; i < entries_per_cluster; i++) {
            struct fat32_dirent* e = &entries[i];
            if (e->name[0] == 0x00) {
                pmm_free(cluster_buf_paddr, cluster_buf_pages);
                return entries_written;
            }

            if (!is_valid_sfn(e)) {
                continue;
            }

            if (visible_offset < fat_offset) {
                visible_offset++;
                continue;
            }

            if (entries_written >= count) {
                pmm_free(cluster_buf_paddr, cluster_buf_pages);
                return entries_written;
            }

            struct dirent ent = {
                .d_ino = cluster_offset(fatfs, cluster) + (i * sizeof(struct fat32_dirent)),
                .d_off = visible_offset + 1,
                .d_reclen = sizeof(struct dirent),
                .d_type = (e->attributes & FAT32_ATTR_DIRECTORY) ? DT_DIR : DT_REG,
            };

            sfn_to_cstr(e, ent.d_name);
            if (ent.d_name[0] == '\0') {
                continue;
            }

            ret = USER_MEMCPY_MAYBE_TO_USER(&buf[entries_written], &ent, sizeof(struct dirent));
            if (ret < 0) {
                pmm_free(cluster_buf_paddr, cluster_buf_pages);
                return ret;
            }

            entries_written++;
            visible_offset++;
        }

        uint32_t next;

        ret = next_cluster(fatfs, cluster, &next);
        if (ret < 0) {
            pmm_free(cluster_buf_paddr, cluster_buf_pages);
            return ret;
        }

        cluster = next;
    }

    pmm_free(cluster_buf_paddr, cluster_buf_pages);
    return entries_written;
}

static int fat32_getstat(struct vfs_node* node, struct stat* stat) {
    struct fat32_node* fnode = (struct fat32_node*) node;
    struct fat32_filesystem* fatfs = (struct fat32_filesystem*) node->filesystem;

    struct stat backing_stat;

    fatfs->backing->ops->lock(fatfs->backing);
    int ret = fatfs->backing->ops->getstat(fatfs->backing, &backing_stat);
    fatfs->backing->ops->unlock(fatfs->backing);

    if (ret < 0) {
        return ret;
    }

    struct fat32_dirent dirent;

    fatfs->backing->ops->lock(fatfs->backing);
    ret = fatfs->backing->ops->read(fatfs->backing, &dirent, sizeof(dirent), fnode->dirent_disk_offset, 0);
    fatfs->backing->ops->unlock(fatfs->backing);

    if (ret < 0) {
        return ret;
    }

    struct stat st = {
        .st_dev = backing_stat.st_rdev,
        .st_ino = fnode->dirent_disk_offset,
        .st_mode = vfs_type_to_mode(node->type),
        .st_nlink = fnode->parent_dir ? 1 : 0,
        .st_rdev = 0,
        .st_size = fnode->size,
        .st_blksize = fatfs->cluster_size,
        .st_blocks = DIV_CEIL(fnode->size, fatfs->cluster_size),
        .st_atim = dirent_datetime_to_timespec(dirent.access_date, 0, 0),
        .st_ctim = dirent_datetime_to_timespec(dirent.last_mod_date, dirent.last_mod_time, 0),
    };

    st.st_mtim = st.st_ctim;

    return USER_MEMCPY_MAYBE_TO_USER(stat, &st, sizeof(struct stat));
}

static int fat32_setstat(struct vfs_node* node, const struct stat* stat, int flags) {
    (void) node;
    (void) stat;
    (void) flags;
    return -ENOTSUP;
}

static int fat32_lock(struct vfs_node* node) {
    mutex_acquire(&((struct fat32_node*) node)->mutex);
    return 0;
}

static int fat32_unlock(struct vfs_node* node) {
    mutex_release(&((struct fat32_node*) node)->mutex);
    return 0;
}

static void fat32_inactive(struct vfs_node* node) {
    slab_cache_free(fat32_node_cache, (void*) node);
}

void fat32_init(void) {
    fat32_node_cache = slab_cache_create("struct fat32_node cache", sizeof(struct fat32_node));
    if (unlikely(!fat32_node_cache)) {
        kpanic(NULL, false, "failed to create object cache for fat32 nodes");
    }

    if (unlikely(!vfs_register_fs("fat32", &fat32_ops))) {
        kpanic(NULL, false, "failed to register fat32 with vfs");
    }
}
