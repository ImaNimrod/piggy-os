#include <cpu/smp.h>
#include <errno.h>
#include <fs/tmpfs.h>
#include <fs/vfs.h>
#include <mem/paging.h>
#include <mem/pmm.h>
#include <mem/slab.h>
#include <mem/vmm.h>
#include <sys/timer.h>
#include <types.h>
#include <utils/hashmap.h>
#include <utils/log.h>
#include <utils/macros.h>
#include <utils/spinlock.h>
#include <utils/string.h>
#include <utils/usercopy.h>
#include <utils/vector.h>

struct tmpfs_filesystem {
    struct vfs_filesystem;
    ino_t inode_counter;
};

struct tmpfs_node {
    struct vfs_node;
    struct stat stat;
    union {
        vector_t* pages;
        hashmap_t* children;
    };
    spinlock_t lock;
};

static struct slab_cache* tmpfs_node_cache;

static int tmpfs_mount(struct vfs_node* backing, struct vfs_node* target, struct vfs_filesystem** result);
static int tmpfs_root(struct vfs_filesystem* filesystem, struct vfs_node** result);

static struct vfs_ops tmpfs_ops = {
    .mount = tmpfs_mount,
    .root = tmpfs_root,
};

static int tmpfs_create(struct vfs_node* parent, char* name, vfs_type_t type, struct vfs_node** result);
static int tmpfs_lookup(struct vfs_node* parent, char* name, struct vfs_node** result);
static int tmpfs_rename(struct vfs_node* src_dir, struct vfs_node* src, char* old_name, struct vfs_node* target_dir, char* new_name);
static int tmpfs_unlink(struct vfs_node* parent, char* name, struct vfs_node** result);
static ssize_t tmpfs_read(struct vfs_node* node, void* buf, size_t count, off_t offset, int flags);
static ssize_t tmpfs_write(struct vfs_node* node, const void* buf, size_t count, off_t offset, int flags);
static int tmpfs_ioctl(struct vfs_node* node, int request, void* argp);
static int tmpfs_truncate(struct vfs_node* node, off_t length);
static int tmpfs_sync(struct vfs_node* node);
static int tmpfs_mmap(struct vfs_node* node, void* addr, off_t offset, int flags, uint64_t pte_flags);
static int tmpfs_munmap(struct vfs_node* node, void* addr, off_t offset);
static ssize_t tmpfs_getdents(struct vfs_node* node, struct dirent* buf, size_t count, off_t offset);
static int tmpfs_getstat(struct vfs_node* node, struct stat* stat);
static int tmpfs_setstat(struct vfs_node* node, const struct stat* stat, int flags);
static int tmpfs_lock(struct vfs_node* node);
static int tmpfs_unlock(struct vfs_node* node);
static void tmpfs_inactive(struct vfs_node* node);

static struct vfs_node_ops tmpfs_node_ops = {
    .create = tmpfs_create,
    .lookup = tmpfs_lookup,
    .rename = tmpfs_rename,
    .unlink = tmpfs_unlink,
    .read = tmpfs_read,
    .write = tmpfs_write,
    .ioctl = tmpfs_ioctl,
    .truncate = tmpfs_truncate,
    .sync = tmpfs_sync,
    .mmap = tmpfs_mmap,
    .munmap = tmpfs_munmap,
    .getdents = tmpfs_getdents,
    .getstat = tmpfs_getstat,
    .setstat = tmpfs_setstat,
    .lock = tmpfs_lock,
    .unlock = tmpfs_unlock,
    .inactive = tmpfs_inactive,
};

static struct tmpfs_node* create_node(struct vfs_filesystem* filesystem, vfs_type_t type) {
    struct tmpfs_node* node = slab_cache_alloc(tmpfs_node_cache);
    if (unlikely(node == NULL)) {
        return NULL;
    }

    if (type == VFS_TYPE_DIRECTORY) {
        node->children = hashmap_create(32);
        if (unlikely(node->children == NULL)) {
            slab_cache_free(tmpfs_node_cache, node);
            return NULL;
        }

        if (unlikely(!hashmap_set(node->children, ".", 1, node))) {
            hashmap_destroy(node->children);
            slab_cache_free(tmpfs_node_cache, node);
            return NULL;
        }
    } else if (type == VFS_TYPE_REGULAR) {
        node->pages = vector_create(sizeof(uintptr_t));
        if (unlikely(node->children == NULL)) {
            slab_cache_free(tmpfs_node_cache, node);
            return NULL;
        }

        node->flags |= VFS_FLAG_MMAP;
    }

    node->type = type;
    node->ops = &tmpfs_node_ops;
    node->filesystem = filesystem;
    node->refcount = 1;

    node->stat.st_ino = __atomic_add_fetch(&((struct tmpfs_filesystem*) filesystem)->inode_counter, 1, __ATOMIC_SEQ_CST);
    node->stat.st_mode = vfs_type_to_mode(type);
    node->stat.st_nlink = type == VFS_TYPE_DIRECTORY ? 2 : 1;
    node->stat.st_blksize = PAGE_SIZE_4KB;
    node->stat.st_atim = node->stat.st_mtim = node->stat.st_ctim = time_realtime;

    return node;
}

static int tmpfs_mount(struct vfs_node* backing, struct vfs_node* target, struct vfs_filesystem** result) {
    (void) backing;
    (void) target;

    struct tmpfs_filesystem* tmpfs = kmalloc(sizeof(struct tmpfs_filesystem));
    if (unlikely(tmpfs == NULL)) {
        return -ENOMEM;
    }

    *result = (struct vfs_filesystem*) tmpfs;
    tmpfs->ops = &tmpfs_ops;

    return 0;
}

static int tmpfs_root(struct vfs_filesystem* filesystem, struct vfs_node** result) {
    if (filesystem->root != NULL) {
        *result = filesystem->root;
        return 0;
    }

    struct tmpfs_node* node = create_node(filesystem, VFS_TYPE_DIRECTORY);
    if (unlikely(node == NULL)) {
        return -ENOMEM;
    }

    *result = (struct vfs_node*) node;
    filesystem->root = *result;

    node->flags |= VFS_FLAG_ROOT;
    return 0;
}

static int tmpfs_create(struct vfs_node* parent, char* name, vfs_type_t type, struct vfs_node** result) {
    if (parent->type != VFS_TYPE_DIRECTORY) {
        return -ENOTDIR;
    }

    size_t name_len = strlen(name);

    struct tmpfs_node* tmpfs_parent = (struct tmpfs_node*) parent;

    void* v;
    if (hashmap_get(tmpfs_parent->children, name, name_len, &v)) {
        return -EEXIST;
    }

    struct tmpfs_node* new = create_node(parent->filesystem, type);
    if (unlikely(new == NULL)) {
        return -ENOMEM;
    }

    if (type == VFS_TYPE_DIRECTORY) {
        if (!hashmap_set(new->children, "..", 2, tmpfs_parent)) {
            new->ops->unlock((struct vfs_node*) new);
            return -ENOMEM;
        }
    }

    if (!hashmap_set(tmpfs_parent->children, name, name_len, new)) {
        new->ops->unlock((struct vfs_node*) new);
        return -ENOMEM;
    }

    if (type == VFS_TYPE_DIRECTORY) {
        tmpfs_parent->stat.st_nlink++;
        VFS_NODE_REF(tmpfs_parent);
    }

    new->filesystem = parent->filesystem;
    *result = (struct vfs_node*) new;

    VFS_NODE_REF(new);
    new->ops->lock((struct vfs_node*) new);
    return 0;
}

static int tmpfs_lookup(struct vfs_node* parent, char* name, struct vfs_node** result) {
    if (parent->type != VFS_TYPE_DIRECTORY) {
        return -ENOTDIR;
    }

    struct vfs_node* child;
    if (!hashmap_get(((struct tmpfs_node*) parent)->children, name, strlen(name), (void**) &child)) {
        return -ENOENT;
    }

    VFS_NODE_REF(child);

    if (strcmp(name, "..") == 0) {
        child->ops->unlock(parent);
    }

    if (child != parent) {
        child->ops->lock(child);
    }

    *result = (struct vfs_node*) child;
    return 0;
}

static int tmpfs_rename(struct vfs_node* src_dir, struct vfs_node* src, char* old_name, struct vfs_node* target_dir, char* new_name) {
    if (src_dir->filesystem != target_dir->filesystem) {
        return -EXDEV;
    }

    if (src_dir->type != VFS_TYPE_DIRECTORY || target_dir->type != VFS_TYPE_DIRECTORY) {
        return -ENOTDIR;
    }

    struct tmpfs_node* tsrc_dir = (struct tmpfs_node*) src_dir;
    struct tmpfs_node* ttarget_dir = (struct tmpfs_node*) target_dir;

    struct vfs_node* old_node = NULL;

    size_t old_len = strlen(old_name);
    size_t new_len = strlen(new_name);

    void* res;
    if (hashmap_get(ttarget_dir->children, new_name, new_len, &res)) {
        old_node = res;
    }

    if (old_node != NULL) {
        if (src == old_node) {
            return 0;
        }

        if (src->type != VFS_TYPE_DIRECTORY && old_node->type == VFS_TYPE_DIRECTORY) {
            return -EISDIR;
        }

        if (src->type == VFS_TYPE_DIRECTORY && old_node->type != VFS_TYPE_DIRECTORY) {
            return -ENOTDIR;
        }

        if (old_node->type == VFS_TYPE_DIRECTORY && hashmap_size(((struct tmpfs_node*) old_node)->children) > 2) {
            return -ENOTEMPTY;
        }
    }

    if (src->mounted || (old_node != NULL && old_node->mounted)) {
        return -EBUSY;
    }

    if (tsrc_dir == ttarget_dir && strcmp(old_name, new_name) == 0) {
        return 0;
    }

    if (!hashmap_set(ttarget_dir->children, new_name, new_len, src)) {
        return -ENOMEM;
    }

    hashmap_remove(tsrc_dir->children, old_name, old_len);

    if (src->type == VFS_TYPE_DIRECTORY && src_dir != target_dir) {
        struct tmpfs_node* tsrc = (struct tmpfs_node*) src;
        hashmap_set(tsrc->children, "..", 2, target_dir);

        tsrc_dir->stat.st_nlink--;
        ttarget_dir->stat.st_nlink++;

        VFS_NODE_UNREF(src);
        VFS_NODE_REF(target_dir);
    }

    if (old_node != NULL) {
        if (old_node->type == VFS_TYPE_DIRECTORY) {
            VFS_NODE_REF(old_node);
        }

        ((struct tmpfs_node*) old_node)->stat.st_nlink--;
        VFS_NODE_UNREF(old_node);
    }

    return 0;
}

static int tmpfs_unlink(struct vfs_node* parent, char* name, struct vfs_node** result) {
    if (parent->type != VFS_TYPE_DIRECTORY) {
        return -ENOTDIR;
    }

    if (parent->mounted != NULL) {
        return -EBUSY;
    }

    size_t name_len = strlen(name);

    struct tmpfs_node* child;
    if (!hashmap_get(((struct tmpfs_node*) parent)->children, name, name_len, (void**) &child)) {
        return -ENOENT;
    }

    if (child->type == VFS_TYPE_DIRECTORY) {
        if (hashmap_size(child->children) > 2) {
            return -ENOTEMPTY;
        }
    }

    if (!hashmap_remove(((struct tmpfs_node*) parent)->children, name, name_len)) {
        return -EIO;
    }

    child->stat.st_nlink -= child->type == VFS_TYPE_DIRECTORY ? 2 : 1;

    VFS_NODE_UNREF(child);
    *result = (struct vfs_node*) child;
    return 0;
}

static ssize_t tmpfs_read(struct vfs_node* node, void* buf, size_t count, off_t offset, int flags) {
    (void) flags;

    if (node->type == VFS_TYPE_DIRECTORY) {
        return -EISDIR;
    }

    struct tmpfs_node* tnode = (struct tmpfs_node*) node;

    if (offset >= tnode->stat.st_size) {
        return 0;
    }

    ssize_t remaining = tnode->stat.st_size - offset;
    ssize_t to_read = MIN((off_t) count, remaining);

    ssize_t done = 0;
    while (done < to_read) {
        size_t file_off = offset + done;
        size_t page_index = file_off / PAGE_SIZE_4KB;
        size_t page_off = file_off % PAGE_SIZE_4KB;
        size_t chunk = MIN(PAGE_SIZE_4KB - page_off, (size_t) to_read - done);

        uintptr_t* page = (uintptr_t*) vector_get(tnode->pages, page_index);

        ssize_t ret;

        if (page == NULL || *page == 0) {
            ret = USER_MEMSET_MAYBE_USER((uint8_t*) buf + done, 0, chunk);
        } else {
            ret = USER_MEMCPY_MAYBE_TO_USER((uint8_t*) buf + done, (void*) (*page + page_off + HIGH_VMA), chunk);
        }

        if (ret < 0) {
            return ret;
        }

        done += chunk;
    }

    tnode->stat.st_atim = time_realtime;
    return done;
}

static ssize_t tmpfs_write(struct vfs_node* node, const void* buf, size_t count, off_t offset, int flags) {
    (void) flags;

    if (node->type == VFS_TYPE_DIRECTORY) {
        return -EISDIR;
    }

    struct tmpfs_node* tnode = (struct tmpfs_node*) node;

    off_t end = offset + count;
    if (end != 0 && end >= tnode->stat.st_size) {
        tnode->stat.st_size = end;
        tnode->stat.st_blocks = DIV_CEIL(end, tnode->stat.st_blksize);

        if (!vector_resize(tnode->pages, tnode->stat.st_blocks)) {
            return -ENOMEM;
        }
    }

    ssize_t done = 0;
    while (done < (off_t) count) {
        size_t file_off = offset + done;
        size_t page_index = file_off / PAGE_SIZE_4KB;
        size_t page_off   = file_off % PAGE_SIZE_4KB;
        size_t chunk = PAGE_SIZE_4KB - page_off;
        if (chunk > count - done) {
            chunk = count - done;
        }

        uintptr_t* page = (uintptr_t*) vector_get(tnode->pages, page_index);
        if (*page == 0) {
            bool need_zero = page_off != 0 || chunk != PAGE_SIZE_4KB;
            uintptr_t new_page = need_zero ? pmm_alloc_zero(1) : pmm_alloc(1);
            vector_set(tnode->pages, page_index, &new_page);
        }

        ssize_t ret = USER_MEMCPY_MAYBE_FROM_USER((void*) (*page + page_off + HIGH_VMA), (uint8_t*) buf + done, chunk);
        if (ret < 0) {
            return ret;
        }

        done += chunk;
    }

    tnode->stat.st_mtim = tnode->stat.st_ctim = time_realtime;
    return done;
}

static int tmpfs_ioctl(struct vfs_node* node, int request, void* argp) {
    (void) node;
    (void) request;
    (void) argp;
    return -ENOTTY;
}

static int tmpfs_truncate(struct vfs_node* node, off_t length) {
    struct tmpfs_node* tnode = (struct tmpfs_node*) node;

    if (node->type == VFS_TYPE_DIRECTORY) {
        return -EISDIR;
    }

    off_t old_size = tnode->stat.st_size;
    if (old_size == length) {
        return 0;
    }

    size_t new_page_count = DIV_CEIL(length, PAGE_SIZE_4KB);

    bool ret;
    if (length < old_size) {
        size_t old_page_count = DIV_CEIL(old_size, PAGE_SIZE_4KB);

        for (size_t i = new_page_count; i < old_page_count; i++) {
            uintptr_t* page = (uintptr_t*) vector_get(tnode->pages, i);
            if (page != NULL && *page != 0) {
                pmm_free(*page, 1);
            }
        }

        ret = vector_resize(tnode->pages, new_page_count);
    } else {
        ret = vector_resize(tnode->pages, new_page_count);
    }

    if (!ret) {
        return -ENOMEM;
    }

    tnode->stat.st_size = length;
    tnode->stat.st_blocks = DIV_CEIL(length, tnode->stat.st_blksize);
    tnode->stat.st_atim = tnode->stat.st_mtim = tnode->stat.st_ctim = time_realtime;
    return 0;
}

static int tmpfs_sync(struct vfs_node* node) {
    (void) node;
    return 0;
}

static int tmpfs_mmap(struct vfs_node* node, void* addr, off_t offset, int flags, uint64_t pte_flags) {
    struct tmpfs_node* tnode = (struct tmpfs_node*) node;
    uintptr_t* page = (uintptr_t*) vector_get(tnode->pages, offset / PAGE_SIZE_4KB);

    struct vmm_context* context = this_cpu()->scheduler.current_thread->process->vmm_context;

    if (flags & MAP_SHARED) {
        pagemap_map(context->pagemap, (uintptr_t) addr, *page, pte_flags, PAGE_SIZE_4KB);
    } else {
        off_t end = tnode->stat.st_size;
        size_t size = offset + PAGE_SIZE_4KB < end ? PAGE_SIZE_4KB : end - offset;

        uintptr_t paddr = pmm_alloc_zero(1);
        memcpy((void*) (paddr + HIGH_VMA), (void*) (*page + HIGH_VMA), size);
        pagemap_map(context->pagemap, (uintptr_t) addr, paddr, pte_flags, PAGE_SIZE_4KB);
    }

    return 0;
}

static int tmpfs_munmap(struct vfs_node* node, void* addr, off_t offset) {
    struct tmpfs_node* tnode = (struct tmpfs_node*) node;
    if (offset >= tnode->stat.st_size) {
        return 0;
    }

    struct vmm_context* context = this_cpu()->scheduler.current_thread->process->vmm_context;

    page_size_t page_size;
    pagemap_unmap(context->pagemap, (uintptr_t) addr, &page_size);
    pagemap_invalidate((uintptr_t) addr, page_size);

    return 0;
}

static ssize_t tmpfs_getdents(struct vfs_node* node, struct dirent* buf, size_t count, off_t offset) {
    if (node->type != VFS_TYPE_DIRECTORY) {
        return -ENOTDIR;
    }

    struct tmpfs_node* tnode = (struct tmpfs_node*) node;

    ssize_t current = 0;
    ssize_t ret = 0;

    HASHMAP_FOREACH(tnode->children) {
        if (current < offset) {
            current++;
            continue;
        }

        if (ret == (ssize_t) count) {
            break;
        }

        struct tmpfs_node* ent_node = entry->value;

        struct dirent ent = {
            .d_ino = ent_node->stat.st_ino,
            .d_off = offset,
            .d_reclen = sizeof(struct dirent),
            .d_type = vfs_type_to_dirent(ent_node->type),
        };
        memcpy(ent.d_name, entry->key, entry->key_size);
        ent.d_name[entry->key_size] = '\0';

        int err;
        if ((err = USER_MEMCPY_MAYBE_TO_USER(&buf[ret], &ent, sizeof(struct dirent))) < 0) {
            return err;
        }

        ret += 1;
    }

    tnode->stat.st_atim = time_realtime;
    return ret;
}

static int tmpfs_getstat(struct vfs_node* node, struct stat* stat) {
    return USER_MEMCPY_MAYBE_TO_USER((void*) stat, (const void*) &((struct tmpfs_node*) node)->stat, sizeof(struct stat));
}

static int tmpfs_setstat(struct vfs_node* node, const struct stat* stat, int flags) {
    struct tmpfs_node* tnode = (struct tmpfs_node*) node;

    if (flags & VFS_STAT_ST_ATIM) {
        tnode->stat.st_atim = stat->st_atim;
    }
    if (flags & VFS_STAT_ST_MTIM) {
        tnode->stat.st_mtim = stat->st_mtim;
    }
    if (flags & VFS_STAT_ST_CTIM) {
        tnode->stat.st_ctim = stat->st_ctim;
    }

    return 0;
}

static int tmpfs_lock(struct vfs_node* node) {
    spinlock_acquire(&((struct tmpfs_node*) node)->lock);
    return 0;
}

static int tmpfs_unlock(struct vfs_node* node) {
    spinlock_release(&((struct tmpfs_node*) node)->lock);
    return 0;
}

static void tmpfs_inactive(struct vfs_node* node) {
    struct tmpfs_node* tnode = (struct tmpfs_node*) node;

    if (tnode->type == VFS_TYPE_REGULAR) {
        for (size_t i = 0; i < vector_size(tnode->pages); i++) {
            uintptr_t* page = (uintptr_t*) vector_get(tnode->pages, i);
            if (page != NULL && *page != 0) {
                pmm_free(*page, 1);
            }
        }

        vector_destroy(tnode->pages);
    } else if (tnode->type == VFS_TYPE_DIRECTORY) {
        if (hashmap_size(tnode->children) != 2) {
            kpanic(NULL, true, "nonempty tmpfs directory node is being released");
        }

        struct vfs_node* parent;
        if (!hashmap_get(tnode->children, "..", 2, (void**) &parent)) {
            kpanic(NULL, true, "tmpfs directory node missing '..' entry");
        }

        VFS_NODE_UNREF(parent);

        hashmap_destroy(tnode->children);
    }

    slab_cache_free(tmpfs_node_cache, tnode);
}

void tmpfs_init(void) {
    tmpfs_node_cache = slab_cache_create("struct tmpfs_node cache", sizeof(struct tmpfs_node));
    if (unlikely(tmpfs_node_cache == NULL)) {
        kpanic(NULL, false, "failed to create object cache for tmpfs nodes");
    }

    if (unlikely(!vfs_register_fs("tmpfs", &tmpfs_ops))) {
        kpanic(NULL, false, "failed to register tmpfs with vfs");
    }
}
