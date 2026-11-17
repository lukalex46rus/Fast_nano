#pragma once

#include "../../../core/Features.h"

#if F_USE_LOGIC && F_USE_LOOPER

#ifdef __AVR__
#define LOOPER_MAKE_CALLBACK(name, type, ...) typedef type (*name)(__VA_ARGS__)
#define LOOPER_CALLBACK(name, type, ...) type (*name)(__VA_ARGS__)
#else
#include <functional>
#define LOOPER_MAKE_CALLBACK(name, type, ...) typedef std::function<type(__VA_ARGS__)> name
#define LOOPER_CALLBACK(name, type, ...) std::function<type(__VA_ARGS__)> name
#endif

#define LOOPER_CONCAT_IMPL(a, b) a##b
#define LOOPER_CONCAT(a, b) LOOPER_CONCAT_IMPL(a, b)

// Подавление -Wimplicit-fallthrough в LOOPER_WAIT: case-метки — точки продолжения.
#if defined(__GNUC__) && (__GNUC__ >= 7)
#define LOOPER_FALLTHROUGH __attribute__((fallthrough))
#else
#define LOOPER_FALLTHROUGH ((void)0)
#endif

#endif
