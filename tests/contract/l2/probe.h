#ifndef MANTIS_TEST_L2_PROBE_H
#define MANTIS_TEST_L2_PROBE_H
#include <stddef.h>
#include <stdint.h>
typedef struct MantisL2AbiValue {
    const char *name;
    uint64_t value;
} MantisL2AbiValue;
typedef struct MantisL2AbiString {
    const char *name, *value;
} MantisL2AbiString;
const MantisL2AbiValue *mantis_l2_frozen_values(size_t *);
const MantisL2AbiValue *mantis_l2_current_values(size_t *);
const MantisL2AbiString *mantis_l2_frozen_strings(size_t *);
const MantisL2AbiString *mantis_l2_current_strings(size_t *);
#endif
