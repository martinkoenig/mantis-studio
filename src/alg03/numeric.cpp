#include "private.hpp"
#include <algorithm>
#include <cmath>

namespace mantis::alg03 {
namespace {
struct Numeric {};
struct I {
    double lo{}, hi{};
};
void finite(double x) {
    if (!std::isfinite(x))
        throw Numeric{};
}
I point(double x) {
    finite(x);
    return {x, x};
}
double lower(double x) {
    finite(x);
    return std::nextafter(x, -std::numeric_limits<double>::infinity());
}
double upper(double x) {
    finite(x);
    return std::nextafter(x, std::numeric_limits<double>::infinity());
}
I operator-(I x) {
    return {-x.hi, -x.lo};
}
I operator+(I a, I b) {
    if (a.lo == 0 && a.hi == 0)
        return b;
    if (b.lo == 0 && b.hi == 0)
        return a;
    if (a.lo == a.hi && b.lo == b.hi && a.lo == -b.lo)
        return point(0);
    return {lower(a.lo + b.lo), upper(a.hi + b.hi)};
}
I operator-(I a, I b) {
    return a + (-b);
}
I operator*(I a, I b) {
    if ((a.lo == 0 && a.hi == 0) || (b.lo == 0 && b.hi == 0))
        return point(0);
    if (a.lo == 1 && a.hi == 1)
        return b;
    if (b.lo == 1 && b.hi == 1)
        return a;
    if (a.lo == -1 && a.hi == -1)
        return -b;
    if (b.lo == -1 && b.hi == -1)
        return -a;
    std::array<double, 4> v{a.lo * b.lo, a.lo * b.hi, a.hi * b.lo, a.hi * b.hi};
    for (double x : v)
        finite(x);
    return {lower(*std::min_element(v.begin(), v.end())), upper(*std::max_element(v.begin(), v.end()))};
}
I operator/(I a, I b) {
    if (b.lo <= 0 && b.hi >= 0)
        throw Numeric{};
    if (b.lo == 1 && b.hi == 1)
        return a;
    if (b.lo == -1 && b.hi == -1)
        return -a;
    if (a.lo == 0 && a.hi == 0)
        return point(0);
    return a * I{lower(1 / b.hi), upper(1 / b.lo)};
}
I square(I a) {
    if (a.lo == 0 && a.hi == 0)
        return point(0);
    if (a.lo == a.hi && (a.lo == 1 || a.lo == -1))
        return point(1);
    const double hi = std::max(a.lo * a.lo, a.hi * a.hi);
    const double lo = a.lo <= 0 && a.hi >= 0 ? 0 : std::min(a.lo * a.lo, a.hi * a.hi);
    return {lo == 0 ? 0 : std::max(0., lower(lo)), upper(hi)};
}
I root(I a) {
    if (a.lo < 0)
        throw Numeric{};
    if (a.lo == a.hi && (a.lo == 0 || a.lo == 1))
        return a;
    return {a.lo == 0 ? 0 : std::max(0., lower(std::sqrt(a.lo))), upper(std::sqrt(a.hi))};
}
I abs_i(I a) {
    return {a.lo <= 0 && a.hi >= 0 ? 0 : std::min(std::abs(a.lo), std::abs(a.hi)),
            std::max(std::abs(a.lo), std::abs(a.hi))};
}
using V = std::array<double, 3>;
using VI = std::array<I, 3>;
I norm(const VI &v) {
    const I sum = square(v[0]) + square(v[1]) + square(v[2]);
    // The exact sum of squares is nonnegative. Outward addition can put
    // nextafter(0, -infinity) below that known domain when residuals span zero.
    return root({std::max(0., sum.lo), sum.hi});
}
I norm2(const std::array<I, 2> &v) {
    const I sum = square(v[0]) + square(v[1]);
    return root({std::max(0., sum.lo), sum.hi});
}
I dot(const V &a, const VI &b) {
    I v = point(0);
    for (size_t i = 0; i < 3; ++i)
        v = v + point(a[i]) * b[i];
    return v;
}
VI points(const V &v) {
    return {point(v[0]), point(v[1]), point(v[2])};
}
struct Model {
    const calibration::PinholeBrown5 &m;
    template <class T> std::array<T, 2> forward(T x, T y) const {
        auto c = [](double v) -> T {
            if constexpr (std::is_same_v<T, I>)
                return point(v);
            else
                return v;
        };
        T r = x * x + y * y, a = c(1) + c(m.k1) * r + c(m.k2) * r * r + c(m.k3) * r * r * r;
        return {x * a + c(2 * m.p1) * x * y + c(m.p2) * (r + c(2) * x * x),
                y * a + c(m.p1) * (r + c(2) * y * y) + c(2 * m.p2) * x * y};
    }
    std::array<double, 3> jac(double x, double y) const {
        double r = x * x + y * y, a = 1 + m.k1 * r + m.k2 * r * r + m.k3 * r * r * r,
               b = m.k1 + 2 * m.k2 * r + 3 * m.k3 * r * r;
        return {a + 2 * x * x * b + 2 * m.p1 * y + 6 * m.p2 * x, 2 * x * y * b + 2 * m.p1 * x + 2 * m.p2 * y,
                a + 2 * y * y * b + 6 * m.p1 * y + 2 * m.p2 * x};
    }
};
// Interval coefficients must be formed as interval products, not pre-rounded
// double coefficients (2*p, 6*p, etc.).
std::array<I, 2> forward(const Model &m, I x, I y) {
    I r = square(x) + square(y),
      a = point(1) + point(m.m.k1) * r + point(m.m.k2) * square(r) + point(m.m.k3) * square(r) * r;
    return {x * a + point(2) * point(m.m.p1) * x * y + point(m.m.p2) * (r + point(2) * square(x)),
            y * a + point(m.m.p1) * (r + point(2) * square(y)) + point(2) * point(m.m.p2) * x * y};
}
struct Inverse {
    double x{}, y{}, residual{}, delta{};
    std::array<I, 2> enclosure;
};
std::optional<Inverse> certify(const Model &m, double x, double y, std::array<I, 2> target, const Policy &p,
                               double gamma) {
    auto f = forward(m, point(x), point(y));
    std::array<I, 2> r{f[0] - target[0], f[1] - target[1]};
    I residual = norm2({point(m.m.fx) * r[0], point(m.m.fy) * r[1]});
    double delta = (norm2(r) / point(gamma)).hi;
    finite(delta);
    const double margin =
        std::min((point(p.domain.a) - abs_i(point(x))).lo, (point(p.domain.b) - abs_i(point(y))).lo);
    if (delta > 0 && !(margin > delta))
        return {};
    if (delta == 0 && margin < 0)
        return {};
    return Inverse{x, y, residual.hi, delta, {point(x) + I{-delta, delta}, point(y) + I{-delta, delta}}};
}
struct Mapped {
    VI position, velocity;
    I q, s, tau, depth, cosine;
};
Mapped map(const std::array<I, 2> &xy, const spatial::Transform &t, const Plane &p) {
    VI h{xy[0], xy[1], point(1)};
    I length = norm(h);
    VI w;
    for (size_t i = 0; i < 3; ++i)
        w[i] = h[i] / length;
    VI v;
    for (size_t i = 0; i < 3; ++i) {
        v[i] = point(0);
        for (size_t j = 0; j < 3; ++j)
            v[i] = v[i] + point(t.matrix[i * 4 + j]) * w[j];
    }
    V o{t.matrix[3], t.matrix[7], t.matrix[11]};
    I s = dot(p.normal_xyz, points(o)) + point(p.offset_mm), q = dot(p.normal_xyz, v);
    I cosine = abs_i(q) / (norm(points(p.normal_xyz)) * norm(v));
    I tau = (-s) / q;
    VI position;
    for (size_t i = 0; i < 3; ++i)
        position[i] = point(o[i]) + tau * v[i];
    return {position, v, q, s, tau, tau * w[2], cosine};
}
double radius(const VI &box, const V &centre) {
    VI differences;
    for (size_t i = 0; i < 3; ++i)
        differences[i] = abs_i(box[i] - point(centre[i]));
    return norm(differences).hi;
}
bool contained(const Mapped &m, const Policy &p) {
    if (m.q.lo <= 0 && m.q.hi >= 0)
        return false;
    if (m.tau.lo <= 0 || m.depth.lo < p.z_min_mm || m.depth.hi > p.z_max_mm ||
        m.cosine.lo < p.min_incidence_cosine)
        return false;
    for (auto x : m.position)
        if (x.lo < -p.max_abs_position_mm || x.hi > p.max_abs_position_mm)
            return false;
    return true;
}
} // namespace

Result<double> certify_domain(const calibration::PinholeBrown5 &m, const Policy::Domain &d) {
    return detail::result([&] {
        detail::require(std::numeric_limits<double>::is_iec559 && sizeof(double) == 8,
                        "IEEE754 binary64 required", Status::unsupported);
        auto valid = calibration::validate_camera_model(m);
        if (!valid)
            throw Failure(valid.error());
        detail::require(std::isfinite(d.a) && d.a > 0 && d.a <= 16 && std::isfinite(d.b) && d.b > 0 &&
                            d.b <= 16 && m.fx >= 1e-12 && m.fx <= 1e12 && m.fy >= 1e-12 && m.fy <= 1e12,
                        "ALG03 inverse numeric domain", Status::incompatible);
        try {
            I a = point(d.a), b = point(d.b), rho = square(a) + square(b);
            I kr1 = point(std::abs(m.k1)), kr2 = point(std::abs(m.k2)), kr3 = point(std::abs(m.k3));
            I radial = kr1 * rho + kr2 * square(rho) + kr3 * square(rho) * rho;
            I max_b = kr1 + point(2) * kr2 * rho + point(3) * kr3 * square(rho);
            I p1 = point(std::abs(m.p1)), p2 = point(std::abs(m.p2));
            I off = point(2) * p1 * a + point(2) * p2 * b;
            I row1 = point(2) * p1 * b + point(6) * p2 * a + off,
              row2 = point(6) * p1 * b + point(2) * p2 * a + off;
            I e = radial + point(2) * rho * max_b + point(std::max(row1.hi, row2.hi));
            const double gamma = (point(1) - e).lo;
            detail::require(gamma >= .5, "Brown5 domain not certified strongly monotone",
                            Status::incompatible);
            return gamma;
        } catch (const Numeric &) {
            fail(Status::incompatible, "Brown5 domain certificate overflow", "alg03");
        }
    });
}

Sample intersect(const calibration::PinholeBrown5 &camera, const spatial::Transform &t, const Plane &plane,
                 const Policy &policy, float u, float v, const CancellationToken &token) {
    detail::require(policy.uncertainty_requirement != Uncertainty::require_declared_bound ||
                        policy.max_declared_model_error_mm.has_value(),
                    "Declared bound policy allowance required", Status::invalid_argument);
    Sample out;
    auto diagnostic = [&](size_t n, double x) {
        finite(x);
        out.diagnostics[n] = x;
        out.diagnostic_valid |= 1u << n;
    };
    auto gamma_result = certify_domain(camera, policy.domain);
    if (!gamma_result)
        throw Failure(gamma_result.error());
    const double gamma = *gamma_result;
    token.check();
    try {
        Model model{camera};
        std::array<I, 2> target{(point(double(u)) - point(camera.cx)) / point(camera.fx),
                                (point(double(v)) - point(camera.cy)) / point(camera.fy)};
        const double xd = (double(u) - camera.cx) / camera.fx, yd = (double(v) - camera.cy) / camera.fy;
        finite(xd);
        finite(yd);
        double x = 0, y = 0;
        std::optional<Inverse> inverse;
        for (unsigned iteration = 0; iteration < 100; ++iteration) {
            token.check();
            auto f = model.forward(x, y);
            double rx = f[0] - xd, ry = f[1] - yd;
            double residual = std::hypot(rx, ry);
            finite(residual);
            // Initial exact root must converge/certify BEFORE seeking a strictly
            // decreasing Newton trial. This also covers the optical centre.
            if (std::hypot(camera.fx * rx, camera.fy * ry) <= 1e-9) {
                inverse = certify(model, x, y, target, policy, gamma);
                if (inverse && inverse->residual <= 1e-9)
                    break;
                out.reasons = inverse_uncertified;
                return out;
            }
            auto j = model.jac(x, y);
            double det = j[0] * j[2] - j[1] * j[1];
            finite(det);
            if (det <= 0) {
                out.reasons = inverse_failure;
                return out;
            }
            double dx = (j[2] * rx - j[1] * ry) / det, dy = (-j[1] * rx + j[0] * ry) / det;
            bool moved = false;
            for (unsigned trial = 0; trial <= 20; ++trial) {
                token.check();
                double alpha = std::ldexp(1., -static_cast<int>(trial));
                double nx = x - alpha * dx, ny = y - alpha * dy;
                if (!std::isfinite(nx) || !std::isfinite(ny) || std::abs(nx) > policy.domain.a ||
                    std::abs(ny) > policy.domain.b)
                    continue;
                auto nf = model.forward(nx, ny);
                if (std::hypot(nf[0] - xd, nf[1] - yd) < residual) {
                    x = nx;
                    y = ny;
                    moved = true;
                    break;
                }
            }
            if (!moved) {
                out.reasons = inverse_failure;
                return out;
            }
        }
        if (!inverse) {
            out.reasons = inverse_failure;
            return out;
        }
        diagnostic(0, inverse->residual);
        diagnostic(1, inverse->delta);
        const double hn = std::hypot(x, y, 1.);
        V w{x / hn, y / hn, 1 / hn}, velocity{}, origin{t.matrix[3], t.matrix[7], t.matrix[11]};
        for (size_t i = 0; i < 3; ++i)
            for (size_t j = 0; j < 3; ++j)
                velocity[i] += t.matrix[i * 4 + j] * w[j];
        double s = plane.offset_mm, q = 0;
        for (size_t i = 0; i < 3; ++i) {
            s += plane.normal_xyz[i] * origin[i];
            q += plane.normal_xyz[i] * velocity[i];
        }
        finite(s);
        diagnostic(2, q);
        // Check zero/indeterminate denominators without dividing first.
        VI ih{inverse->enclosure[0], inverse->enclosure[1], point(1)};
        I length = norm(ih);
        VI iv;
        for (size_t i = 0; i < 3; ++i) {
            iv[i] = point(0);
            for (size_t j = 0; j < 3; ++j)
                iv[i] = iv[i] + point(t.matrix[i * 4 + j]) * (ih[j] / length);
        }
        I si = dot(plane.normal_xyz, points(origin)) + point(plane.offset_mm), qi = dot(plane.normal_xyz, iv);
        if (qi.lo == 0 && qi.hi == 0)
            out.reasons |= parallel;
        if (si.lo == 0 && si.hi == 0)
            out.reasons |= camera_on_plane;
        if (out.reasons)
            return out;
        if ((qi.lo <= 0 && qi.hi >= 0) || (si.lo <= 0 && si.hi >= 0)) {
            out.reasons = numeric_guard;
            return out;
        }
        Mapped box = map(inverse->enclosure, t, plane);
        double c = std::abs(q) / (std::hypot(plane.normal_xyz[0], plane.normal_xyz[1], plane.normal_xyz[2]) *
                                  std::hypot(velocity[0], velocity[1], velocity[2]));
        diagnostic(3, c);
        if (box.cosine.hi < policy.min_incidence_cosine) {
            out.reasons = near_parallel;
            return out;
        }
        if (box.cosine.lo < policy.min_incidence_cosine) {
            out.reasons = numeric_guard;
            return out;
        }
        if (plane.side == Side::camera_on_positive_side && !(si.lo > 0 && qi.hi < 0)) {
            out.reasons = backside;
            return out;
        }
        const double tau = -s / q, depth = tau * w[2];
        diagnostic(4, tau);
        diagnostic(5, depth);
        if (box.tau.hi <= 0 || box.depth.hi <= 0) {
            out.reasons = behind_camera;
            return out;
        }
        if (box.tau.lo <= 0 || box.depth.lo <= 0) {
            out.reasons = numeric_guard;
            return out;
        }
        V nominal;
        for (size_t i = 0; i < 3; ++i) {
            nominal[i] = origin[i] + tau * velocity[i];
            finite(nominal[i]);
        }
        if (box.depth.hi < policy.z_min_mm || box.depth.lo > policy.z_max_mm)
            out.reasons |= depth_or_position_domain;
        for (auto b : box.position)
            if (b.hi < -policy.max_abs_position_mm || b.lo > policy.max_abs_position_mm)
                out.reasons |= depth_or_position_domain;
        if (out.reasons)
            return out;
        if (!contained(box, policy)) {
            out.reasons = numeric_guard;
            return out;
        }
        const double error = radius(box.position, nominal);
        diagnostic(6, error);
        if (error > policy.max_numeric_error_mm) {
            out.reasons = numeric_guard;
            return out;
        }
        try {
            if (plane.parameter_bounds && policy.declared_ray_bounds) {
                auto pb = *plane.parameter_bounds;
                auto rb = *policy.declared_ray_bounds;
                I nn = norm(points(plane.normal_xyz)), on = norm(points(origin)), vn = norm(box.velocity);
                I eo = point(rb.origin_l2_mm), ev = point(rb.direction_l2), en = point(pb.normal_l2),
                  ed = point(pb.offset_abs_mm);
                I es = nn * eo + on * en + en * eo + ed, eq = nn * ev + vn * en + en * ev;
                if (eq.hi < abs_i(box.q).lo) {
                    I dt = (es + abs_i(box.tau) * eq) / (abs_i(box.q) - eq);
                    diagnostic(7, (eo + abs_i(box.tau) * ev + dt * (vn + ev)).hi);
                }
            }
        } catch (const Numeric &) {
            out.diagnostic_valid &= ~(1u << 7);
            out.diagnostics[7] = 0;
        }
        if (policy.uncertainty_requirement == Uncertainty::require_declared_bound &&
            (!(out.diagnostic_valid & (1u << 7)) ||
             out.diagnostics[7] > *policy.max_declared_model_error_mm)) {
            out.reasons = declared_bound_unavailable_or_exceeded;
            return out;
        }
        // Quantization enclosure reuses the root and strong-monotonicity bound;
        // it does not run four more Newton solves or exceed the trial cap.
        try {
            auto cell = [](float f) {
                double half =
                    std::max(double(f) - double(std::nextafter(f, -std::numeric_limits<float>::infinity())),
                             double(std::nextafter(f, std::numeric_limits<float>::infinity())) - double(f)) /
                    2;
                return point(double(f)) + I{-half, half};
            };
            std::array<I, 2> rounded{(cell(u) - point(camera.cx)) / point(camera.fx),
                                     (cell(v) - point(camera.cy)) / point(camera.fy)};
            auto quantized = certify(model, x, y, rounded, policy, gamma);
            if (quantized) {
                auto qb = map(quantized->enclosure, t, plane);
                if (contained(qb, policy))
                    diagnostic(8, radius(qb.position, nominal));
            }
        } catch (const Numeric &) { /* Optional diagnostic remains unavailable, not zero uncertainty. */
        }
        out.position = nominal;
        return out;
    } catch (const Numeric &) {
        out.reasons |= numeric_guard;
        out.position = {};
        return out;
    }
}
} // namespace mantis::alg03
