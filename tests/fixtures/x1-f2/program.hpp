#pragma once
#include <mantis/projected_light_device.hpp>
namespace x1_f2_fixture {
inline mantis::data::AcquisitionProgram program(const mantis::device::ProjectedGraph &graph, bool on = true) {
    namespace d = mantis::data;
    using namespace std::chrono_literals;
    d::AcquisitionProgram p;
    p.identity = {
        {{on ? "x1-f2-synthetic-finite-on" : "x1-f2-synthetic-off"}}, d::Unknown{}, d::Unavailable{}};
    for (const auto &c : graph.components) {
        if (c.kind == mantis::device::ParticipantKind::emitter)
            p.participants.emitters.push_back({c.descriptor.id});
        if (c.kind == mantis::device::ParticipantKind::controller)
            p.participants.controllers.push_back({c.descriptor.id});
    }
    d::AcquisitionStep step;
    step.index = 17;
    step.label = on ? "Synthetic finite ON" : "Software OFF only";
    step.emitters.push_back({p.participants.emitters.at(0), on ? d::EmitterState::on : d::EmitterState::off});
    step.capture.mode = d::CaptureMode::none;
    step.evidence_requirement = d::EvidenceRequirement::commanded_only;
    step.max_duration = 1s;
    p.steps.push_back(step);
    p.repetitions = 1;
    p.bounds = {2s, 1s, 1, 16, 16, 1024 * 1024, 1};
    return p;
}
} // namespace x1_f2_fixture
