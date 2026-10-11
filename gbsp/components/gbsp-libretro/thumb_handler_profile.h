#ifndef GBSP_THUMB_HANDLER_PROFILE_H
#define GBSP_THUMB_HANDLER_PROFILE_H

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#if defined(RETRO_GO) && defined(CONFIG_GBSP_THUMB_HANDLER_PROFILE) && CONFIG_GBSP_THUMB_HANDLER_PROFILE && !defined(HAVE_DYNAREC)
#define GBSP_THUMB_HANDLER_PROFILE_ACTIVE 1

typedef struct {
    uint32_t handler[256];
    uint32_t total;
} gbsp_thumb_handler_profile_t;

extern gbsp_thumb_handler_profile_t gbsp_thumb_handler_profile;

#define GBSP_THUMB_HANDLER_ISSUED(opcode) do { \
    ++gbsp_thumb_handler_profile.handler[((opcode) >> 8) & 0xffu]; \
    ++gbsp_thumb_handler_profile.total; \
} while (0)

static inline void gbsp_thumb_handler_profile_reset(void)
{
    memset(&gbsp_thumb_handler_profile, 0, sizeof(gbsp_thumb_handler_profile));
}

static inline void gbsp_thumb_handler_profile_write(FILE *fp, unsigned frames)
{
    fprintf(fp, "thumb_handler_profile_v1 frames=%u total=%u timing=INVALIDO\\n",
            frames, (unsigned)gbsp_thumb_handler_profile.total);
    for (unsigned k = 0; k < 256; ++k)
        if (gbsp_thumb_handler_profile.handler[k])
            fprintf(fp, "thumb_handler k=%02x count=%u\\n", k,
                    (unsigned)gbsp_thumb_handler_profile.handler[k]);
}
#else
#define GBSP_THUMB_HANDLER_PROFILE_ACTIVE 0
#define GBSP_THUMB_HANDLER_ISSUED(opcode) ((void)(opcode))
static inline void gbsp_thumb_handler_profile_reset(void) {}
static inline void gbsp_thumb_handler_profile_write(FILE *fp, unsigned frames)
{
    (void)fp;
    (void)frames;
}
#endif

#endif
