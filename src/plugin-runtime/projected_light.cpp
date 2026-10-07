#include <mantis/plugin_runtime.hpp>
#include <mantis/semantic_views.hpp>
#include <set>

namespace mantis::plugins {
namespace {
void result(int rc) {
    if (!rc)
        return;
    const auto code = rc == MANTIS_PL_BUSY           ? Status::busy
                      : rc == MANTIS_PL_INVALID      ? Status::invalid_argument
                      : rc == MANTIS_PL_INCOMPATIBLE ? Status::incompatible
                                                     : Status::plugin_failed;
    fail(code, "Projected-light operation failed (code " + std::to_string(rc) + ")", "projected-light");
}
template <class T, class View, auto Convert> struct Receiver {
    uint32_t calls{};
    std::optional<T> value;
    std::optional<Error> error;
    static int emit(void *ctx, const View *v) noexcept {
        auto &r = *static_cast<Receiver *>(ctx);
        ++r.calls;
        try {
            if (r.calls != 1)
                fail(Status::incompatible, "Multiple publications in one call");
            r.value = Convert(v);
            return 0;
        } catch (const Failure &e) {
            r.error = e.error;
        } catch (const std::exception &e) {
            r.error = Error{Status::incompatible, e.what(), "projected-light"};
        } catch (...) {
            r.error = Error{Status::incompatible, "Unknown callback exception", "projected-light"};
        }
        return MANTIS_PL_ERROR;
    }
    T finish(int rc) {
        if (error)
            throw Failure(*error);
        if (rc && calls)
            fail(Status::incompatible, "Failure emitted a publication");
        result(rc);
        if (calls != 1 || !value)
            fail(Status::incompatible, "Success requires exactly one publication");
        return std::move(*value);
    }
};
using ValidationReceiver =
    Receiver<device::ProgramValidation, MantisProgramValidationV1, semantic::validation>;
using StatusReceiver = Receiver<device::ProjectedStatus, MantisProjectedStatusV1, semantic::status>;
using AbortReceiver = Receiver<device::AbortOutcome, MantisAbortOutcomeV1, semantic::abort_outcome>;
template <class F> auto checked(F f) -> Result<decltype(f())> {
    try {
        return f();
    } catch (const Failure &e) {
        return std::unexpected(e.error);
    } catch (const std::exception &e) {
        return std::unexpected(Error{Status::plugin_failed, e.what(), "projected-light"});
    } catch (...) {
        return std::unexpected(Error{Status::plugin_failed, "Plugin exception", "projected-light"});
    }
}
struct BundleReceiver {
    uint32_t calls{};
    std::optional<data::AcquisitionBundle> value;
    std::optional<Error> error;
    static int emit(void *ctx, const MantisSemanticPacketV1 *v) noexcept {
        auto &r = *static_cast<BundleReceiver *>(ctx);
        ++r.calls;
        try {
            if (r.calls != 1 || !v || v->kind != MANTIS_SEMANTIC_BUNDLE)
                fail(Status::incompatible, "next requires exactly one AcquisitionBundle");
            auto packet = semantic::packet(v, host_api());
            r.value = std::get<data::AcquisitionBundle>(std::move(packet));
            return 0;
        } catch (const Failure &e) {
            r.error = e.error;
        } catch (const std::exception &e) {
            r.error = Error{Status::incompatible, e.what(), "projected-light"};
        } catch (...) {
            r.error = Error{Status::incompatible, "Unknown bundle error", "projected-light"};
        }
        return MANTIS_PL_ERROR;
    }
};
class ProjectedExecutor final : public device::ProjectedExecutor {
    std::shared_ptr<Loaded> loaded_;
    device::ProjectedGraph graph_;
    sdk::ProjectedLight api_;
    std::mutex context_mutex_;
    std::mutex operation_mutex_;
    std::shared_ptr<const data::AcquisitionProgram> prepared_;
    std::optional<data::RunId> active_run_;
    std::optional<data::GenerationId> active_generation_;
    std::optional<data::AcquisitionBundle> previous_;
    std::unique_lock<std::mutex> ordinary() {
        std::unique_lock lock(operation_mutex_, std::try_to_lock);
        if (!lock.owns_lock())
            fail(Status::busy, "Projected-light operation is already active");
        return lock;
    }
    // Caller holds context_mutex_. Correlation is boundary validation, not scheduling.
    void step_matches(const data::StepInstance &step) const {
        if (!prepared_ || !active_run_ || step.run_id != *active_run_ ||
            step.repetition_index >= prepared_->repetitions ||
            std::none_of(prepared_->steps.begin(), prepared_->steps.end(),
                         [&](const auto &s) { return s.index == step.step_index; }))
            fail(Status::incompatible, "Step does not belong to the active prepared program");
    }
    void reference_matches(const data::ProgramReference &actual) const {
        const auto &expected = prepared_->identity;
        if (actual.id != expected.id ||
            (expected.hash.get() && (!actual.hash.get() || *actual.hash.get() != *expected.hash.get())))
            fail(Status::incompatible, "Program identity/hash differs from prepared program");
        if (const auto *e = expected.content.get()) {
            const auto *a = actual.content.get();
            if (!a || a->id != e->id || a->type != e->type || a->revision != e->revision ||
                a->hash != e->hash)
                fail(Status::incompatible, "Program content provenance differs from prepared program");
        }
        // An enriched reference must also agree with established prepared provenance.
        if (const auto *a = actual.content.get();
            a && a->hash.get() && expected.hash.get() && *a->hash.get() != *expected.hash.get())
            fail(Status::incompatible, "Program content contradicts prepared hash");
        if (actual.hash.get() && expected.content.get() && expected.content.get()->hash.get() &&
            *actual.hash.get() != *expected.content.get()->hash.get())
            fail(Status::incompatible, "Program hash contradicts prepared content");
    }
    void run_matches(const data::Evidence<data::RunId> &run,
                     const data::Evidence<data::GenerationId> &generation) {
        std::lock_guard lock(context_mutex_);
        if (run.get() && active_run_ && *run.get() != *active_run_)
            fail(Status::incompatible, "Plugin output belongs to another active run");
        if (generation.get() && active_generation_ && *generation.get() != *active_generation_)
            fail(Status::incompatible, "Plugin output belongs to another active generation");
    }
    void timeout(uint32_t t) const {
        if (t > MANTIS_MAX_TIMEOUT_MS || t > graph_.limits.max_call_timeout_ms)
            fail(Status::invalid_argument, "Timeout exceeds plugin limit");
    }
    void program(const data::AcquisitionProgram &p) const {
        auto valid = data::validate(p);
        if (!valid)
            throw Failure(valid.error());
        auto find = [&](const data::ComponentId &id,
                        device::ParticipantKind kind) -> const device::ProjectedComponent & {
            auto it = std::find_if(graph_.components.begin(), graph_.components.end(), [&](const auto &c) {
                return c.descriptor.id == id.id &&
                       (c.kind == kind ||
                        (kind == device::ParticipantKind::controller &&
                         c.kind == device::ParticipantKind::parent &&
                         (device::has_capability(c.descriptor, MANTIS_HARDWARE_TRIGGER_V1) ||
                          device::has_capability(c.descriptor, MANTIS_EMITTER_POWER_CONTROL_V1))));
            });
            if (it == graph_.components.end())
                fail(Status::invalid_argument, "Program participant is outside selected parent");
            return *it;
        };
        for (const auto &c : p.participants.cameras) {
            const auto &selected = find(c.component, device::ParticipantKind::image);
            if (!selected.image_source || c.stream != selected.image_source->stream ||
                c.role != selected.role)
                fail(Status::invalid_argument, "Program camera stream/role differs from selected graph");
        }
        for (const auto &e : p.participants.emitters)
            (void)find(e, device::ParticipantKind::emitter);
        for (const auto &c : p.participants.controllers)
            (void)find(c, device::ParticipantKind::controller);
        // Feasibility/scheduling remain L3/plugin responsibility. This checks only
        // explicitly inaccessible relationships before passing a program onward.
        for (const auto &s : p.steps) {
            for (const auto &camera : s.capture.cameras) {
                const auto &c = find(camera, device::ParticipantKind::image);
                if (std::find(c.capture_modes.begin(), c.capture_modes.end(), *s.capture.mode) ==
                    c.capture_modes.end())
                    fail(Status::invalid_argument, "Unsupported camera capture mode");
            }
            if (s.capture.trigger) {
                const auto &c = find(s.capture.trigger->controller, device::ParticipantKind::controller);
                for (const auto &target : s.capture.trigger->endpoints)
                    if (std::find(c.trigger_endpoints.begin(), c.trigger_endpoints.end(), target.id) ==
                        c.trigger_endpoints.end())
                        fail(Status::invalid_argument, "Unsupported controller endpoint");
            }
        }
    }

  public:
    ProjectedExecutor(std::shared_ptr<Loaded> loaded, device::ProjectedGraph graph, uint32_t t,
                      std::shared_ptr<void> lease)
        : loaded_(std::move(loaded)), graph_(std::move(graph)),
          api_(loaded_->query<MantisProjectedLightV1>(MANTIS_PROJECTED_LIGHT_V1), host_api(),
               graph_.parent.value.c_str(), t, std::move(lease)) {}
    const device::ProjectedGraph &graph() const override {
        return graph_;
    }
    Result<device::ProgramValidation> validate(const data::AcquisitionProgram &p, uint32_t t) override {
        return checked([&] {
            auto operation = ordinary();
            timeout(t);
            program(p);
            semantic::ProgramView view(p);
            ValidationReceiver r;
            return r.finish(api_.validate(view.get(), t, r.emit, &r));
        });
    }
    Result<device::ProgramValidation> prepare(const data::AcquisitionProgram &p, uint32_t t) override {
        return checked([&] {
            auto operation = ordinary();
            {
                std::lock_guard lock(context_mutex_);
                prepared_.reset();
            }
            timeout(t);
            program(p);
            semantic::ProgramView view(p);
            ValidationReceiver r;
            auto snapshot = std::make_shared<const data::AcquisitionProgram>(p);
            auto validated = r.finish(api_.prepare(view.get(), t, r.emit, &r));
            if (validated.accepted) {
                std::lock_guard lock(context_mutex_);
                prepared_ = std::move(snapshot);
            }
            return validated;
        });
    }
    Result<void> start(const data::RunId &run, const data::GenerationId &generation, uint32_t t) override {
        auto r = checked([&] {
            auto operation = ordinary();
            timeout(t);
            {
                std::lock_guard lock(context_mutex_);
                if (!prepared_)
                    fail(Status::invalid_argument, "Start requires a successfully prepared program");
            }
            if (run.id.value.empty() || run.id.value.size() > 256 || generation.id.value.empty() ||
                generation.id.value.size() > 256)
                fail(Status::invalid_argument, "Invalid run/generation identity");
            result(api_.start(run.id.value.c_str(), generation.id.value.c_str(), t));
            {
                std::lock_guard lock(context_mutex_);
                active_run_ = run;
                active_generation_ = generation;
                previous_.reset();
            }
            return true;
        });
        if (!r)
            return std::unexpected(r.error());
        return {};
    }
    Result<std::optional<data::AcquisitionBundle>> next(uint32_t t) override {
        return checked([&]() -> std::optional<data::AcquisitionBundle> {
            auto operation = ordinary();
            timeout(t);
            BundleReceiver r;
            auto rc = api_.next(t, r.emit, &r);
            if (r.error)
                throw Failure(*r.error);
            if (rc && r.calls)
                fail(Status::incompatible, "Failure/not-ready emitted a bundle");
            if (rc == MANTIS_PL_NOT_READY)
                return {};
            result(rc);
            if (r.calls != 1 || !r.value)
                fail(Status::incompatible, "Success without one bundle");
            {
                std::lock_guard lock(context_mutex_);
                if (!active_run_ || r.value->key.run_id != *active_run_ || !prepared_ ||
                    r.value->evidence.program.id != prepared_->identity.id)
                    fail(Status::incompatible, "Bundle does not belong to the selected run/program");
                reference_matches(r.value->evidence.program);
                if (r.value->evidence.step.get())
                    step_matches(*r.value->evidence.step.get());
                for (const auto &trigger : r.value->triggers)
                    step_matches(trigger.step);
                const auto &actual = r.value->evidence.participants;
                const auto &expected = prepared_->participants;
                if (actual.cameras.size() != expected.cameras.size() ||
                    actual.emitters != expected.emitters || actual.controllers != expected.controllers)
                    fail(Status::incompatible,
                         "Bundle participant declaration differs from prepared program");
                for (size_t i = 0; i < actual.cameras.size(); ++i)
                    if (actual.cameras[i].component != expected.cameras[i].component ||
                        actual.cameras[i].stream != expected.cameras[i].stream ||
                        actual.cameras[i].role != expected.cameras[i].role)
                        fail(Status::incompatible, "Bundle camera identity differs from prepared program");
                if (previous_) {
                    auto valid = data::validate_successor(*previous_, *r.value);
                    if (!valid)
                        fail(Status::incompatible, valid.error().message, "projected-light");
                }
                previous_ = *r.value; // immutable image storage remains shared
            }
            return std::move(r.value);
        });
    }
    Result<device::ProjectedStatus> status(uint32_t t) override {
        return checked([&] {
            auto operation = ordinary();
            timeout(t);
            StatusReceiver r;
            auto status = r.finish(api_.status(t, r.emit, &r));
            run_matches(status.run, status.generation);
            if (status.step.get()) {
                std::lock_guard lock(context_mutex_);
                step_matches(*status.step.get());
            }
            return status;
        });
    }
    Result<device::AbortOutcome> abort(data::AcquisitionReason reason, uint32_t t) override {
        return checked([&] {
            timeout(t);
            if (reason < data::AcquisitionReason::none || reason > data::AcquisitionReason::cleanup_failure)
                fail(Status::invalid_argument, "Invalid abort reason");
            AbortReceiver r;
            auto outcome = r.finish(api_.abort(static_cast<uint32_t>(reason), t, r.emit, &r));
            run_matches(outcome.run, outcome.fenced_generation);
            std::set<data::ComponentId> expected, actual;
            for (const auto &component : graph_.components)
                if (component.kind == device::ParticipantKind::emitter)
                    expected.insert(data::ComponentId{component.descriptor.id});
            for (const auto &emitter : outcome.emitters) {
                if (!actual.insert(emitter.emitter).second)
                    fail(Status::incompatible, "Abort reports a duplicate emitter");
            }
            if (actual != expected)
                fail(Status::incompatible, "Abort must report every owned emitter exactly once");
            return outcome;
        });
    }
    Result<void> stop(uint32_t t) override {
        auto r = checked([&] {
            auto operation = ordinary();
            timeout(t);
            result(api_.stop(t));
            return true;
        });
        if (!r)
            return std::unexpected(r.error());
        return {};
    }
    Result<void> close(uint32_t t) override {
        auto r = checked([&] {
            timeout(t);
            result(api_.close(t));
            return true;
        });
        if (!r)
            return std::unexpected(r.error());
        return {};
    }
    Result<std::string> diagnostics(uint32_t t) override {
        return checked([&] {
            auto operation = ordinary();
            timeout(t);
            struct Text {
                uint32_t calls{};
                std::string text;
                bool invalid{};
            } out;
            auto emit = [](void *ctx, const char *text) noexcept {
                auto &o = *static_cast<Text *>(ctx);
                ++o.calls;
                auto rc = sdk::boundary([&] {
                    if (o.calls != 1 || !text) {
                        o.invalid = true;
                        return;
                    }
                    size_t n = 0;
                    while (n <= 1024 && text[n])
                        ++n;
                    if (n > 1024) {
                        o.invalid = true;
                        return;
                    }
                    o.text = {text, n};
                });
                if (rc)
                    o.invalid = true;
                return rc;
            };
            auto rc = api_.diagnostics(t, emit, &out);
            if (out.invalid || (!rc && out.calls != 1) || (rc && out.calls))
                fail(Status::incompatible, "Invalid diagnostic publication");
            result(rc);
            return out.text;
        });
    }
};
} // namespace
std::vector<device::ProjectedGraph> discover_projected_light(const Loaded &loaded, uint32_t t) {
    if (t > MANTIS_MAX_TIMEOUT_MS)
        fail(Status::invalid_argument, "Deadline must be finite");
    const MantisProjectedLightV1 *api{};
    try {
        api = static_cast<const MantisProjectedLightV1 *>(
            loaded.api()->query_interface(MANTIS_PROJECTED_LIGHT_V1));
    } catch (...) {
        fail(Status::plugin_failed, "Plugin query exception");
    }
    if (!api)
        return {};
    if (!sdk::compatible_table(api))
        fail(Status::incompatible, "Incompatible projected-light table");
    if (!api->enumerate || !api->open || !api->validate || !api->prepare || !api->start || !api->next ||
        !api->status || !api->abort || !api->stop || !api->destroy || !api->diagnostics)
        fail(Status::incompatible, "Incomplete projected-light table");
    struct Discovery {
        std::vector<device::ProjectedGraph> graphs;
        std::optional<Error> error;
    } state;
    auto emit = [](void *ctx, const MantisProjectedGraphV1 *v) noexcept -> int {
        auto &s = *static_cast<Discovery *>(ctx);
        try {
            if (s.graphs.size() >= 64)
                fail(Status::incompatible, "Too many projected-light parents");
            auto g = semantic::graph(v);
            for (const auto &prior : s.graphs)
                for (const auto &c : prior.components)
                    for (const auto &d : g.components)
                        if (c.descriptor.id == d.descriptor.id)
                            fail(Status::incompatible, "Duplicate/shared owned component identity");
            s.graphs.push_back(std::move(g));
            return 0;
        } catch (const Failure &e) {
            s.error = e.error;
        } catch (const std::exception &e) {
            s.error = Error{Status::incompatible, e.what(), "projected-light"};
        } catch (...) {
            s.error = Error{Status::incompatible, "Graph callback exception", "projected-light"};
        }
        return MANTIS_PL_ERROR;
    };
    int rc{};
    try {
        rc = api->enumerate(t, emit, &state);
    } catch (...) {
        fail(Status::plugin_failed, "Plugin enumeration exception");
    }
    if (state.error)
        throw Failure(*state.error);
    if (rc && !state.graphs.empty())
        fail(Status::incompatible, "Failed discovery emitted descriptors");
    result(rc);
    return state.graphs;
}
namespace {
struct Ownership {
    std::mutex mutex;
    std::set<std::pair<std::filesystem::path, Id>> parents;
};
std::shared_ptr<void> claim_parent(const std::shared_ptr<Loaded> &loaded, const Id &parent) {
    static auto state = std::make_shared<Ownership>();
    const auto key = std::make_pair(loaded->path(), parent);
    {
        std::lock_guard lock(state->mutex);
        if (!state->parents.insert(key).second)
            fail(Status::busy, "Projected-light parent already owned");
    }
    return std::shared_ptr<void>(loaded.get(), [loaded, key, owner = state](void *) {
        std::lock_guard lock(owner->mutex);
        owner->parents.erase(key);
    });
}
} // namespace
std::unique_ptr<device::ProjectedExecutor> open_projected_light(std::shared_ptr<Loaded> loaded,
                                                                const Id &parent, uint32_t t) {
    auto graphs = discover_projected_light(*loaded, t);
    for (auto &graph : graphs)
        if (graph.parent == parent) {
            if (t > graph.limits.max_call_timeout_ms)
                fail(Status::invalid_argument, "Open timeout exceeds plugin limit");
            auto lease = claim_parent(loaded, parent);
            try {
                return std::make_unique<ProjectedExecutor>(std::move(loaded), std::move(graph), t,
                                                           std::move(lease));
            } catch (const sdk::ContractFailure &e) {
                result(e.status);
                throw;
            }
        }
    fail(Status::not_found, "Projected-light parent not found");
}
std::vector<Registry::ProjectedParent> Registry::projected_light_parents(uint32_t t) {
    std::vector<std::pair<std::string, std::shared_ptr<Loaded>>> entries;
    {
        std::lock_guard lock(mutex_);
        for (const auto &[id, e] : entries_)
            if (e->manifest.kind == "device" && e->loaded && e->state == "registered")
                entries.emplace_back(id, e->loaded);
    }
    std::vector<ProjectedParent> out;
    for (const auto &[id, loaded] : entries) {
        for (auto &graph : discover_projected_light(*loaded, t)) {
            for (auto &c : graph.components)
                c.descriptor.plugin_id = id;
            out.push_back({id, std::move(graph)});
        }
    }
    return out;
}
std::unique_ptr<device::ProjectedExecutor> Registry::open_projected_light(const std::string &id,
                                                                          const Id &parent, uint32_t t) {
    std::shared_ptr<Loaded> loaded;
    {
        auto e = entry(id);
        std::lock_guard lock(mutex_);
        if (e->manifest.kind != "device" || !e->loaded || e->state != "registered")
            fail(Status::unsupported, "Projected-light mode requires an enabled in-process device plugin");
        loaded = e->loaded;
    }
    return plugins::open_projected_light(std::move(loaded), parent, t);
}
} // namespace mantis::plugins
