// Host mock of <pgmspace.h> — no flash, everything is regular RAM.
#pragma once
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#ifndef PROGMEM
#define PROGMEM
#endif
#ifndef PGM_P
#define PGM_P const char*
#endif
#define pgm_read_byte(addr)  (*(const uint8_t*)(addr))
#define pgm_read_word(addr)  (*(const uint16_t*)(addr))
#define pgm_read_dword(addr) (*(const uint32_t*)(addr))
#define pgm_read_ptr(addr)   (*(void* const*)(addr))
#define memcpy_P  memcpy
#define strcpy_P  strcpy
#define strncpy_P strncpy
#define strlen_P  strlen
#define snprintf_P snprintf
#define sprintf_P  sprintf
