#pragma once
#include <cstdlib>

namespace mantis::test {
// A null value removes the variable in both CRTs; no POSIX API reaches MSVC.
inline int environment(const char *name, const char *value) {
#ifdef _WIN32
    return ::_putenv_s(name, value ? value : "");
#else
    return value ? ::setenv(name, value, 1) : ::unsetenv(name);
#endif
}
} // namespace mantis::test
