#pragma once
#include "../../core/Features.h"
#if F_USE_MENU

#include <stdint.h>
#include <string.h>

#ifdef ARDUINO
#include <Arduino.h>
#endif

// ==================== PROGMEM ====================

#ifdef ARDUINO

#define MENU_PROGMEM PROGMEM
#define MENU_PGM_READ(ptr) ((char)pgm_read_byte(ptr))
#define MENU_PGM_FIND(str, c) fn::disp::menuutil::findChar_P(str, c)
#define MENU_PGM_LEN(str) strlen_P(str)

#define MENU_READ_PGM(fstr, str)                 \
    char str[strlen_P((const char*)fstr) + 1]; \
    strcpy_P(str, (const char*)fstr)

#else

#define MENU_PROGMEM
#define MENU_PGM_READ(ptr) (*(ptr))
#define MENU_PGM_FIND(str, c) strchr(str, c)
#define MENU_PGM_LEN(str) strlen(str)

#endif

namespace fn {
namespace disp {
namespace menuutil {

const char* findChar_P(const char* str, char c);

uint8_t countSub(const char* str, char div);

uint8_t getSub(const char* str, char div, uint8_t n, const char** start);

uint8_t strlenU(const char* str, uint8_t* letters, uint8_t maxBytes = UINT8_MAX, uint8_t maxLetters = UINT8_MAX);

void _dummyRender(const char*, size_t);

}  // namespace menuutil
}  // namespace disp
}  // namespace fn

#endif  // F_USE_MENU
