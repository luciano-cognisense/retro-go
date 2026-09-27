#ifndef GBSP_FETCH_PATH_H
#define GBSP_FETCH_PATH_H
// Reuse PC only within one fetch. Refresh after cheats and on the slow page path.
// Opcodes are still loaded every time; no instruction cache or cycle changes.
#if defined(RETRO_GO) && defined(CONFIG_GBSP_FETCH_PC) && CONFIG_GBSP_FETCH_PC && !defined(HAVE_DYNAREC)
#define GBSP_FETCH_PC_ACTIVE 1
#else
#define GBSP_FETCH_PC_ACTIVE 0
#endif
#endif
