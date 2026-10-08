#pragma once
#include <mantis/projected_light_io.hpp>
#include <set>
namespace mantis::artifact {
// Bounded storage continuity, not a second execution state machine. No hardware policy.
struct BundleState {
    data::ProjectedCaptureHeader header;
    std::optional<data::AcquisitionBundle> previous; // Semantic context only; no mapped pixels retained.
    std::vector<uint64_t> ordinals;
    std::vector<data::EmitterCommand> commands;
    uint64_t bytes{}, count{}, events{};
    explicit BundleState(data::ProjectedCaptureHeader h) : header(std::move(h)) {
        data::validate_capture_header(header);
        ordinals.reserve(header.config.max_correlation_entries);
        commands.reserve(
            std::min<uint64_t>(header.config.max_correlation_entries, header.program.bounds.max_commands));
    }
    void accept(const data::AcquisitionBundle &b) {
        auto check = [](bool ok, const char *s) {
            if (!ok)
                fail(Status::corrupt, s, "bundle-capture");
        };
        auto valid = data::validate(b);
        if (!valid)
            throw Failure(valid.error());
        check(b.key.run_id == header.run, "Recorded RunId changed");
        check(data::same_program_reference(b.evidence.program, header.program.identity),
              "Recorded program reference changed");
        check(data::same_participants(b.evidence.participants, header.program.participants),
              "Recorded participants changed");
        if (previous) {
            check(previous->key.sequence.value != UINT64_MAX &&
                      b.key.sequence.value == previous->key.sequence.value + 1,
                  "Bundle sequence gap/reuse");
            auto successor = data::validate_successor(*previous, b);
            if (!successor)
                throw Failure(successor.error());
        }
        for (const auto &p : b.evidence.causal_predecessors)
            check(p.run_id == header.run &&
                      std::binary_search(ordinals.begin(), ordinals.end(), p.ordinal.value),
                  "Missing recorded causal predecessor");
        auto step = [&](const data::StepInstance &s) {
            check(s.run_id == header.run && s.repetition_index < header.program.repetitions &&
                      std::any_of(
                          header.program.steps.begin(), header.program.steps.end(),
                          [&](const auto &x) { return x.index == s.step_index; }),
                  "Step outside persisted program");
        };
        if (b.evidence.step.get())
            step(*b.evidence.step.get());
        for (const auto &t : b.triggers)
            step(t.step);
        check(ordinals.size() < header.config.max_correlation_entries &&
                  b.triggers.size() < header.program.bounds.max_events &&
                  events <= header.program.bounds.max_events - 1 - b.triggers.size(),
              "Recorded correlation/event bound exceeded");
        std::vector<data::EmitterCommand> added;
        for (const auto &emitter : b.evidence.emitters) {
            if (const auto *command = emitter.commanded.get()) {
                auto find = [&](const auto &values) {
                    return std::find_if(values.begin(), values.end(),
                                        [&](const auto &old) { return old.request == command->request; });
                };
                const auto old = find(commands), pending = find(added);
                if (old != commands.end())
                    check(data::same_emitter_command(*old, *command),
                          "Contradictory recorded command identity");
                else if (pending != added.end())
                    check(data::same_emitter_command(*pending, *command), "Contradictory command identity");
                else {
                    check(commands.size() < header.program.bounds.max_commands &&
                              added.size() < header.program.bounds.max_commands - commands.size() &&
                              commands.size() + added.size() < header.config.max_correlation_entries,
                          "Recorded command/correlation bound exceeded");
                    added.push_back(*command);
                }
            }
        }
        auto n = data::bundle_encoded_size(b);
        check(n <= header.program.bounds.max_bytes && bytes <= header.program.bounds.max_bytes - n,
              "Recorded byte bound exceeded");
        auto context = b;
        context.frameset.reset(); // retain only semantic successor context
        commands.insert(commands.end(), std::make_move_iterator(added.begin()),
                        std::make_move_iterator(added.end()));
        previous = std::move(context);
        ordinals.push_back(b.evidence.key.ordinal.value);
        bytes += n;
        ++count;
        events += 1 + b.triggers.size();
    }
};
} // namespace mantis::artifact
