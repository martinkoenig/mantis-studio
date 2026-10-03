#pragma once
#include <mantis/base.hpp>
#include <map>
#include <optional>
#include <queue>
#include <set>
#include <vector>
namespace mantis::spatial {
struct CoordinateFrame {
    Id id;
    std::string name;
};
inline const CoordinateFrame world{{"org.mantis.world"}, "World (+X right, +Y forward, +Z up), millimeters"};
struct Transform {
    CoordinateFrame target, source;
    std::array<double, 16> matrix{1, 0, 0, 0, 0, 1, 0, 0,
                                  0, 0, 1, 0, 0, 0, 0, 1}; // row-major T_target_from_source
    std::optional<std::array<double, 36>> covariance;
    std::array<double, 3> apply(std::array<double, 3> p) const {
        std::array<double, 3> o{};
        for (size_t r = 0; r < 3; ++r)
            o[r] = matrix[r * 4] * p[0] + matrix[r * 4 + 1] * p[1] + matrix[r * 4 + 2] * p[2] +
                   matrix[r * 4 + 3];
        return o;
    }
};
using Pose = Transform;
inline Transform compose(const Transform &a, const Transform &b) {
    if (a.source.id != b.target.id)
        fail(Status::invalid_argument, "Transform frames do not compose");
    Transform c;
    c.target = a.target;
    c.source = b.source;
    c.matrix.fill(0);
    for (size_t i = 0; i < 4; ++i)
        for (size_t j = 0; j < 4; ++j)
            for (size_t k = 0; k < 4; ++k)
                c.matrix[i * 4 + j] += a.matrix[i * 4 + k] * b.matrix[k * 4 + j];
    return c;
}
class TransformGraph {
    uint64_t version_{};
    std::vector<Transform> edges_;

  public:
    uint64_t version() const {
        return version_;
    }
    TransformGraph with(Transform t) const {
        auto copy = *this;
        for (auto &e : copy.edges_)
            if (e.source.id == t.source.id && e.target.id == t.target.id) {
                e = std::move(t);
                ++copy.version_;
                return copy;
            }
        copy.edges_.push_back(std::move(t));
        ++copy.version_;
        return copy;
    }
    Result<Transform> resolve(const CoordinateFrame &target, const CoordinateFrame &source) const {
        Transform identity;
        identity.source = source;
        identity.target = source;
        std::queue<Transform> q;
        q.push(identity);
        std::set<Id> seen;
        while (!q.empty()) {
            auto p = q.front();
            q.pop();
            if (p.target.id == target.id)
                return p;
            if (!seen.insert(p.target.id).second)
                continue;
            for (const auto &e : edges_)
                if (e.source.id == p.target.id)
                    q.push(compose(e, p));
        }
        return std::unexpected(Error{Status::not_found, "No directed transform path", "spatial"});
    }
};
} // namespace mantis::spatial
