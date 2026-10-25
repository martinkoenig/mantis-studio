#include "serial-settings.hpp"
#include <cerrno>
#include <cstddef>
#include <termios.h>

namespace x1::f2::posix {
namespace {
constexpr tcflag_t input_off =
    IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL | IXON | IXOFF | IXANY | INPCK;
constexpr tcflag_t local_off = ECHO | ECHONL | ICANON | ISIG | IEXTEN;
constexpr tcflag_t control_off = CSIZE | PARENB | PARODD | CSTOPB | CRTSCTS;
bool equal(const termios &a, const termios &b) noexcept {
    if (a.c_iflag != b.c_iflag || a.c_oflag != b.c_oflag || a.c_cflag != b.c_cflag ||
        a.c_lflag != b.c_lflag || ::cfgetispeed(&a) != ::cfgetispeed(&b) ||
        ::cfgetospeed(&a) != ::cfgetospeed(&b))
        return false;
    for (std::size_t i = 0; i < NCCS; ++i)
        if (a.c_cc[i] != b.c_cc[i])
            return false;
    return true;
}
bool effective(const termios &m) noexcept {
    return !(m.c_iflag & input_off) && !(m.c_oflag & OPOST) && !(m.c_lflag & local_off) &&
           !(m.c_cflag & (PARENB | PARODD | CSTOPB | CRTSCTS)) && (m.c_cflag & CSIZE) == CS8 &&
           (m.c_cflag & (CREAD | CLOCAL)) == (CREAD | CLOCAL) && m.c_cc[VMIN] == 1 && m.c_cc[VTIME] == 0 &&
           ::cfgetispeed(&m) == B115200 && ::cfgetospeed(&m) == B115200;
}
void rollback(int fd, const termios &original, SerialResult &result) noexcept {
    result.rollback_attempted = true;
    if (::tcsetattr(fd, TCSANOW, &original) < 0) {
        result.rollback_error = errno;
        return;
    }
    termios restored{};
    if (::tcgetattr(fd, &restored) < 0)
        result.rollback_error = errno;
    else if (!equal(restored, original))
        result.rollback_error = EIO;
}
} // namespace
bool SerialProfile::supported() const noexcept {
    return baud == 115200 && data_bits == 8 && stop_bits == 1 && !parity && !software_flow && !hardware_flow;
}
SerialResult configure_serial(int fd, SerialProfile profile) noexcept {
    if (!profile.supported())
        return {SerialStatus::unsupported, EINVAL};
    termios original{};
    if (::tcgetattr(fd, &original) < 0)
        return {SerialStatus::inspect_failed, errno};
    auto desired = original;
    desired.c_iflag &= ~input_off;
    desired.c_oflag &= ~OPOST;
    desired.c_lflag &= ~local_off;
    desired.c_cflag = (desired.c_cflag & ~control_off) | CS8 | CREAD | CLOCAL;
    desired.c_cc[VMIN] = 1;
    desired.c_cc[VTIME] = 0;
    if (::cfsetispeed(&desired, B115200) < 0 || ::cfsetospeed(&desired, B115200) < 0)
        return {SerialStatus::unsupported, errno};
    if (::tcsetattr(fd, TCSANOW, &desired) < 0) {
        SerialResult result{SerialStatus::apply_failed, errno};
        rollback(fd, original, result);
        return result;
    }
    termios actual{};
    const auto readback = ::tcgetattr(fd, &actual);
    if (readback < 0 || !effective(actual)) {
        SerialResult result{SerialStatus::verify_failed, readback < 0 ? errno : EIO};
        rollback(fd, original, result);
        return result;
    }
    return {SerialStatus::configured};
}
} // namespace x1::f2::posix
