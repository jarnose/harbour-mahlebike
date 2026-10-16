#pragma once

// Shared by the Qt-free test programs: hex in, hex out, and a check macro
// that reports the failing expression instead of aborting at the first one.
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

inline std::vector<uint8_t> fromHex(const std::string &hex)
{
    std::vector<uint8_t> out;
    for (size_t i = 0; i + 1 < hex.size(); i += 2)
        out.push_back(static_cast<uint8_t>(std::stoi(hex.substr(i, 2), nullptr, 16)));
    return out;
}

template <typename Container>
inline std::string toHex(const Container &bytes)
{
    static const char *digits = "0123456789abcdef";
    std::string out;
    for (uint8_t b : bytes) {
        out += digits[b >> 4];
        out += digits[b & 15];
    }
    return out;
}

static int g_failures = 0;
static int g_checks = 0;

#define CHECK(cond)                                                              \
    do {                                                                         \
        ++g_checks;                                                              \
        if (!(cond)) {                                                           \
            ++g_failures;                                                        \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        }                                                                        \
    } while (0)

#define CHECK_EQ(a, b)                                                           \
    do {                                                                         \
        ++g_checks;                                                              \
        if (!((a) == (b))) {                                                     \
            ++g_failures;                                                        \
            std::fprintf(stderr, "FAIL %s:%d: %s == %s\n", __FILE__, __LINE__,   \
                         #a, #b);                                                \
        }                                                                        \
    } while (0)

inline int finish(const char *name)
{
    std::printf("%s: %d checks, %d failures\n", name, g_checks, g_failures);
    return g_failures ? 1 : 0;
}
