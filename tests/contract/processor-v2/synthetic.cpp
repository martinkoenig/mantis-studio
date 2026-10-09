// Test-only contract producer: consumes explicitly supplied synthetic observation fixtures.
// Uses the frozen queried C ABI; no image brightness processing or hardware interaction.
#include <cstdlib>
#include <cstring>
#include <mantis/laser_observation_io.hpp>
#include <mantis/sdk.hpp>
#include <mantis/semantic_views.hpp>
namespace {
int describe(uint32_t ms, MantisNodeDescriptorV1 *d) noexcept {
    return mantis::sdk::boundary([&] {
        if (!ms || ms > MANTIS_MAX_TIMEOUT_MS || !mantis::sdk::compatible_table(d))
            throw std::runtime_error("Invalid descriptor request");
        *d = {sizeof(*d),
              1,
              "org.example.synthetic-observation",
              MANTIS_ACQUISITION_BUNDLE,
              MANTIS_LASER_OBSERVATION,
              1,
              1,
              1,
              "cpu"};
    });
}
int process(const MantisHostV1 *host, const MantisSemanticPacketV1 *input, uint32_t ms,
            MantisSemanticEmitV1 emit, void *context) noexcept {
    return mantis::sdk::boundary([&] {
        using namespace mantis;
        if (!ms || ms > MANTIS_MAX_TIMEOUT_MS || !emit)
            fail(Status::invalid_argument, "Invalid call");
        auto packet = plugins::semantic::packet(input, host);
        auto *bundle = std::get_if<data::AcquisitionBundle>(&packet);
        if (!bundle || !bundle->frameset)
            fail(Status::incompatible, "Fixture needs captured bundle");
        const auto *path = std::getenv("MANTIS_SYNTHETIC_OBSERVATION_FIXTURE");
        if (!path)
            fail(Status::invalid_argument, "Explicit immutable MLOBS001 fixture required");
        auto out = data::read_laser_observation(std::filesystem::path(path));
        if (out.context.origin != data::ObservationOrigin::synthetic || !out.key.run_id.get() ||
            *out.key.run_id.get() != bundle->key.run_id)
            fail(Status::incompatible, "Not a matching synthetic fixture");
        auto source =
            std::find_if(bundle->evidence.frames.begin(), bundle->evidence.frames.end(), [&](const auto &f) {
                return f.frame.camera == out.context.source.frame.camera &&
                       f.frame.stream == out.context.source.frame.stream;
            });
        if (source == bundle->evidence.frames.end())
            fail(Status::incompatible, "Explicit fixture source camera/stream not in bundle");
        out.key.sequence = {bundle->key.sequence.value};
        // Copy exact recorded hardware facts, never derive stronger evidence from intent or pixels.
        out.context.source = *source;
        out.context.frameset = bundle->evidence.frameset;
        out.context.bundle = bundle->key;
        out.context.acquisition_evidence = bundle->evidence.key;
        out.context.triggers = bundle->evidence.triggers;
        out.context.clock_mappings = bundle->evidence.clock_mappings;
        out.context.requested_emitters = bundle->evidence.participants.emitters;
        out.context.emitter_evidence = bundle->evidence.emitters;
        for (auto &e : out.context.emitter_evidence) {
            std::erase_if(e.exposure_effective, [&](const auto &f) { return f.frame != source->frame; });
        }
        if (bundle->evidence.step.get())
            out.context.correlation =
                data::ProgramCorrelation{bundle->evidence.program, *bundle->evidence.step.get()};
        else if (bundle->evidence.step.presence() == data::Presence::unavailable)
            out.context.correlation = data::Unavailable{};
        else
            out.context.correlation = data::Unknown{};
        // Stream identity, sequence, synthetic dictionaries, parameters and fixed completion belong to
        // fixture.
        plugins::semantic::PacketView view(data::SemanticPacket{std::move(out)}, host);
        sdk::check(emit(context, view.get()));
    });
}
const MantisProcessorV2 processor{sizeof(processor), 1, describe, process};
int initialize(const MantisHostV1 *h) { return mantis::sdk::compatible(h) ? 0 : 1; }
void shutdown() {}
const void *query(const char *id) {
    return id && !std::strcmp(id, MANTIS_PROCESSOR_V2) ? &processor : nullptr;
}
const MantisPluginV1 plugin{sizeof(plugin), 1,    "org.example.synthetic-observation", "1.0.0", initialize,
                            shutdown,       query};
} // namespace
extern "C" MANTIS_EXPORT const MantisPluginV1 *mantis_plugin_entry(uint32_t version) {
    return version == 1 ? &plugin : nullptr;
}
