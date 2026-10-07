#include "v2/plugin.h"
#include <stddef.h>
size_t mantis_frozen_layout(size_t *out) {
    size_t n = 0;
#define LAYOUT_TYPE(T)                                                                                       \
    out[n++] = sizeof(T);                                                                                    \
    out[n++] = _Alignof(T);
#define LAYOUT_FIELD(T, F) out[n++] = offsetof(T, F);
#include "legacy_layout_fields.inc"
#undef LAYOUT_TYPE
#undef LAYOUT_FIELD
    return n;
}
