#pragma once

// Private isolated-fixture export, never part of the Studio plugin ABI. Hooks are
// installed only while calls are quiescent and run outside the priority mutex.
// The test owns the hook context through next() retirement. No hook is installed
// by ordinary daemon/bench activation.
namespace x1::f2::simulation {
using PublicationTestHook = void (*)(void *context, bool admitted) noexcept;
using SetPublicationTestHook = int (*)(void *instance, PublicationTestHook, void *context) noexcept;
inline constexpr auto publication_test_hook_symbol = "mantis_x1_f2_set_publication_test_hook";
} // namespace x1::f2::simulation
