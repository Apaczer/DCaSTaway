/* SDL_image.h stub for libretro */
#ifndef SDL_IMAGE_H
#define SDL_IMAGE_H

#include "sdl_compat.h"

/* Image loading - stubs for libretro (not needed) */
static inline SDL_Surface *IMG_Load(const char *file) { (void)file; return NULL; }
static inline SDL_Surface *IMG_Load_RW(void *src, int freesrc) { (void)src; (void)freesrc; return NULL; }

#endif /* SDL_IMAGE_H */
