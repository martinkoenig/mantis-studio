#ifndef MANTIS_TEST_PROJECTED_FIXTURE_H
#define MANTIS_TEST_PROJECTED_FIXTURE_H
#include <stdint.h>
enum {
    TEST_NORMAL,
    TEST_GRAPH_NULL,
    TEST_GRAPH_SIZE,
    TEST_GRAPH_VERSION,
    TEST_GRAPH_COUNT,
    TEST_GRAPH_DUPLICATE,
    TEST_GRAPH_PARENT,
    TEST_GRAPH_RELATION,
    TEST_GRAPH_PRESENCE,
    TEST_ZERO_EMIT,
    TEST_DOUBLE_EMIT,
    TEST_WRONG_MEMBER,
    TEST_TOO_MANY_MEMBERS,
    TEST_BAD_FRAMESET,
    TEST_BAD_EVIDENCE,
    TEST_RUN_MISMATCH,
    TEST_NOT_READY,
    TEST_FAILURE,
    TEST_PENDING,
    TEST_EMIT_AFTER_FAILURE,
    TEST_BUNDLE_NULL,
    TEST_BUNDLE_SIZE,
    TEST_BUNDLE_VERSION,
    TEST_BUNDLE_PRESENCE,
    TEST_BAD_STATUS,
    TEST_BAD_ABORT,
    TEST_REJECT_PROGRAM,
    TEST_DESTROY_REFUSE,
    TEST_ACTIVE_RUN,
    TEST_PROGRAM_MISMATCH
};
typedef struct TestProjectedControl {
    void (*fault)(uint32_t);
    uint32_t (*pending)(void);
    uint32_t (*destroyed)(void);
    uint32_t (*live_instances)(void);
    uint32_t (*initializations)(void);
    uint32_t (*shutdowns)(void);
} TestProjectedControl;
#define TEST_PROJECTED_CONTROL "org.mantis.test.projected-control.v1"
#endif
