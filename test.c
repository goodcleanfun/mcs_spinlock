#include <stdio.h>
#include <stdlib.h>

#include "greatest/greatest.h"
#include "threading/threading.h"

#include "mcs_spinlock.h"

typedef struct {
    int counter;
    mcs_spinlock_t *lock;
} spinlock_test_t;

#ifndef NUM_THREADS
#define NUM_THREADS 16
#endif
#ifndef NUM_PUSHES
#define NUM_PUSHES 100000
#endif


int increment_counter(void *arg) {
    spinlock_test_t *test = (spinlock_test_t *)arg;
    for (int i = 0; i < NUM_PUSHES; i++) {
        mcs_spinlock_lock(test->lock);
        test->counter++;
        mcs_spinlock_unlock(test->lock);
    }
    return 0;
}

TEST mcs_spinlock_multithread_test(void) {
    thrd_t threads[NUM_THREADS];

    spinlock_test_t *test = (spinlock_test_t *)malloc(sizeof(spinlock_test_t));

    test->counter = 0;
    mcs_spinlock_t *lock = mcs_spinlock_new();
    test->lock = lock;

    for (int i = 0; i < NUM_THREADS; i++) {
        ASSERT_EQ(thrd_success, thrd_create(&threads[i], increment_counter, test));
    }
    for (int i = 0; i < NUM_THREADS; i++) {
        thrd_join(threads[i], NULL);
    }

    mcs_spinlock_lock(test->lock);
    ASSERT_FALSE(mcs_spinlock_trylock(test->lock));
    mcs_spinlock_unlock(test->lock);

    ASSERT_EQ(NUM_THREADS * NUM_PUSHES, test->counter);

    free(test);

    PASS();
}


// Main test suite
SUITE(mcs_spinlock_tests) {
    RUN_TEST(mcs_spinlock_multithread_test);
}

GREATEST_MAIN_DEFS();

int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();

    RUN_SUITE(mcs_spinlock_tests);

    GREATEST_MAIN_END();
}