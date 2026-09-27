# Accepted interpreter baseline — 2026-09-27

Accepted after the console comparisons recorded under HandsOnLab/outputs:
- Mapped RAM stores, subtraction overflow simplification and thumb_test_sub operand capture.
- Consumer CPSR synchronization with lazy dispatch (~11% less emulation time in the C→A2 comparison; A1 was unavailable and the user accepted A2).
- FETCH_PC reuse within a fetch plus local cheat hook snapshot (combined ~2.7% Mario / ~2.9% Fire Red less emulation time in the latest comparison).

Configuration for this baseline: GBSP_FAST_RAM_WRITES, GBSP_CPSR_CONSUMER_SYNC,
GBSP_CPSR_LAZY_DISPATCH and GBSP_FETCH_PC enabled; GBSP_SPARSE_REGISTERS,
GBSP_COMPACT_READS and GBSP_MEMORY_PROFILE disabled. Cache 32KB, PSRAM 80MHz.
Experimental Kconfig defaults remain disabled; the app sdkconfig selects them.
A clean deletes the app sdkconfig: restore the archived configuration explicitly.

Exact tested binary/configuration: outputs/gbsp-fetch-cheat/fetch-cheat.bin and
fetch-cheat-sdkconfig (outside the repository). This consolidation does not change
the audio controller, shared retro-go components or launcher. Speed measurements
cover two scenes, not universal emulator performance or exhaustive correctness.
