#include "sim.h"

#include <SDL2/SDL.h>
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

_Static_assert(sizeof(int) * CHAR_BIT == 32, "sim requires 32-bit int");

static SDL_Window *window;
static SDL_Renderer *renderer;
static SDL_Texture *texture;
static uint32_t pixels[SIM_Y_SIZE][SIM_X_SIZE];
static uint32_t frame_tick;
static unsigned frame_number;
static int without_window;
static uint64_t draw_hash, draw_count;

static uint64_t hash_word(uint64_t hash, uint32_t word)
{
    for (unsigned shift = 0; shift < 32; shift += 8) {
        hash ^= (word >> shift) & 255u;
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

void simExit(void)
{
    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    texture = NULL;
    renderer = NULL;
    window = NULL;
    if (!without_window)
        SDL_Quit();
}

int simInit(int headless)
{
    without_window = headless;
    memset(pixels, 0, sizeof(pixels));
    frame_number = 0;
    draw_count = 0;
    draw_hash = UINT64_C(14695981039346656037);
    if (headless)
        return 1;

    if (SDL_Init(SDL_INIT_VIDEO) != 0)
        goto fail;
    window = SDL_CreateWindow("Turmite colony — LLVM interpreter",
                              SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                              SIM_X_SIZE, SIM_Y_SIZE, 0);
    if (!window)
        goto fail;
    renderer = SDL_CreateRenderer(window, -1, 0);
    if (!renderer)
        goto fail;
    texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
                                SDL_TEXTUREACCESS_STREAMING,
                                SIM_X_SIZE, SIM_Y_SIZE);
    if (!texture || SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_NONE) != 0)
        goto fail;
    frame_tick = SDL_GetTicks();
    return 1;

fail:
    fprintf(stderr, "SDL: %s\n", SDL_GetError());
    simExit();
    return 0;
}

void simPutPixel(int x, int y, int argb)
{
    if (x < 0 || x >= SIM_X_SIZE || y < 0 || y >= SIM_Y_SIZE) {
        fprintf(stderr, "simPutPixel: invalid coordinates (%d, %d)\n", x, y);
        simExit();
        exit(EXIT_FAILURE);
    }
    pixels[y][x] = (uint32_t)argb;
    if (without_window) {
        draw_hash = hash_word(draw_hash, (uint32_t)x);
        draw_hash = hash_word(draw_hash, (uint32_t)y);
        draw_hash = hash_word(draw_hash, (uint32_t)argb);
        ++draw_count;
    }
}

int simFlush(void)
{
    ++frame_number;
    if (without_window) {
        uint64_t hash = UINT64_C(14695981039346656037);
        for (int y = 0; y < SIM_Y_SIZE; ++y)
            for (int x = 0; x < SIM_X_SIZE; ++x)
                hash = hash_word(hash, pixels[y][x]);
        printf("frame=%u pixels=%016" PRIx64 " calls=%" PRIu64
               " trace=%016" PRIx64 "\n",
               frame_number, hash, draw_count, draw_hash);
        return 1;
    }

    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_QUIT ||
            (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE))
            return 0;
    }
    if (SDL_UpdateTexture(texture, NULL, pixels,
                          SIM_X_SIZE * (int)sizeof(uint32_t)) != 0 ||
        SDL_RenderClear(renderer) != 0 ||
        SDL_RenderCopy(renderer, texture, NULL, NULL) != 0) {
        fprintf(stderr, "SDL: %s\n", SDL_GetError());
        simExit();
        exit(EXIT_FAILURE);
    }
    SDL_RenderPresent(renderer);
    uint32_t elapsed = SDL_GetTicks() - frame_tick;
    if (elapsed < 33)
        SDL_Delay(33 - elapsed);
    frame_tick = SDL_GetTicks();
    return 1;
}
