/* Included by separate frozen/current C TUs with isolated header search paths. */
#include "probe.h"
#include <mantis/projected_light.h>

#define ABI_TYPE(T) {#T ".sizeof", sizeof(T)}, {#T ".alignment", _Alignof(T)},
#define ABI_FIELD(T, F) {#T "." #F ".offset", offsetof(T, F)}, {#T "." #F ".sizeof", sizeof(((T *)0)->F)},
#define ABI_VALUE(V) {#V, V},
#define ABI_STRING(V)
static const MantisL2AbiValue values[] = {
#include "layout.inc"
#include "values.inc"
};
#undef ABI_TYPE
#undef ABI_FIELD
#undef ABI_VALUE
#undef ABI_STRING
#define ABI_VALUE(V)
#define ABI_STRING(V) {#V, V},
static const MantisL2AbiString strings[] = {
#include "values.inc"
};
#undef ABI_VALUE
#undef ABI_STRING

const MantisL2AbiValue *L2_VALUES(size_t *count) {
    *count = sizeof(values) / sizeof(values[0]);
    return values;
}
const MantisL2AbiString *L2_STRINGS(size_t *count) {
    *count = sizeof(strings) / sizeof(strings[0]);
    return strings;
}
