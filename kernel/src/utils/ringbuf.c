#include <mem/slab.h>
#include <utils/ringbuf.h>
#include <utils/string.h>

ringbuf_t* ringbuf_create(size_t size, size_t elem_size) {
    ringbuf_t* r = kmalloc(sizeof(ringbuf_t));
    if (r == NULL) {
        return NULL;
    }

    r->size = 0;
    r->capacity = size;
    r->elem_size = elem_size;

    r->read_ptr = -1;
    r->write_ptr = -1;

    r->data = kmalloc(size * elem_size);
    if (r->data == NULL) {
        kfree(r);
        return NULL;
    }

    return r;
}

void ringbuf_destroy(ringbuf_t* r) {
    kfree(r->data);
    kfree(r);
}

bool ringbuf_push(ringbuf_t* r, const void* data) {
    if ((!r->read_ptr && r->write_ptr == (r->capacity - 1)) || (r->read_ptr == (r->write_ptr + 1))) {
        return false;
    }

    spinlock_acquire(&r->lock);

    if (r->read_ptr == (size_t) -1) {
        r->read_ptr = 0;
        r->write_ptr = 0;
    } else {
        if (r->write_ptr == (r->capacity - 1)) {
            r->write_ptr = 0;
        } else {
            r->write_ptr++;
        }
    }

    memcpy((void*) ((uintptr_t)  r->data + (r->write_ptr * r->elem_size)), data, r->elem_size);
    r->size++;

    spinlock_release(&r->lock);
    return true;
}

bool ringbuf_pop(ringbuf_t* r, void* data) {
    if (r->read_ptr == (size_t) -1) {
        return false;
    }

    spinlock_acquire(&r->lock);
    memcpy(data, (void*) ((uintptr_t) r->data + (r->read_ptr * r->elem_size)), r->elem_size);
    r->size--;

    if (r->read_ptr == r->write_ptr) {
        r->read_ptr = -1;
        r->write_ptr = -1;
    } else {
        if (r->read_ptr == (r->capacity - 1)) {
            r->read_ptr = 0;
        } else {
            r->read_ptr++;
        }
    }

    spinlock_release(&r->lock);
    return true;
}

bool ringbuf_pop_tail(ringbuf_t* r, void* data) {
	if (r->read_ptr == r->write_ptr) {
		return false;
	}

    spinlock_acquire(&r->lock);

	if (r->write_ptr == 0) {
		r->write_ptr = r->capacity - 1;
	} else {
		r->write_ptr--;
	}

	memcpy(data, (void*) ((uintptr_t) r->data + (r->write_ptr * r->elem_size)), r->elem_size);
    r->size--;

    spinlock_release(&r->lock);
	return true;
}

bool ringbuf_peek(ringbuf_t* r, void* data) {
    if (r->read_ptr == (size_t) -1) {
        return false;
    }

    spinlock_acquire(&r->lock);
    memcpy(data, (void*) ((uintptr_t) r->data + (r->read_ptr * r->elem_size)), r->elem_size);
    spinlock_release(&r->lock);
    return true;
}
