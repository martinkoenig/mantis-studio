#include "private.hpp"
#include <mantis/l1_recorded.hpp>

namespace mantis::alg03 {
using namespace detail;
namespace {
template <class T> T checked(Result<T> r) {
    if (!r)
        throw Failure(r.error());
    return std::move(*r);
}
data::SemanticPublished execute(pipeline::SemanticNode node, schema::DataTypeId type,
                                data::SemanticPublished input, const CancellationToken &token) {
    pipeline::NodeDescriptor source;
    source.id = "recorded-input";
    source.outputs = {{"source", type}};
    pipeline::SemanticGraph graph;
    graph.nodes = {{source, {}}, std::move(node)};
    graph.connections = {{0, 0, 1, 0, 1, pipeline::QueuePolicy::lossless}};
    return checked(pipeline::execute(checked(pipeline::compile(graph)), std::move(input), token)).output;
}
} // namespace
Result<RecordedResult> process_recorded(std::shared_ptr<artifact::Store> store,
                                        const RecordedRequest &request, const CancellationToken &token) {
    return result([&] {
        token.check();
        require(bool(store), "Recorded processing requires Store", Status::invalid_argument);
        const auto &raw = request.extractor.raw_input;
        reference_shape(raw);
        require(raw.type == schema::DataTypeId{"org.mantis.RawCapture", 3} && raw.hash.get() &&
                    raw.revision == 0,
                "Exact RawCapture-3 reference required", Status::incompatible);
        (void)descriptor(*store, raw, {"org.mantis.RawCapture", 3}, token);
        require(request.extractor.subpixel.has_value(), "Recorded processing requires ALG02",
                Status::incompatible);
        // Construct/validate configuration and immutable geometry before reading.
        auto extractor = pipeline::l1_candidate_extractor(request.extractor);
        auto policy = checked(load_policy(*store, request.policy, token));
        require(policy.source_contract == SourceContract::alg02_subpixel_1,
                "Recorded processing requires ALG02 policy", Status::incompatible);
        (void)checked(load_plane(*store, request.plane, token));
        auto bundle = [&] {
            artifact::BundleCaptureReader reader(store, raw.id);
            for (uint64_t ordinal = 0;; ++ordinal) {
                auto selected = reader.next(token);
                require(selected.has_value(), "Selected bundle not found", Status::not_found);
                if (ordinal == request.record)
                    return std::move(*selected);
            }
        }();
        require(bundle.frameset && bundle.evidence.disposition == data::AcquisitionDisposition::captured,
                "Selected bundle is not a captured image bundle", Status::incompatible);
        auto extracted = execute(std::move(extractor), schema::acquisition_bundle,
                                 data::publish(data::SemanticPacket{std::move(bundle)}), token);
        const auto &observation = std::get<data::LaserObservation>(*extracted);
        token.check();
        auto id = store->begin_laser_observation();
        data::ContentReference observation_ref;
        try {
            store->append_laser_observation(id, observation);
            auto d = store->finalize(id, token);
            observation_ref = {id, schema::laser_observation, d.hash, 1};
        } catch (const Failure &e) {
            throw Failure(reserved_error(*store, id, e.error));
        } catch (const std::bad_alloc &) {
            throw Failure(
                reserved_error(*store, id, {Status::busy, "Observation allocation failed", "alg03"}));
        } catch (const std::filesystem::filesystem_error &e) {
            throw Failure(reserved_error(*store, id, filesystem_error(e, "persist recorded observation")));
        }
        auto processed = result([&] {
            Bindings bindings{observation_ref, request.plane, request.policy,
                              request.camera,  request.rig,   request.calibration_selection};
            auto prepared = checked(prepare(*store, bindings, token));
            // prepare requires a persisted exact observation; do not rerun extraction
            // to cross that immutable boundary. The second typed plan consumes the
            // same publication and uses the existing Prepared equality guards.
            auto metric =
                execute(triangulator(prepared), schema::laser_observation, std::move(extracted), token);
            auto packet = std::get<data::Published>(*metric);
            // persist_result owns the reservation, typed readback and failure
            // receipt. No additional fallible work after its success boundary.
            auto persisted = checked(persist_result(*store, prepared, *packet, token));
            return RecordedResult{std::move(observation_ref), std::move(persisted)};
        });
        if (!processed) {
            auto error = processed.error();
            // Prefix the earlier observation receipt rather than passing an
            // existing metric receipt through reserved_error's message truncation.
            auto receipt =
                reserved_error(*store, observation_ref.id, {error.code, "Recorded observation", "alg03"});
            error.message = receipt.message + "; " + error.message;
            throw Failure(std::move(error));
        }
        return std::move(*processed);
    });
}
} // namespace mantis::alg03
