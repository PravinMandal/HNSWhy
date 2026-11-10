#pragma once

#include <cmath>
#include <functional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace vftest {

struct Failure : std::runtime_error {
    explicit Failure(std::string message) : std::runtime_error(std::move(message)) {}
};

struct Case {
    std::string name;
    std::function<void()> fn;
};

std::vector<Case>& registry();

struct Registrar {
    Registrar(std::string name, std::function<void()> fn);
};

}  // namespace vftest

#define VF_TEST(name)                                                     \
    static void vf_test_##name();                                         \
    static ::vftest::Registrar vf_reg_##name(#name, vf_test_##name);      \
    static void vf_test_##name()

#define CHECK(condition)                                                                  \
    do {                                                                                  \
        if (!(condition)) {                                                               \
            throw ::vftest::Failure(std::string(__FILE__) + ":" + std::to_string(__LINE__) + \
                                    ": CHECK failed: " #condition);                       \
        }                                                                                 \
    } while (false)

#define CHECK_EQ(a, b)                                                                    \
    do {                                                                                  \
        const auto& vf_a = (a);                                                           \
        const auto& vf_b = (b);                                                           \
        if (!(vf_a == vf_b)) {                                                            \
            std::ostringstream vf_os;                                                     \
            vf_os << __FILE__ << ":" << __LINE__ << ": CHECK_EQ failed: " #a " == " #b    \
                  << " (" << vf_a << " vs " << vf_b << ")";                               \
            throw ::vftest::Failure(vf_os.str());                                         \
        }                                                                                 \
    } while (false)

#define CHECK_NEAR(a, b, eps)                                                             \
    do {                                                                                  \
        const double vf_a = static_cast<double>(a);                                       \
        const double vf_b = static_cast<double>(b);                                       \
        const double vf_eps = static_cast<double>(eps);                                   \
        if (std::fabs(vf_a - vf_b) > vf_eps) {                                            \
            std::ostringstream vf_os;                                                     \
            vf_os << __FILE__ << ":" << __LINE__ << ": CHECK_NEAR failed: " #a " ~ " #b   \
                  << " (" << vf_a << " vs " << vf_b << ", eps " << vf_eps << ")";         \
            throw ::vftest::Failure(vf_os.str());                                         \
        }                                                                                 \
    } while (false)

#define CHECK_THROWS_AS(expr, type)                                                       \
    do {                                                                                  \
        bool vf_caught = false;                                                           \
        try {                                                                             \
            (void)(expr);                                                                 \
        } catch (const type&) {                                                           \
            vf_caught = true;                                                             \
        } catch (...) {                                                                   \
        }                                                                                 \
        if (!vf_caught) {                                                                 \
            throw ::vftest::Failure(std::string(__FILE__) + ":" + std::to_string(__LINE__) + \
                                    ": expected exception: " #expr);                      \
        }                                                                                 \
    } while (false)
