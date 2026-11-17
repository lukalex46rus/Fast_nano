#pragma once
// Низкоуровневая сборка строк в готовый буфер.
// Без Arduino String, без динамической памяти: все функции пишут в char[].

#include <stdint.h>

namespace fn {
namespace strings {

uint16_t appendChar(char sym, char* buf, int16_t left = -1);
uint16_t appendRepeat(char sym, int16_t amount, char* buf, int16_t left = -1);
uint16_t appendProgmem(const void* pstr, int16_t len, char* buf, int16_t left = -1);
uint16_t appendProgmem(const void* pstr, char* buf, int16_t left = -1);
uint16_t append(const char* str, int16_t len, char* buf, int16_t left = -1);
uint16_t append(const char* str, char* buf, int16_t left = -1);
uint8_t appendUnsigned(uint32_t v, uint8_t base, char* buf, int16_t left = -1);
uint8_t appendSigned(int32_t v, uint8_t base, char* buf, int16_t left = -1);
uint8_t appendUnsigned64(uint64_t v, uint8_t base, char* buf, int16_t left = -1);
uint8_t appendSigned64(int64_t v, uint8_t base, char* buf, int16_t left = -1);
uint8_t appendFloat(float v, uint8_t dec, char* buf, int16_t left = -1);

}  // namespace strings
}  // namespace fn
