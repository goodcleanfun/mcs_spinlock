#ifndef MCS_LOCK_H
#define MCS_LOCK_H

#include <stdatomic.h>

#include "aligned/aligned.h"
#include "cpu_relax/cpu_relax.h"
#include "threading/threading.h"

typedef struct spinlock_mcs_node {
    atomic_bool locked;
    _Atomic(struct spinlock_mcs_node *) next;
} spinlock_mcs_node_t;

typedef struct {
    _Atomic(spinlock_mcs_node_t *)tail;
    tss_t local_node;
} spinlock_mcs_t;

#ifndef SPINLOCK_MCS_MALLOC
#define SPINLOCK_MCS_MALLOC cache_line_aligned_malloc
#endif

#ifndef SPINLOCK_MCS_FREE
#define SPINLOCK_MCS_FREE aligned_free
#endif

// This allows using a memory pool (might need to be NUMA aware, etc.) instead of a malloc-like allocator
#ifndef SPINLOCK_MCS_NEW_NODE

#ifndef SPINLOCK_MCS_NODE_MALLOC
#define SPINLOCK_MCS_NODE_MALLOC cache_line_aligned_malloc
#endif
static inline spinlock_mcs_node_t *spinlock_mcs_node_new(void) {
    return (spinlock_mcs_node_t *)SPINLOCK_MCS_NODE_MALLOC(sizeof(spinlock_mcs_node_t));
}
#define SPINLOCK_MCS_NEW_NODE spinlock_mcs_node_new
#endif

// Same with releasing a node. This is used as the tss_dtor, so it executes at thread exit.
#ifndef SPINLOCK_MCS_NODE_RELEASE

#ifndef SPINLOCK_MCS_NODE_FREE
#define SPINLOCK_MCS_NODE_FREE aligned_free
#endif
static inline void spinlock_mcs_node_release(spinlock_mcs_node_t *node) {
    SPINLOCK_MCS_NODE_FREE(node);
}
#define SPINLOCK_MCS_NODE_RELEASE spinlock_mcs_node_release
#endif


static inline spinlock_mcs_t *spinlock_mcs_new(void) {
    spinlock_mcs_t *lock = (spinlock_mcs_t *)SPINLOCK_MCS_MALLOC(sizeof(spinlock_mcs_t));
    atomic_init(&lock->tail, NULL);
    // Note: nodes are freed automatically
    tss_create(&lock->local_node, (tss_dtor_t)SPINLOCK_MCS_NODE_RELEASE);
    return lock;
}

static inline spinlock_mcs_node_t *spinlock_mcs_get_local_node(spinlock_mcs_t *lock) {
    spinlock_mcs_node_t *node = (spinlock_mcs_node_t *)tss_get(lock->local_node);
    if (node == NULL) {
        node = (spinlock_mcs_node_t *)SPINLOCK_MCS_NEW_NODE();
        atomic_store(&node->locked, false);
        atomic_store(&node->next, NULL);
        // Note: this will be freed by the tss_dtor
        tss_set(lock->local_node, node);
    }
    return node;
}


static void spinlock_mcs_lock(spinlock_mcs_t *lock) {
    spinlock_mcs_node_t *node = spinlock_mcs_get_local_node(lock);

    spinlock_mcs_node_t *pred = atomic_exchange(&lock->tail, node);
    if (pred != NULL) {
        atomic_store(&node->locked, true);
        atomic_store(&pred->next, node);
        while (atomic_load(&node->locked)) {
            cpu_relax();
        }
    }
}

static inline bool spinlock_mcs_is_locked(spinlock_mcs_t *lock) {
    return atomic_load(&lock->tail) != NULL;
}

static bool spinlock_mcs_trylock(spinlock_mcs_t *lock) {
    spinlock_mcs_node_t *node = spinlock_mcs_get_local_node(lock);
    spinlock_mcs_node_t *null_tail = NULL;
    return atomic_compare_exchange_strong(&lock->tail, &null_tail, node);
}

static void spinlock_mcs_unlock(spinlock_mcs_t *lock) {
    spinlock_mcs_node_t *node = (spinlock_mcs_node_t *)tss_get(lock->local_node);
    if (node == NULL) {
        return;
    }
    if (atomic_load(&node->next) == NULL) {
        spinlock_mcs_node_t *tail = atomic_load(&lock->tail);
        if (tail == node && atomic_compare_exchange_strong(&lock->tail, &tail, NULL)) {
            return;
        }
        // Wait until successor fills in its next field
        while (atomic_load(&node->next) == NULL) {
            cpu_relax();
        }
    }
    atomic_store(&node->next->locked, false);
    atomic_store(&node->next, NULL);
}

static inline void spinlock_mcs_destroy(spinlock_mcs_t *lock) {
    if (lock == NULL) {
        return;
    }
    SPINLOCK_MCS_FREE(lock);
}

#endif