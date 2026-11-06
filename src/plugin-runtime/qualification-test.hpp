#pragma once
// Private observer for the isolated storage qualifier, absent from product code.
#ifdef MANTIS_ACQUISITION_QUALIFICATION
namespace mantis::plugins {
class Loaded;
namespace qualification_test {
void opened(const Loaded &, void *);
void before_next();
void after_next(int);
} // namespace qualification_test
} // namespace mantis::plugins
#endif
