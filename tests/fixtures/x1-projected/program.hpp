#pragma once
#include <mantis/projected_light_device.hpp>
namespace x1_fixture {
using namespace mantis;
using namespace std::chrono_literals;
inline data::AcquisitionProgram program(const device::ProjectedGraph &g, uint64_t repeats = 1) {
    namespace d = data;
    d::AcquisitionProgram p;
    p.identity = {{{"x1-fixture-dark-L1-dark-L7-dark"}}, d::Unknown{}, d::Unavailable{}};
    for (const auto &v : g.components) {
        if (v.kind == device::ParticipantKind::image)
            p.participants.cameras.push_back({{v.descriptor.id}, v.image_source->stream, v.role});
        else if (v.kind == device::ParticipantKind::emitter)
            p.participants.emitters.push_back({v.descriptor.id});
        else if (v.kind == device::ParticipantKind::controller)
            p.participants.controllers.push_back({v.descriptor.id});
    }
    const char *labels[]{"DARK-0", "L1", "DARK-2", "L7", "DARK-4"};
    for (uint32_t i = 0; i < 5; ++i) {
        d::AcquisitionStep s;
        s.index = 10 + 3 * i;
        s.label = labels[i];
        for (size_t j = 0; j < p.participants.emitters.size(); ++j)
            s.emitters.push_back({p.participants.emitters[j], (i == 1 && j == 0) || (i == 3 && j == 1)
                                                                  ? d::EmitterState::on
                                                                  : d::EmitterState::off});
        s.capture.mode = d::CaptureMode::free_running;
        for (const auto &c : p.participants.cameras)
            s.capture.cameras.push_back(c.component);
        s.evidence_requirement = d::EvidenceRequirement::commanded_only;
        s.max_duration = 1s;
        p.steps.push_back(std::move(s));
    }
    p.repetitions = repeats;
    p.bounds = {10s, 2s, 128, 256, 256, 64 * 1024 * 1024, 1};
    return p;
}
} // namespace x1_fixture
