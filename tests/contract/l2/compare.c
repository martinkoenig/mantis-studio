#include "probe.h"
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    size_t frozen_count, current_count;
    const MantisL2AbiValue *frozen = mantis_l2_frozen_values(&frozen_count);
    const MantisL2AbiValue *current = mantis_l2_current_values(&current_count);
    if (frozen_count != current_count)
        return 1;
    for (size_t i = 0; i < frozen_count; ++i) {
        if (strcmp(frozen[i].name, current[i].name) || frozen[i].value != current[i].value) {
            fprintf(stderr, "Frozen L2 ABI changed: %s: frozen=%" PRIu64 " current=%" PRIu64 "\n",
                    frozen[i].name, frozen[i].value, current[i].value);
            return 1;
        }
    }
    const size_t numeric_count = frozen_count;
    const MantisL2AbiString *frozen_strings = mantis_l2_frozen_strings(&frozen_count);
    const MantisL2AbiString *current_strings = mantis_l2_current_strings(&current_count);
    if (frozen_count != current_count)
        return 1;
    for (size_t i = 0; i < frozen_count; ++i) {
        if (strcmp(frozen_strings[i].name, current_strings[i].name) ||
            strcmp(frozen_strings[i].value, current_strings[i].value)) {
            fprintf(stderr, "Frozen L2 identity changed: %s\n", frozen_strings[i].name);
            return 1;
        }
    }
    printf("Frozen L2 ABI: %zu layout/numeric checks and %zu identity checks passed\n", numeric_count,
           frozen_count);
    return 0;
}
