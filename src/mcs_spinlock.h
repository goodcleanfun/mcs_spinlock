#ifndef MCS_SPINLOCK_H
#define MCS_SPINLOCK_H

#include <stdatomic.h>

#include <stdbool.h>

#include "aligned/aligned.h"
#include "cpu_relax/cpu_relax.h"
#include "threading/threading.h"

#define MAX_PAUSE_ITERATIONS 40

typedef struct mcs_spinlock_node {
    atomic_bool locked;
    _Atomic(struct mcs_spinlock_node *) next;
} mcs_spinlock_node_t;

typedef struct {
    _Atomic(mcs_spinlock_node_t *)tail;
    tss_t local_node;
} mcs_spinlock_t;

#ifndef MCS_SPINLOCK_MALLOC
#define MCS_SPINLOCK_MALLOC cache_line_aligned_malloc
#endif

#ifndef MCS_SPINLOCK_FREE
#define MCS_SPINLOCK_FREE aligned_free
#endif

// This allows using a memory pool (might need to be NUMA aware, etc.) instead of a malloc-like allocator
#ifndef MCS_SPINLOCK_NEW_NODE

#ifndef MCS_SPINLOCK_NODE_MALLOC
#define MCS_SPINLOCK_NODE_MALLOC cache_line_aligned_malloc
#endif
static inline mcs_spinlock_node_t *mcs_spinlock_node_new(void) {
    return (mcs_spinlock_node_t *)MCS_SPINLOCK_NODE_MALLOC(sizeof(mcs_spinlock_node_t));
}
#define MCS_SPINLOCK_NEW_NODE mcs_spinlock_node_new
#endif

// Same with releasing a node. This is used as the tss_dtor, so it executes at thread exit.
#ifndef MCS_SPINLOCK_NODE_RELEASE

#ifndef MCS_SPINLOCK_NODE_FREE
#define MCS_SPINLOCK_NODE_FREE aligned_free
#endif
static inline void mcs_spinlock_node_release(mcs_spinlock_node_t *node) {
    MCS_SPINLOCK_NODE_FREE(node);
}
#define MCS_SPINLOCK_NODE_RELEASE mcs_spinlock_node_release
#endif


static inline mcs_spinlock_t *mcs_spinlock_new(void) {
    mcs_spinlock_t *lock = (mcs_spinlock_t *)MCS_SPINLOCK_MALLOC(sizeof(mcs_spinlock_t));
    atomic_init(&lock->tail, NULL);
    // Note: nodes are freed automatically
    tss_create(&lock->local_node, (tss_dtor_t)MCS_SPINLOCK_NODE_RELEASE);
    return lock;
}

static inline mcs_spinlock_node_t *mcs_spinlock_get_local_node(mcs_spinlock_t *lock) {
    mcs_spinlock_node_t *node = (mcs_spinlock_node_t *)tss_get(lock->local_node);
    if (node == NULL) {
        node = (mcs_spinlock_node_t *)MCS_SPINLOCK_NEW_NODE();
        atomic_store(&node->locked, false);
        atomic_store(&node->next, NULL);
        // Note: this will be freed by the tss_dtor
        tss_set(lock->local_node, node);
    }
    return node;
}


static void mcs_spinlock_lock(mcs_spinlock_t *lock) {
    mcs_spinlock_node_t *node = mcs_spinlock_get_local_node(lock);

    mcs_spinlock_node_t *pred = atomic_exchange(&lock->tail, node);
    if (pred != NULL) {
        atomic_store(&node->locked, true);
        atomic_store(&pred->next, node);
        bool node_is_free = false;
        for (int i = 0; i < MAX_PAUSE_ITERATIONS; i++) {
            if (!atomic_load_explicit(&node->locked, memory_order_acquire)) {
                node_is_free = true;
                break;
            }
            cpu_relax();
        }
        if (!node_is_free) {
            while (atomic_load_explicit(&node->locked, memory_order_relaxed)) {
                thrd_yield();
            }
        }
    }
}

static inline bool mcs_spinlock_is_locked(mcs_spinlock_t *lock) {
    return atomic_load(&lock->tail) != NULL;
}

static bool mcs_spinlock_trylock(mcs_spinlock_t *lock) {
    mcs_spinlock_node_t *node = mcs_spinlock_get_local_node(lock);
    mcs_spinlock_node_t *null_tail = NULL;
    return atomic_compare_exchange_strong(&lock->tail, &null_tail, node);
}

static void mcs_spinlock_unlock(mcs_spinlock_t *lock) {
    mcs_spinlock_node_t *node = (mcs_spinlock_node_t *)tss_get(lock->local_node);
    if (node == NULL) {
        return;
    }
    if (atomic_load(&node->next) == NULL) {
        mcs_spinlock_node_t *tail = atomic_load(&lock->tail);
        if (tail == node && atomic_compare_exchange_strong(&lock->tail, &tail, NULL)) {
            return;
        }
        // Wait until successor fills in its next field
        bool have_next = false;
        for (int i = 0; i < MAX_PAUSE_ITERATIONS; i++) {
            if (atomic_load_explicit(&node->next, memory_order_acquire) != NULL) {
                have_next = true;
                break;
            }
            cpu_relax();
        }
        if (!have_next) {
            while (atomic_load_explicit(&node->next, memory_order_relaxed) == NULL) {
                thrd_yield();
            }
        }
    }
    atomic_store_explicit(&node->next->locked, false, memory_order_release);
    atomic_store(&node->next, NULL);
}

static inline void mcs_spinlock_destroy(mcs_spinlock_t *lock) {
    if (lock == NULL) {
        return;
    }
    MCS_SPINLOCK_FREE(lock);
}

#endif