#include <cpu/asm.h>
#include <dev/char/tty.h>
#include <errno.h>
#include <fs/devfs.h>
#include <fs/vfs.h>
#include <limine.h>
#include <mem/slab.h>
#include <mem/vmm.h>
#include <sys/time.h>
#include <utils/macros.h>
#include <utils/panic.h>
#include <utils/string.h>
#include <utils/user_access.h>
#include "flanterm/backends/fb.h"

extern volatile struct limine_framebuffer_request framebuffer_request;

struct tty* active_tty;

static inline bool ignore_char(struct termios* attr, char c) {
    if (c == '\r' && (attr->c_iflag & IGNCR)) {
        return true;
    }
    return false;
}

static inline char translate_char(struct termios* attr, char c) {
    if (c == '\r' && (attr->c_iflag & ICRNL)) {
        return attr->c_cc[VEOL];
    }
    if (c == (char) attr->c_cc[VEOL] && (attr->c_iflag & INLCR)) {
        return '\r';
    }
    return c;
}

static inline bool pass_to_canon_buf(struct termios* attr, char c) {
    if (c == (char) attr->c_cc[VEOL] || c == '\t' || c >= 32) {
        return true;
    }

    for (size_t i = 0; i < NCCS; i++) {
        if (c == (char) attr->c_cc[i]) {
            return false;
        }
    }

	return true;
}

static void do_echo(struct tty* tty, char c) {
    if (!(tty->attr.c_lflag & ECHO)) {
        return;
    }

    if (c > 0 && c < 32 && c != (char) tty->attr.c_cc[VEOL] && c != '\t') {
        if (!(tty->attr.c_lflag & ECHOCTL)) {
            return;
        }

        char aux[2] = { '^', c + 64 };
        flanterm_write((struct flanterm_context*) tty->private, aux, sizeof(aux));
        return;
    }

    flanterm_write((struct flanterm_context*) tty->private, &c, sizeof(char));
}

static void* flanterm_alloc(size_t size) {
    return kmalloc(size);
}

static void flanterm_free(void* ptr, size_t size) {
    (void) size;
    kfree(ptr);
}

static ssize_t tty_handle_canon(struct tty* tty, void* buf, size_t count) {
        ringbuf_t* line_buf;
        ssize_t ret = 0;
        char* c_buf = buf;

out:
        if (ringbuf_peek(tty->canon_buf, &line_buf)) {
            for (ret = 0; ret < (ssize_t) count; ret++) {
                if (!ringbuf_pop(line_buf, c_buf)) {
                    break;
                }
                c_buf++;
            }

            if (line_buf->size == 0) {
                ringbuf_pop(tty->canon_buf, &line_buf);
                ringbuf_destroy(line_buf);
            }

            return ret;
        }

        size_t items = 0;
        char ch, aux;

        line_buf = ringbuf_create(8192, sizeof(char));
        ringbuf_push(tty->canon_buf, &line_buf);

        while (1) {
            while (ringbuf_pop(tty->input_buf, &ch)) {
                if (ignore_char(&tty->attr, ch)) {
                    continue;
                }

                ch = translate_char(&tty->attr, ch);

                if (pass_to_canon_buf(&tty->attr, ch)) {
                    ringbuf_push(line_buf, &ch);
                    items++;
                    do_echo(tty, ch);
                }

                if (ch == (char) tty->attr.c_cc[VEOL] || ch == (char) tty->attr.c_cc[VEOF]) {
                    goto out;
                }

                if ((tty->attr.c_lflag & ECHOE) && (ch == (char) tty->attr.c_cc[VERASE])) {
                    if (items) {
                        items--;
                        char aux2[] = {'\b', ' ', '\b'};
                        flanterm_write((struct flanterm_context*) tty->private, aux2, sizeof(aux2));
                        ringbuf_pop_tail(line_buf, &aux);
                    }
                }
            }
        }

        goto out;
}

static ssize_t tty_read(struct vfs_node* node, void* buf, off_t offset, size_t count, int flags) {
    (void) offset;
    (void) flags;

    ssize_t ret = 0;

    struct tty* tty = node->private;
    char* c_buf = buf;

    if (tty->attr.c_lflag & ICANON) {
        ret = tty_handle_canon(tty, buf, count);
    } else {
        cc_t min = tty->attr.c_cc[VMIN];
        cc_t time = tty->attr.c_cc[VTIME];

        if (min == 0 && time == 0) {
            if (tty->input_buf->size != 0) {
                for (ssize_t i = 0; i < (ssize_t) count; i++) {
                    if (!ringbuf_pop(tty->input_buf, c_buf)) {
                        break;
                    }

                    if (ignore_char(&tty->attr, *c_buf)) {
                        continue;
                    }

                    *c_buf = translate_char(&tty->attr, *c_buf);
                    do_echo(tty, *c_buf++);
                    ret++;
                }
            }
        } else if (min > 0 && time == 0) {
            while (tty->input_buf->size < min) {
                pause();
            }

            for (ssize_t i = 0; i < (ssize_t) count; i++) {
                if (!ringbuf_pop(tty->input_buf, c_buf)) {
                    break;
                }

                if (ignore_char(&tty->attr, *c_buf)) {
                    continue;
                }

                *c_buf = translate_char(&tty->attr, *c_buf);
                do_echo(tty, *c_buf++);
                ret++;
            }
        } else {
            ret = -1;
        }
    }

    return ret;
}

static ssize_t tty_write(struct vfs_node* node, const void* buf, off_t offset, size_t count, int flags) {
    (void) offset;
    (void) flags;

    struct tty* tty = node->private;
    flanterm_write((struct flanterm_context*) tty->private, buf, count);
    return count;
}

static int tty_ioctl(struct vfs_node* node, uint64_t request, void* argp) {
    struct tty* tty = node->private;

    int ret = -ENOTTY;

    switch (request) {
        // TODO: implement tty flushing ioctl for input/output streams 
        case IOCTL_TTYFLUSH:
            ret = 0;
            break;
        case IOCTL_TTYGETATTR:
            if (copy_to_user(argp, (void*) &tty->attr, sizeof(struct termios)) == NULL) {
                ret = -EFAULT;
            } else {
                ret = 0;
            }
            break;
        case IOCTL_TTYSETATTR:
            if (copy_from_user((void*) &tty->attr, argp, sizeof(struct termios)) == NULL) {
                ret = -EFAULT;
            } else {
                ret = 0;
            }
            break;
    }

    return ret;
}

UNMAP_AFTER_INIT void tty_init(void) {
    struct limine_framebuffer_response* framebuffer_response = framebuffer_request.response;
    if (unlikely(framebuffer_response->framebuffer_count == 0)) {
        kpanic(NULL, false, "no framebuffers available for tty");
    }

    struct tty* tty = kmalloc(sizeof(struct tty));
    if (unlikely(tty == NULL)) {
        kpanic(NULL, false, "failed to allocate memory for tty");
    }

    tty->input_buf = ringbuf_create(8192, sizeof(char));
    if (unlikely(tty->input_buf == NULL)) {
        kpanic(NULL, false ,"failed to create tty input buffer");
    }

    tty->canon_buf = ringbuf_create(256, sizeof(ringbuf_t*));
    if (unlikely(tty->canon_buf == NULL)) {
        kpanic(NULL, false, "failed to create tty canonical line buffer");
    }

    tty->attr.c_lflag = ECHO | ECHOE | ECHOCTL | ICANON;
    tty->attr.c_iflag = 0;

    tty->attr.c_cc[VEOF] = 4;
    tty->attr.c_cc[VERASE] = 8;
    tty->attr.c_cc[VINTR] = 3;
    tty->attr.c_cc[VKILL] = 21;
    tty->attr.c_cc[VSTART] = 17;
    tty->attr.c_cc[VSTOP] = 19;
    tty->attr.c_cc[VSUSP] = 26;
    tty->attr.c_cc[VEOL] = '\n';
    tty->attr.c_cc[VQUIT] = 28;

    tty->attr.c_cc[VTIME] = 0;
    tty->attr.c_cc[VMIN] = 1;

    struct limine_framebuffer* framebuffer = framebuffer_response->framebuffers[0];

    tty->private = flanterm_fb_init(
            flanterm_alloc, flanterm_free,
            (uint32_t*) framebuffer->address,
            framebuffer->width, framebuffer->height, framebuffer->pitch,
            framebuffer->red_mask_size, framebuffer->red_mask_shift,
            framebuffer->green_mask_size, framebuffer->green_mask_shift,
            framebuffer->blue_mask_size, framebuffer->blue_mask_shift,
            NULL,
            NULL, NULL,
            NULL, NULL,
            NULL, NULL,
            NULL, 0, 0, 1,
            0, 0,
            0);

    active_tty = tty;

    struct vfs_node* tty_node = devfs_create_device("tty0");
    if (unlikely(tty_node == NULL)) {
        kpanic(NULL, false, "failed to create tty node in devfs");
    }

    tty_node->stat = (struct stat) {
        .st_dev = makedev(0, 1),
        .st_mode = S_IFCHR,
        .st_rdev = makedev(TTY_MAJ, 0),
        .st_size = 0,
        .st_blksize = 4096,
        .st_blocks = 0,
        .st_atim = time_realtime,
        .st_mtim = time_realtime,
        .st_ctim = time_realtime,
    };

    tty_node->private = tty;

    tty_node->read = tty_read;
    tty_node->write = tty_write;
    tty_node->ioctl = tty_ioctl;
}
