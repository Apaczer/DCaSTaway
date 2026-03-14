/* SDL_endian.h stub for libretro - little endian (MIPS32 EL) */
#ifndef SDL_ENDIAN_H
#define SDL_ENDIAN_H

#include <stdint.h>

#define SDL_LIL_ENDIAN 1234
#define SDL_BIG_ENDIAN 4321

/* SF2000 is little endian */
#define SDL_BYTEORDER SDL_LIL_ENDIAN

/* Byte swapping functions */
static inline uint16_t SDL_Swap16(uint16_t x) {
    return (x << 8) | (x >> 8);
}

static inline uint32_t SDL_Swap32(uint32_t x) {
    return ((x << 24) | ((x << 8) & 0x00FF0000) | ((x >> 8) & 0x0000FF00) | (x >> 24));
}

#if SDL_BYTEORDER == SDL_LIL_ENDIAN
#define SDL_SwapLE16(x) (x)
#define SDL_SwapLE32(x) (x)
#define SDL_SwapBE16(x) SDL_Swap16(x)
#define SDL_SwapBE32(x) SDL_Swap32(x)
#else
#define SDL_SwapLE16(x) SDL_Swap16(x)
#define SDL_SwapLE32(x) SDL_Swap32(x)
#define SDL_SwapBE16(x) (x)
#define SDL_SwapBE32(x) (x)
#endif

#endif /* SDL_ENDIAN_H */
