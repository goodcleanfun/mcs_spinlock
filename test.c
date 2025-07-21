#include <stdio.h>
#include <stdlib.h>

#include "greatest/greatest.h"
#include "threading/threading.h"

#include "spinlock_mcs.h"

typedef struct {
    int counter;
    spinlock_mcs_t *lock;
} spinlock_test_t;

#define NUM_THREADS 16
#define NUM_PUSHES 100000


int increment_counter(void *arg) {
    spinlock_test_t *test = (spinlock_test_t *)arg;
    for (int i = 0; i < NUM_PUSHES; i++) {
        spinlock_mcs_lock(test->lock);
        test->counter++;
        spinlock_mcs_unlock(test->lock);
    }
    return 0;
}

TEST spinlock_mcs_multithread_test(void) {
    thrd_t threads[NUM_THREADS];

    spinlock_test_t *test = (spinlock_test_t *)malloc(sizeof(spinlock_test_t));

    test->counter = 0;
    spinlock_mcs_t *lock = spinlock_mcs_new();
    test->lock = lock;

    for (int i = 0; i < NUM_THREADS; i++) {
        ASSERT_EQ(thrd_success, thrd_create(&threads[i], increment_counter, test));
    }
    for (int i = 0; i < NUM_THREADS; i++) {
        thrd_join(threads[i], NULL);
    }

    spinlock_mcs_lock(test->lock);
    ASSERT_FALSE(spinlock_mcs_trylock(test->lock));
    spinlock_mcs_unlock(test->lock);

    ASSERT_EQ(NUM_THREADS * NUM_PUSHES, test->counter);

    free(test);

    PASS();
}


// Main test suite
SUITE(spinlock_mcs_tests) {
    RUN_TEST(spinlock_mcs_multithread_test);
}

GREATEST_MAIN_DEFS();

int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();

    RUN_SUITE(spinlock_mcs_tests);

    GREATEST_MAIN_END();
}