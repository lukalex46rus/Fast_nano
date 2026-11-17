#pragma once
#include <inttypes.h>
#include <stddef.h>

#include "../../../core/Features.h"

#if F_USE_LOGIC && F_USE_LOOPER

#if defined(__GNUC__)
#define LOOPER_INLINE __attribute__((always_inline)) inline
#else
#define LOOPER_INLINE inline
#endif

namespace fn {
namespace looper {

// Компактный набор битовых флагов.
struct Flags {
    uint8_t flags = 0;

    LOOPER_INLINE void set(uint8_t mask) {
        flags |= mask;
    }

    LOOPER_INLINE void clear(uint8_t mask) {
        flags &= uint8_t(~mask);
    }

    LOOPER_INLINE void write(uint8_t mask, bool value) {
        value ? set(mask) : clear(mask);
    }

    LOOPER_INLINE void writeBits(uint8_t mask, uint8_t bits) {
        flags = (flags & uint8_t(~mask)) | (bits & mask);
    }

    LOOPER_INLINE uint8_t read(uint8_t mask) const {
        return flags & mask;
    }

    LOOPER_INLINE bool isSet(uint8_t mask) const {
        return read(mask) == mask;
    }

    LOOPER_INLINE bool isClear(uint8_t mask) const {
        return read(mask) == 0;
    }

    LOOPER_INLINE bool compare(uint8_t mask, uint8_t value) const {
        return read(mask) == value;
    }
};

}  // namespace looper
}  // namespace fn

#endif
