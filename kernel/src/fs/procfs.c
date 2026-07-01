#include <cpu/smp.h>
#include <errno.h>
#include <fs/procfs.h>
#include <fs/vfs.h>
#include <mem/slab.h>
#include <printf.h> 
#include <sys/process.h>
#include <sys/timer.h>
#include <utils/hashmap.h>
#include <utils/log.h>
#include <utils/macros.h>
#include <utils/mutex.h>
#include <utils/string.h>
#include <utils/usercopy.h>

enum procfs_node_type : int {
    PROCFS_TYPE_ROOT,
    PROCFS_TYPE_SELF,

    // Per-process
    PROCFS_TYPE_PID_DIR,
    PROCFS_TYPE_CMDLINE,
    PROCFS_TYPE_CWD,
    PROCFS_TYPE_ENVIRON,
    PROCFS_TYPE_EXE,
    PROCFS_TYPE_STATUS,
};

struct procfs_node {
    struct vfs_node;
    struct stat stat;
    struct procfs_node* parent;
    enum procfs_node_type procfs_type;
    pid_t pid;
};

struct procfs_node_map_key {
    enum procfs_node_type type;
    pid_t pid;
};

static ino_t inode_counter;
static struct slab_cache* procfs_node_cache;
static hashmap_t* procfs_node_map;
static mutex_t procfs_node_map_mutex;
static struct procfs_node* procfs_root_node;

static int procfs_mount(struct vfs_node* backing, struct vfs_node* target, struct vfs_filesystem** result);
static int procfs_root(struct vfs_filesystem* filesystem, struct vfs_node** result);

static struct vfs_ops procfs_ops = {
    .mount = procfs_mount,
    .root = procfs_root,
};

static int procfs_parent(struct vfs_node* node, struct vfs_node** result);
static int procfs_create(struct vfs_node* parent, const char* name, vfs_type_t type, struct vfs_node** result);
static int procfs_lookup(struct vfs_node* parent, const char* name, struct vfs_node** result);
static int procfs_rename(struct vfs_node* src_dir, struct vfs_node* src, const char* old_name, struct vfs_node* target_dir, const char* new_name);
static int procfs_link(struct vfs_node* dir, const char* name, struct vfs_node* node);
static int procfs_symlink(struct vfs_node* dir, const char* name, const char* target);
static ssize_t procfs_readlink(struct vfs_node* node, char* buf, size_t length);
static int procfs_unlink(struct vfs_node* parent, struct vfs_node* child, const char* name);
static ssize_t procfs_read(struct vfs_node* node, void* buf, size_t count, off_t offset, int flags);
static ssize_t procfs_write(struct vfs_node* node, const void* buf, size_t count, off_t offset, int flags);
static int procfs_ioctl(struct vfs_node* node, int request, void* argp);
static int procfs_truncate(struct vfs_node* node, off_t length);
static short procfs_poll(struct vfs_node* node, short events, struct poll_table* pt);
static int procfs_sync(struct vfs_node* node);
static ssize_t procfs_getdents(struct vfs_node* node, struct dirent* buf, size_t count, off_t offset);
static int procfs_getstat(struct vfs_node* node, struct stat* stat);
static int procfs_setstat(struct vfs_node* node, const struct stat* stat, int flags);
static int procfs_lock(struct vfs_node* node);
static int procfs_unlock(struct vfs_node* node);
static void procfs_inactive(struct vfs_node* node);

static struct vfs_node_ops procfs_node_ops = {
    .parent = procfs_parent,
    .create = procfs_create,
    .lookup = procfs_lookup,
    .rename = procfs_rename,
    .link = procfs_link,
    .symlink = procfs_symlink,
    .readlink = procfs_readlink,
    .unlink = procfs_unlink,
    .read = procfs_read,
    .write = procfs_write,
    .ioctl = procfs_ioctl,
    .truncate = procfs_truncate,
    .poll = procfs_poll,
    .sync = procfs_sync,
    .getdents = procfs_getdents,
    .getstat = procfs_getstat,
    .setstat = procfs_setstat,
    .lock = procfs_lock,
    .unlock = procfs_unlock,
    .inactive = procfs_inactive,
};

static inline uint64_t node_map_key(enum procfs_node_type type, pid_t pid) {
    return type | ((uint64_t) pid << 32);
}

static inline char status_to_char(process_state_t state) {
    switch (state) {
        case PROCESS_STATE_RUNNING:
            return 'R';
        case PROCESS_STATE_ZOMBIE:
            return 'Z';
        default:
            __builtin_unreachable();
    }
}

static int create_node(enum procfs_node_type type, pid_t pid, struct procfs_node* parent, struct vfs_node** result) {
    uint64_t key = node_map_key(type, pid);

    mutex_acquire(&procfs_node_map_mutex);

    void* res;
    if (hashmap_get(procfs_node_map, &key, sizeof(key), &res)) {
        mutex_release(&procfs_node_map_mutex);

        struct vfs_node* cached_node = res;
        VFS_NODE_REF(cached_node);
        *result = cached_node;
        return 0;
    }

    struct procfs_node* node = slab_cache_alloc(procfs_node_cache);
    if (unlikely(!node)) {
        mutex_release(&procfs_node_map_mutex);
        return -ENOMEM;
    }

    switch (type) {
        case PROCFS_TYPE_CMDLINE:
        case PROCFS_TYPE_ENVIRON:
        case PROCFS_TYPE_STATUS:
            node->type = VFS_TYPE_REGULAR;
            break;
        case PROCFS_TYPE_ROOT:
        case PROCFS_TYPE_PID_DIR:
            node->type = VFS_TYPE_DIRECTORY;
            break;
        case PROCFS_TYPE_SELF:
        case PROCFS_TYPE_CWD:
        case PROCFS_TYPE_EXE:
            node->type = VFS_TYPE_SYMLINK;
            break;
        default:
            __builtin_unreachable();
    }

    node->ops = &procfs_node_ops;
    node->refcount = 1;

    memset(&node->stat, 0, sizeof(struct stat));
    node->stat.st_ino = __atomic_fetch_add(&inode_counter, 1, __ATOMIC_SEQ_CST);
    node->stat.st_mode = vfs_type_to_mode(node->type);
    node->stat.st_nlink = node->type == VFS_TYPE_DIRECTORY ? 2 : 1;
    node->stat.st_blksize = PAGE_SIZE_4KB;
    node->stat.st_atim = node->stat.st_mtim = node->stat.st_ctim = time_realtime;

    node->parent = parent;
    if (likely(parent)) {
        VFS_NODE_REF((struct vfs_node*) parent);
    }

    node->procfs_type = type;
    node->pid = pid;

    if (!hashmap_set(procfs_node_map, &key, sizeof(key), node)) {
        slab_cache_free(procfs_node_cache, node);
        mutex_release(&procfs_node_map_mutex);
        return -ENOMEM;
    }

    mutex_release(&procfs_node_map_mutex);

    *result = (struct vfs_node*) node;
    return 0;
}

static ssize_t getdents_pid_dir(struct dirent* buf, size_t count, off_t offset) {
    static const struct dirent per_process_dirents[] = {
        { .d_name = ".", .d_type = DT_DIR, .d_reclen = sizeof(struct dirent) },
        { .d_name = "..", .d_type = DT_DIR, .d_reclen = sizeof(struct dirent) },
        { .d_name = "cmdline", .d_type = DT_REG, .d_reclen = sizeof(struct dirent) },
        { .d_name = "cwd", .d_type = DT_LNK, .d_reclen = sizeof(struct dirent) },
        { .d_name = "environ", .d_type = DT_REG, .d_reclen = sizeof(struct dirent) },
        { .d_name = "exe", .d_type = DT_LNK, .d_reclen = sizeof(struct dirent) },
        { .d_name = "status", .d_type = DT_REG, .d_reclen = sizeof(struct dirent) },
    };

    size_t total = SIZEOF_ARRAY(per_process_dirents);
    if ((size_t) offset >= total) {
        return 0;
    }

    ssize_t actual_count = (ssize_t) MIN(count, total - offset);

    ssize_t ret = USER_MEMCPY_MAYBE_TO_USER(buf, &per_process_dirents[offset], actual_count * sizeof(struct dirent));
    if (ret < 0) {
        return ret;
    }

    return actual_count;
}

static ssize_t getdents_root(struct dirent* buf, size_t count, off_t offset) {
    size_t written = 0;
    size_t index = offset;

    int ret;

    while (written < count) {
        if (index == 0) {
            struct dirent ent = {
                .d_type = DT_DIR,
                .d_ino = 0,
                .d_reclen = sizeof(struct dirent),
                .d_name = ".",
            };

            if ((ret = USER_MEMCPY_MAYBE_TO_USER(&buf[written], &ent, sizeof(struct dirent))) < 0) {
                return ret;
            }

            written++;
            index++;
            continue;
        }

        if (index == 1) {
            struct dirent ent = {
                .d_type = DT_DIR,
                .d_ino = 0,
                .d_reclen = sizeof(struct dirent),
                .d_name = "..",
            };

            if ((ret = USER_MEMCPY_MAYBE_TO_USER(&buf[written], &ent, sizeof(struct dirent))) < 0) {
                return ret;
            }

            written++;
            index++;
            continue;
        }

        if (index == 2) {
            struct dirent ent = {
                .d_type = DT_DIR,
                .d_ino = 0,
                .d_reclen = sizeof(struct dirent),
                .d_name = "self",
            };

            if ((ret = USER_MEMCPY_MAYBE_TO_USER(&buf[written], &ent, sizeof(struct dirent))) < 0) {
                return ret;
            }

            written++;
            index++;
            continue;
        }

        size_t target = index - 3;
        size_t current = 0;
        bool found = false;

        mutex_acquire(&processes_mutex);

        HASHMAP_FOREACH(processes) {
            struct process* process = entry->value;

            if (current++ == target) {
                struct dirent ent = {
                    .d_type = DT_DIR,
                    .d_ino = 0,
                    .d_reclen = sizeof(struct dirent),
                };

                snprintf(ent.d_name, sizeof(ent.d_name), "%d", process->pid);

                if ((ret = USER_MEMCPY_MAYBE_TO_USER(&buf[written], &ent, sizeof(struct dirent))) < 0) {
                    mutex_release(&processes_mutex);
                    return ret;
                }

                found = true;
                break;
            }
        }

        mutex_release(&processes_mutex);

        if (!found) {
            break;
        }

        written++;
        index++;
    }

    return written;
}

static ssize_t read_array(char** array, char* buf, size_t length) {
    size_t pos = 0;

    for (size_t i = 0; array[i]; i++) {
        size_t arg_length = strlen(array[i]) + 1;

        if (pos + arg_length > length) {
            break;
        }

        memcpy(buf + pos, array[i], arg_length);
        pos += arg_length;
    }

    return pos;
}

static ssize_t read_status(struct process* process, char* buf, size_t length) {
    spinlock_acquire(&process->thread_list_lock);
    size_t num_threads = vector_size(process->threads);
    spinlock_release(&process->thread_list_lock);

    return snprintf(buf, length,
            "State: %c\nPid: %d\nPPid: %d\nThreads: %zu\n",
            status_to_char(process->state), process->pid, process->parent ? process->parent->pid : 0, num_threads);
}

static pid_t strtopid(const char* str) {
    if (*str == '\0') {
        return -EINVAL;
    }

    unsigned int value = 0;

    while (*str) {
        if (*str < '0' || *str > '9') {
            return -EINVAL;
        }

        unsigned int digit = *str - '0';

        if (value > (((pid_t) -1) - digit) / 10) {
            return -EINVAL;
        }

        value = value * 10 + digit;
        str++;
    }

    return value;
}

static int procfs_mount(struct vfs_node* backing, struct vfs_node* target, struct vfs_filesystem** result) {
    (void) backing;
    (void) target;

    struct vfs_filesystem* procfs = kmallocz(sizeof(struct vfs_filesystem));
    if (unlikely(!procfs)) {
        return -ENOMEM;
    }
    procfs->root = (struct vfs_node*) procfs_root_node;
    procfs->ops = &procfs_ops;

    procfs_root_node->filesystem = procfs;

    *result = procfs;
    return 0;
}

static int procfs_root(struct vfs_filesystem* filesystem, struct vfs_node** result) {
    *result = filesystem->root;
    return 0;
}

static int procfs_parent(struct vfs_node* node, struct vfs_node** result) {
    struct procfs_node* pnode = (struct procfs_node*) node;

    struct vfs_node* parent = pnode->parent ? (struct vfs_node*) pnode->parent : (struct vfs_node*) pnode;

    VFS_NODE_REF(parent);
    *result = parent;
    return 0;
}

static int procfs_create(struct vfs_node* parent, const char* name, vfs_type_t type, struct vfs_node** result) {
    (void) parent;
    (void) name;
    (void) type;
    (void) result;
    return -ENOTSUP;
}

static int procfs_lookup(struct vfs_node* parent, const char* name, struct vfs_node** result) {
    struct procfs_node* pparent = (struct procfs_node*) parent;

    int ret;

    switch (pparent->procfs_type) {
        case PROCFS_TYPE_ROOT:
            if (strcmp(name, "self") == 0) {
                ret = create_node(PROCFS_TYPE_SELF, 0, pparent, result);
                if (ret >= 0) {
                    VFS_NODE_REF(*result);
                }
                return ret;
            }

            pid_t pid = strtopid(name);
            if (pid < 0) {
                return -ENOENT;
            }

            mutex_acquire(&processes_mutex);

            struct process* process;
            bool found = hashmap_get(processes, &pid, sizeof(pid_t), (void**) &process);

            mutex_release(&processes_mutex);

            if (!found) {
                return -ENOENT;
            }

            ret = create_node(PROCFS_TYPE_PID_DIR, pid, pparent, result);
            if (ret >= 0) {
                VFS_NODE_REF(*result);
            }
            return ret;
        case PROCFS_TYPE_PID_DIR:
            if (strcmp(name, "cmdline") == 0) {
                ret = create_node(PROCFS_TYPE_CMDLINE, pparent->pid, pparent, result);
            } else if (strcmp(name, "cwd") == 0) {
                ret = create_node(PROCFS_TYPE_CWD, pparent->pid, pparent, result);
            } else if (strcmp(name, "environ") == 0) {
                ret = create_node(PROCFS_TYPE_ENVIRON, pparent->pid, pparent, result);
            } else if (strcmp(name, "exe") == 0) {
                ret = create_node(PROCFS_TYPE_EXE, pparent->pid, pparent, result);
            } else if (strcmp(name, "status") == 0) {
                ret = create_node(PROCFS_TYPE_STATUS, pparent->pid, pparent, result);
            } else {
                return -ENOENT;
            }

            if (ret >= 0) {
                VFS_NODE_REF(*result);
            }
            return ret;
        default:
            return -ENOTDIR;
    }
}

static int procfs_rename(struct vfs_node* src_dir, struct vfs_node* src, const char* old_name, struct vfs_node* target_dir, const char* new_name) {
    (void) src_dir;
    (void) src;
    (void) old_name;
    (void) target_dir;
    (void) new_name;
    return -ENOTSUP;
}

static int procfs_link(struct vfs_node* dir, const char* name, struct vfs_node* node) {
    (void) dir;
    (void) name;
    (void) node;
    return -ENOTSUP;
}

static int procfs_symlink(struct vfs_node* dir, const char* name, const char* target) {
    (void) dir;
    (void) name;
    (void) target;
    return -ENOTSUP;
}

static ssize_t procfs_readlink(struct vfs_node* node, char* buf, size_t length) {
    struct procfs_node* pnode = (struct procfs_node*) node;

    ssize_t actual_length;
    char target[PATH_MAX_LENGTH];

    switch (pnode->procfs_type) {
        case PROCFS_TYPE_SELF:
            struct process* current_process = this_cpu()->scheduler.current_thread->process;
            actual_length = snprintf(target, sizeof(target), "%d", current_process->pid);
            break;
        case PROCFS_TYPE_CWD:
        case PROCFS_TYPE_EXE:
            actual_length = snprintf(target, sizeof(target), "/");
            break;
        default:
            return -EINVAL;
    }

    actual_length = MIN(actual_length, (ssize_t) length);

    int ret = USER_MEMCPY_MAYBE_TO_USER(buf, target, actual_length);
    if (ret < 0) {
        return ret;
    }

    pnode->stat.st_atim = time_realtime;
    return actual_length;
}

static int procfs_unlink(struct vfs_node* parent, struct vfs_node* child, const char* name) {
    (void) parent;
    (void) child;
    (void) name;
    return -ENOTSUP;
}

static ssize_t procfs_read(struct vfs_node* node, void* buf, size_t count, off_t offset, int flags) {
    (void) flags;

    if (node->type == VFS_TYPE_DIRECTORY) {
        return -EISDIR;
    }

    struct procfs_node* pnode = (struct procfs_node*) node;

    mutex_acquire(&processes_mutex);

    struct process* process;
    bool found = hashmap_get(processes, &pnode->pid, sizeof(pid_t), (void**) &process);

    mutex_release(&processes_mutex);

    if (!found) {
        return -ESRCH;
    }

    ssize_t actual_count;
    char data[1024];

    switch (pnode->procfs_type) {
        case PROCFS_TYPE_CMDLINE:
            actual_count = read_array(process->cmdline, data, sizeof(data));
            break;
        case PROCFS_TYPE_ENVIRON:
            actual_count = read_array(process->environ, data, sizeof(data));
            break;
        case PROCFS_TYPE_STATUS:
            actual_count = read_status(process, data, sizeof(data));
            break;
        default:
            return -EINVAL;
    }

    if (offset >= actual_count) {
        return 0;
    }

    actual_count = MIN(actual_count, (ssize_t) count);

    int ret = USER_MEMCPY_MAYBE_TO_USER(buf, data, actual_count);
    if (ret < 0) {
        return ret;
    }

    pnode->stat.st_atim = time_realtime;
    return (ssize_t) actual_count;
}

static ssize_t procfs_write(struct vfs_node* node, const void* buf, size_t count, off_t offset, int flags) {
    (void) node;
    (void) buf;
    (void) count;
    (void) offset;
    (void) flags;
    return -EPERM;
}

static int procfs_ioctl(struct vfs_node* node, int request, void* argp) {
    (void) node;
    (void) request;
    (void) argp;
    return -ENOTTY;
}

static int procfs_truncate(struct vfs_node* node, off_t length) {
    (void) node;
    (void) length;
    return -EPERM;
}

static short procfs_poll(struct vfs_node* node, short events, struct poll_table* pt) {
    (void) node;
    (void) pt;

    short revents = 0;

    if (events & POLLIN) {
        revents |= POLLIN;
    }

    if (events & POLLOUT) {
        revents |= POLLOUT;
    }

    return revents;
}

static int procfs_sync(struct vfs_node* node) {
    (void) node;
    return 0;
}

static ssize_t procfs_getdents(struct vfs_node* node, struct dirent* buf, size_t count, off_t offset) {
    if (node->type != VFS_TYPE_DIRECTORY) {
        return -ENOTDIR;
    }

    if (offset < 0) {
        return -EINVAL;
    }

    struct procfs_node* pnode = (struct procfs_node*) node;

    ssize_t ret;

    switch (pnode->procfs_type) {
        case PROCFS_TYPE_ROOT:
            ret = getdents_root(buf, count, offset);
            break;
        case PROCFS_TYPE_PID_DIR:
            ret = getdents_pid_dir(buf, count, offset);
            break;
        default:
            return -EINVAL;
    }

    pnode->stat.st_atim = time_realtime;
    return ret;
}

static int procfs_getstat(struct vfs_node* node, struct stat* stat) {
    return USER_MEMCPY_MAYBE_TO_USER((void*) stat, (const void*) &((struct procfs_node*) node)->stat, sizeof(struct stat));
}

static int procfs_setstat(struct vfs_node* node, const struct stat* stat, int flags) {
    (void) node;
    (void) stat;
    (void) flags;
    return -ENOTSUP;
}

static int procfs_lock(struct vfs_node* node) {
    (void) node;
    return 0;
}

static int procfs_unlock(struct vfs_node* node) {
    (void) node;
    return 0;
}

static void procfs_inactive(struct vfs_node* node) {
    struct procfs_node* pnode = (struct procfs_node*) node;

    mutex_acquire(&procfs_node_map_mutex);

    uint64_t key = node_map_key(pnode->procfs_type, pnode->pid);
    hashmap_remove(procfs_node_map, &key, sizeof(key));

    mutex_release(&procfs_node_map_mutex);

    if (likely(pnode->parent)) {
        VFS_NODE_UNREF(pnode->parent);
    }

    slab_cache_free(procfs_node_cache, node);
}

void procfs_delete_nodes(pid_t pid) {
    static const enum procfs_node_type per_process_node_types[] = {
        PROCFS_TYPE_PID_DIR,
        PROCFS_TYPE_CMDLINE,
        PROCFS_TYPE_CWD,
        PROCFS_TYPE_ENVIRON,
        PROCFS_TYPE_EXE,
        PROCFS_TYPE_STATUS,
    };

    for (size_t i = 0; i < SIZEOF_ARRAY(per_process_node_types); i++) {
        uint64_t key = node_map_key(per_process_node_types[i], pid);

        mutex_acquire(&procfs_node_map_mutex);

        struct vfs_node* node;
        bool found = hashmap_get(procfs_node_map, &key, sizeof(key), (void**) &node);

        mutex_release(&procfs_node_map_mutex);

        if (likely(found)) {
            VFS_NODE_UNREF(node);
        }
    }
}

void procfs_init(void) {
    procfs_node_cache = slab_cache_create("struct procfs_node cache", sizeof(struct procfs_node));
    if (unlikely(!procfs_node_cache)) {
        kpanic(NULL, false, "failed to create object cache for procfs nodes");
    }

    procfs_node_map = hashmap_create(256);
    if (unlikely(!procfs_node_map)) {
        kpanic(NULL, false, "failed to create procfs node map");
    }

    mutex_init(&procfs_node_map_mutex);

    if (create_node(PROCFS_TYPE_ROOT, 0, NULL, (struct vfs_node**) &procfs_root_node) < 0) {
        kpanic(NULL, false, "failed to create procfs root node");
    }

    if (unlikely(!vfs_register_fs("procfs", &procfs_ops))) {
        kpanic(NULL, false, "failed to register procfs with vfs");
    }
}
