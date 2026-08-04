#pragma once

#include <cstdint>
#include <cstddef>
#include <cstring>

namespace EndianUtils {
    inline uint32_t swap32(uint32_t value);
    inline uint32_t read_be32(const uint8_t* buffer, size_t offset = 0);
    inline float read_le_float(const uint8_t* buffer, size_t offset = 0);
}

// Inline implementations
inline uint32_t EndianUtils::swap32(uint32_t value) {
    return __builtin_bswap32(value);
}

// memcpy instead of reinterpret_cast: the magic scan advances byte-by-byte,
// so these reads are frequently unaligned, and the cast form is a strict-
// aliasing violation under -O3. GCC compiles the memcpy to one load anyway.
inline uint32_t EndianUtils::read_be32(const uint8_t* buffer, size_t offset) {
    uint32_t v;
    std::memcpy(&v, buffer + offset, sizeof(v));
    return swap32(v);
}

inline float EndianUtils::read_le_float(const uint8_t* buffer, size_t offset) {
    float v;
    std::memcpy(&v, buffer + offset, sizeof(v));
    return v;
}
