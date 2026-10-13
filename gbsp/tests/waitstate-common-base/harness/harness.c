/* Harness diferencial A1 (CONFIG_GBSP_CPU_ALERT_PER_INSTRUCTION), build host.
 *
 * Roda o execute_arm() REAL dos fontes da branch (RETRO_GO, opcoes do perfil v3) e compara variantes:
 *   - apos cada instrucao executada (gancho de teste harness_trace na copia do cpu.cpp): PC, modo, ciclos
 *     restantes, flags locais n/z/c/v e os 16 registradores visiveis -- acumulados num hash e contados;
 *   - a cada alerta tratado (gancho harness_alert): bits HALT|IRQ e PC;
 *   - a cada quadro: hash do estado inteiro (registradores, SPSR, bancos, IWRAM, EWRAM, VRAM, OAM, paleta, IO,
 *     backup, contadores) e da CPU, mais o hash do rastreio e as contagens.
 * Opcoes: --idle 0xENDERECO (idle_loop_target_pc), --save-load N (salva e recarrega o estado no quadro N),
 *         --reset N (reset_gba no quadro N),
 *         --mark N (zera hashes/contagens no inicio do quadro N: referencia),
 *         --rewind N M (salva no inicio do quadro N, carrega esse estado no inicio do quadro M e zera hashes/contagens;
 *                       os quadros seguintes sao rotulados N, N+1, ...: comparaveis com a referencia --mark N).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "common.h"
#include "cpsr_sync.h"
#include "ram_store.h"

gbsp_memory_t *gbsp_memory;
u16 *gba_screen_pixels_buf;
u32 idle_loop_target_pc = 0xFFFFFFFF;
u32 translation_gate_target_pc[MAX_TRANSLATION_GATES];
u32 translation_gate_targets = 0;
boot_mode selected_boot_mode = boot_game;
u32 skip_next_frame = 0;
int sprite_limit = 1;
void netpacket_poll_receive(void) {}
void netpacket_send(uint16_t client_id, const void *buf, size_t len) { (void)client_id; (void)buf; (void)len; }
void set_fastforward_override(bool fastforward) { (void)fastforward; }
int64_t esp_timer_get_time(void) { return 0; }
static int16_t no_input(unsigned port, unsigned dev, unsigned idx, unsigned id) { (void)port;(void)dev;(void)idx;(void)id; return 0; }

static void fnv(uint64_t *h, const void *p, size_t n)
{
  const unsigned char *b = (const unsigned char *)p;
  for (size_t i = 0; i < n; i++) { *h ^= b[i]; *h *= 1099511628211ULL; }
}

/* --- ganchos chamados pela copia de teste do cpu.cpp --- */
static uint64_t trace_h = 14695981039346656037ULL, alert_h = 14695981039346656037ULL;
static uint64_t n_instr = 0, n_alert = 0, n_alert_halt = 0, n_alert_irq = 0;
static uint64_t frame_instr = 0;
static long d0 = -2, d1 = 0;   /* janela [d0, d1) de instrucoes despejadas (HARNESS_DUMP, HARNESS_DUMP_N) */
static int in_window(void)
{
  if (d0 == -2) { const char *a = getenv("HARNESS_DUMP"); d0 = -1; if (a) { d0 = strtol(a, NULL, 10); d1 = d0 + (getenv("HARNESS_DUMP_N") ? strtol(getenv("HARNESS_DUMP_N"), NULL, 10) : 60); } }
  return d0 >= 0 && (long)n_instr >= d0 && (long)n_instr < d1;
}
void harness_trace(u32 pc, u32 mode, s32 cycles, u32 n, u32 z, u32 c, u32 v)
{
  u32 rec[7] = { pc, mode, (u32)cycles, n, z, c, v };
  fnv(&trace_h, rec, sizeof(rec));
  fnv(&trace_h, reg, 16 * sizeof(u32));
  n_instr++;
  /* watchdog: um quadro tem ~2e4 (alerta) a ~4e4 (estresse) instrucoes; acima de 2e6 o quadro nao fecha (livelock) */
  if (++frame_instr > 2000000) { printf("LIVELOCK instr=%llu pc=%08x\n", (unsigned long long)n_instr, pc); fflush(stdout); exit(7); }
  if (in_window())
    fprintf(stderr, "#%llu pc=%08x %s r0=%08x r1=%08x sp=%08x lr=%08x cpsr=%08x\n", (unsigned long long)n_instr, pc, mode ? "ARM" : "THUMB", reg[0], reg[1], reg[13], reg[14], reg[REG_CPSR]);
}
void harness_alert(u32 bits, u32 pc)
{
  u32 rec[2] = { bits, pc };
  fnv(&alert_h, rec, sizeof(rec));
  static int log = -1; if (log < 0) log = getenv("HARNESS_ALERTLOG") != NULL;
  if (log && (d0 < 0 || in_window())) fprintf(stderr, "ALERT bits=%u pc=%08x instr=%llu\n", bits, pc, (unsigned long long)n_instr);
  n_alert++; if (bits & CPU_ALERT_HALT) n_alert_halt++; if (bits & CPU_ALERT_IRQ) n_alert_irq++;
}

static uint64_t state_hash(void)
{
  uint64_t h = 14695981039346656037ULL;
  fnv(&h, reg, sizeof(u32) * REG_ARCH_COUNT); fnv(&h, spsr, sizeof(spsr)); fnv(&h, reg_mode, sizeof(reg_mode));
  fnv(&h, iwram, IWRAM_BYTES); fnv(&h, ewram, sizeof(ewram)); fnv(&h, vram, sizeof(vram));
  fnv(&h, oam_ram, sizeof(oam_ram)); fnv(&h, palette_ram, sizeof(palette_ram)); fnv(&h, io_registers, sizeof(io_registers));
  fnv(&h, gamepak_backup, sizeof(gamepak_backup)); fnv(&h, &cpu_ticks, sizeof(cpu_ticks));
  fnv(&h, &execute_cycles, sizeof(execute_cycles)); fnv(&h, &frame_counter, sizeof(frame_counter));
  return h;
}

int main(int argc, char **argv)
{
  if (argc < 3) { fprintf(stderr, "uso: %s <rom> <quadros> [--idle 0x..] [--save-load N] [--reset N]\n", argv[0]); return 2; }
  const char *rom = argv[1]; long frames = strtol(argv[2], NULL, 10);
  long save_at = -1, reset_at = -1, mark_at = -1, rw_save = -1, rw_load = -1; u32 idle = 0xFFFFFFFF;
  for (int i = 3; i + 1 < argc; i += 2) {
    if (!strcmp(argv[i], "--idle")) idle = (u32)strtoul(argv[i + 1], NULL, 0);
    else if (!strcmp(argv[i], "--save-load")) save_at = strtol(argv[i + 1], NULL, 10);
    else if (!strcmp(argv[i], "--reset")) reset_at = strtol(argv[i + 1], NULL, 10);
    else if (!strcmp(argv[i], "--mark")) mark_at = strtol(argv[i + 1], NULL, 10);
    else if (!strcmp(argv[i], "--rewind") && i + 2 < argc) { rw_save = strtol(argv[i + 1], NULL, 10); rw_load = strtol(argv[i + 2], NULL, 10); i++; }
  }
  iwram_ptr = (u8 *)calloc(1, IWRAM_BYTES);
  gbsp_memory = (gbsp_memory_t *)calloc(1, sizeof(*gbsp_memory));
  gba_screen_pixels_buf = (u16 *)calloc(1, 240 * 160 * sizeof(u16));
  gba_screen_pixels = gba_screen_pixels_buf;
  if (!iwram_ptr || !gbsp_memory || !gba_screen_pixels_buf) { fprintf(stderr, "sem memoria\n"); return 3; }
  retro_set_input_state(no_input);
  init_gamepak_buffer(); init_sound();
  memset(gamepak_backup, 0xff, sizeof(gamepak_backup));
  if (load_gamepak(NULL, rom, 0, 0, 0) != 0) { fprintf(stderr, "load_gamepak falhou: %s\n", rom); return 4; }
  reset_gba();
  idle_loop_target_pc = idle;   /* depois de load_gamepak(), que o reinicia pela tabela de jogos */
  printf("# rom=%s quadros=%ld idle=0x%08x save_load=%ld reset=%ld\n", rom, frames, idle_loop_target_pc, save_at, reset_at);
  void *state = malloc(GBA_STATE_MEM_SIZE), *rw_state = malloc(GBA_STATE_MEM_SIZE);
  long label_off = 0;
  u32 start_frame = frame_counter;
  for (long f = 0; f < frames; f++) {
    if (f == save_at) { gba_save_state(state); if (!gba_load_state(state)) { fprintf(stderr, "load falhou\n"); return 6; } }
    if (f == reset_at) { reset_gba(); idle_loop_target_pc = idle; }
    if (f == rw_save) gba_save_state(rw_state);
    if (f == rw_load) {
      if (!gba_load_state(rw_state)) { fprintf(stderr, "rewind: load falhou\n"); return 6; }
      label_off = rw_save - rw_load;
    }
    if (f == mark_at || f == rw_load) {
      trace_h = alert_h = 14695981039346656037ULL; n_instr = n_alert = n_alert_halt = n_alert_irq = 0;
      printf("# acumuladores zerados\n");
    }
    u32 before = frame_counter; int guard = 0; frame_instr = 0;
    while (frame_counter == before && guard++ < 100000) execute_arm(execute_cycles);
    if (guard >= 100000) { fprintf(stderr, "quadro nao completou\n"); return 5; }
    printf("%ld %016llx %016llx %016llx instr=%llu alertas=%llu halt=%llu irq=%llu\n", f + label_off,
           (unsigned long long)state_hash(), (unsigned long long)trace_h, (unsigned long long)alert_h,
           (unsigned long long)n_instr, (unsigned long long)n_alert, (unsigned long long)n_alert_halt, (unsigned long long)n_alert_irq);
  }
  printf("FINAL quadros=%lu instr=%llu alertas=%llu halt=%llu irq=%llu\n", (unsigned long)(frame_counter - start_frame),
         (unsigned long long)n_instr, (unsigned long long)n_alert, (unsigned long long)n_alert_halt, (unsigned long long)n_alert_irq);
  free(state);
  return 0;
}
