#include <cpu/asm.h> 
#include <cpu/smp.h> 
#include <mem/paging.h> 
#include <mem/pmm.h> 
#include <sys/process.h>
#include <sys/scheduler.h>
#include <utils/log.h>
#include <utils/macros.h>

#include "definitions.h"

static void internal_ring_submit(struct xhci_ring* ring, struct trb* submission_trb) {
    submission_trb->cycle = ring->cycle ? 1 : 0;

    ring->trbs[ring->index++] = *submission_trb;

    if (ring->index == ring->size - 1) {
        struct trb* link = &ring->trbs[ring->size - 1];
        link->parameter = ring->trb_paddr;
        link->cycle = ring->cycle ? 1 : 0;
        link->toggle_cycle = 1;
        link->trb_type = TRB_TYPE_LINK;

        ring->index = 0;
        ring->cycle = !ring->cycle;
    }

    mmio_write32(ring->doorbell, ring->doorbell_value);
}

struct trb* ring_dequeue(struct xhci_ring* ring) {
    bool int_state = spinlock_acquire_irqsave(&ring->lock);

    struct trb* trb = &ring->trbs[ring->index];
    if (trb->cycle != ring->cycle) {
        spinlock_release_irqsave(&ring->lock, int_state);
        return NULL;
    }

    ring->index++;

    if (ring->index == ring->size) {
        ring->index = 0;
        ring->cycle = !ring->cycle;
    }

    spinlock_release_irqsave(&ring->lock, int_state);
    return trb;
}

void ring_init(struct xhci_ring* ring, uint32_t* doorbell, uint32_t doorbell_value) {
    ring->trb_paddr = pmm_alloc_zero(1);
    ring->trbs = (void*) (ring->trb_paddr + HIGH_VMA);
    ring->size = PAGE_SIZE_4KB / sizeof(struct trb);
    ring->index = 0;
    ring->cycle = true;

    ring->doorbell = doorbell;
    ring->doorbell_value = doorbell_value;

    ring->completion_waiters = vector_create(sizeof(struct completion_waiter));
    if (unlikely(ring->completion_waiters == NULL)) {
        kpanic(NULL, false, "failed to allocate memory for xHCI ring completion waiters");
    }
}

void ring_submit(struct xhci_ring* ring, struct trb* submission_trb) {
    bool int_state = spinlock_acquire_irqsave(&ring->lock);
    internal_ring_submit(ring, submission_trb);
    spinlock_release_irqsave(&ring->lock, int_state);
}

bool ring_submit_and_wait(struct xhci_ring* ring, struct trb* submission_trb, struct trb* completion_trb) {
    bool int_state = spinlock_acquire_irqsave(&ring->lock);

    struct completion_waiter waiter = {
        .submission_trb_paddr = (uintptr_t) &ring->trbs[ring->index] - HIGH_VMA,
        .completion_trb = completion_trb,
        .thread = this_cpu()->scheduler.current_thread,
    };

    vector_push(ring->completion_waiters, &waiter);
    internal_ring_submit(ring, submission_trb);

    scheduler_prepare_wait(this_cpu()->scheduler.current_thread, true);

    spinlock_release_irqsave(&ring->lock, int_state);

    scheduler_yield();
    return ((completion_trb->status >> 24) & 0xff) == TRB_STATUS_SUCCESS;
}
