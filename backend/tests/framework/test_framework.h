#pragma once

#include <cmath>
#include <functional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <typeinfo>
#include <vector>

namespace hnswhytest {

struct Failure : std::runtime_error {
    explicit Failure(std::string message) : std::runtime_error(std::move(message)) {}
};

// A test that cannot run in this environment (for example an optional dataset
// is absent). The harness reports it separately from a pass, so a green run
// never hides coverage that did not execute.
struct Skip : std::runtime_error {
    explicit Skip(std::string message) : std::runtime_error(std::move(message)) {}
};

struct Case {
    std::string name;
    std::function<void()> fn;
};

std::vector<Case>& registry();

struct Registrar {
    Registrar(std::string name, std::function<void()> fn);
};

}  // namespace hnswhytest

#define HNSWHY_TEST(name)                                                     \
    static void hnswhy_test_##name();                                         \
    static ::hnswhytest::Registrar hnswhy_reg_##name(#name, hnswhy_test_##name);      \
    static void hnswhy_test_##name()

#define CHECK(condition)                                                                  \
    do {                                                                                  \
        if (!(condition)) {                                                               \
            throw ::hnswhytest::Failure(std::string(__FILE__) + ":" + std::to_string(__LINE__) + \
                                    ": CHECK failed: " #condition);                       \
        }                                                                                 \
    } while (false)

#define CHECK_EQ(a, b)                                                                    \
    do {                                                                                  \
        const auto hnswhy_a = (a);                                                            \
        const auto hnswhy_b = (b);                                                            \
        if (!(hnswhy_a == hnswhy_b)) {                                                            \
            std::ostringstream hnswhy_os;                                                     \
            hnswhy_os << __FILE__ << ":" << __LINE__ << ": CHECK_EQ failed: " #a " == " #b    \
                  << " (" << hnswhy_a << " vs " << hnswhy_b << ")";                               \
            throw ::hnswhytest::Failure(hnswhy_os.str());                                         \
        }                                                                                 \
    } while (false)

#define CHECK_NEAR(a, b, eps)                                                             \
    do {                                                                                  \
        const double hnswhy_a = static_cast<double>(a);                                       \
        const double hnswhy_b = static_cast<double>(b);                                       \
        const double hnswhy_eps = static_cast<double>(eps);                                   \
        /* Written as !(diff <= eps) so a NaN difference fails the check. Comparing */       \
        /* NaN > eps is false and would let a broken result pass silently. */                 \
        if (!(std::fabs(hnswhy_a - hnswhy_b) <= hnswhy_eps)) {                                \
            std::ostringstream hnswhy_os;                                                     \
            hnswhy_os << __FILE__ << ":" << __LINE__ << ": CHECK_NEAR failed: " #a " ~ " #b   \
                  << " (" << hnswhy_a << " vs " << hnswhy_b << ", eps " << hnswhy_eps << ")";         \
            throw ::hnswhytest::Failure(hnswhy_os.str());                                         \
        }                                                                                 \
    } while (false)

#define CHECK_THROWS_AS(expr, type)                                                       \
    do {                                                                                  \
        bool hnswhy_caught = false;                                                           \
        std::string hnswhy_wrong;                                                             \
        try {                                                                             \
            (void)(expr);                                                                 \
        } catch (const type&) {                                                           \
            hnswhy_caught = true;                                                             \
        } catch (const std::exception& hnswhy_error) {                                        \
            hnswhy_wrong = std::string(typeid(hnswhy_error).name()) + ": " + hnswhy_error.what();     \
        } catch (...) {                                                                   \
            hnswhy_wrong = "unknown exception type";                                          \
        }                                                                                 \
        if (!hnswhy_caught) {                                                                 \
            throw ::hnswhytest::Failure(std::string(__FILE__) + ":" + std::to_string(__LINE__) + \
                                    ": expected exception " #type ": " #expr +             \
                                    (hnswhy_wrong.empty() ? " (none thrown)"                  \
                                                      : " (got " + hnswhy_wrong + ")"));      \
        }                                                                                 \
    } while (false)
