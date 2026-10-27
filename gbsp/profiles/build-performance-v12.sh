#!/bin/bash
# Build the gbsp app with the versioned performance profile v12 (profile v11
# plus measured lazy population of the 512 KB ROM-cache tail).
#
#   gbsp/profiles/build-performance-v12.sh
#
# Requires an exported ESP-IDF 5.3.1 environment (IDF_PATH or idf.py), the same
# precondition as ./rg_tool.py. Works from any directory: the repository root
# is derived from this script's own location. Does not modify rg_tool.py or the
# target's global sdkconfig, and does not copy any build output into the
# repository.
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
profile="gbsp/profiles/performance-v12.sdkconfig"
target="esp32-s3-devkit"
cd "$root"

die() { echo "build-performance-v12: $*" >&2; exit 1; }

[ -f rg_tool.py ] || die "rg_tool.py not found in $root"
[ -f "$profile" ] || die "$profile not found"
if [ -z "${IDF_PATH:-}" ] && ! command -v idf.py >/dev/null 2>&1; then
  die "ESP-IDF environment not exported (IDF_PATH unset and idf.py not on PATH)"
fi

required_cfg=(
  "CONFIG_GBSP_FAST_RAM_WRITES=y"
  "CONFIG_GBSP_CPSR_CONSUMER_SYNC=y"
  "CONFIG_GBSP_CPSR_LAZY_DISPATCH=y"
  "CONFIG_GBSP_FETCH_PC=y"
  "CONFIG_GBSP_NO_CHEAT_HOOK=y"
  "CONFIG_GBSP_CPU_RODATA_DRAM=y"
  "CONFIG_ESP32S3_DATA_CACHE_64KB=y"
  "CONFIG_ESP32S3_DATA_CACHE_SIZE=0x10000"
  "CONFIG_ESP32S3_DATA_CACHE_8WAYS=y"
  "CONFIG_ESP32S3_DATA_CACHE_LINE_SIZE=32"
  "CONFIG_SPIRAM_SPEED=80"
  "CONFIG_GBSP_RENDER_CORE1=y"
  "CONFIG_GBSP_RENDER_CORE1_HYBRID=y"
  "CONFIG_GBSP_RENDER_CORE1_ADAPTIVE=y"
  "CONFIG_RG_DISPLAY_ACTIVITY=y"
  "CONFIG_GBSP_AUDIO_CLOCK_COMP=y"
  "CONFIG_GBSP_CPU_ALERT_PER_INSTRUCTION=y"
  "CONFIG_GBSP_THUMB_RESIDENT_PC=y"
  "CONFIG_GBSP_ROM_SECOND_CHANCE=y"
  "CONFIG_GBSP_AUDIO_LIVE_RATE=y"
  "CONFIG_GBSP_WAITSTATE_COMMON_BASE=y"
  "CONFIG_GBSP_ROM_BOUNCE_READ=y"
  "CONFIG_GBSP_ROM_BOUNCE_KB=16"
  "CONFIG_GBSP_ROM_CACHE_512K_TAIL=y"
  "CONFIG_GBSP_ROM_CACHE_LAZY_TAIL=y"
  "CONFIG_RG_AUDIO_I2S_LIVE_RATE=y"
)
forbidden=(
  CONFIG_GBSP_MEMORY_PROFILE
  CONFIG_GBSP_COMPACT_READS
  CONFIG_GBSP_SPARSE_REGISTERS
  CONFIG_ESP32S3_DATA_CACHE_32KB
  CONFIG_GBSP_RENDER_CORE1_STATS
  CONFIG_GBSP_AUDIO_PACING_STATS
  CONFIG_RG_AUDIO_I2S_EVENTS
  CONFIG_GBSP_AUDIO_UNDERRUN_GUARD
  CONFIG_GBSP_AUDIO_TRANSITION_STATS
  CONFIG_RG_AUDIO_I2S_TELEMETRY
)
required_h=(
  "CONFIG_GBSP_FAST_RAM_WRITES 1"
  "CONFIG_GBSP_CPSR_CONSUMER_SYNC 1"
  "CONFIG_GBSP_CPSR_LAZY_DISPATCH 1"
  "CONFIG_GBSP_FETCH_PC 1"
  "CONFIG_GBSP_NO_CHEAT_HOOK 1"
  "CONFIG_GBSP_CPU_RODATA_DRAM 1"
  "CONFIG_ESP32S3_DATA_CACHE_64KB 1"
  "CONFIG_ESP32S3_DATA_CACHE_SIZE 0x10000"
  "CONFIG_ESP32S3_DATA_CACHE_8WAYS 1"
  "CONFIG_ESP32S3_DATA_CACHE_LINE_SIZE 32"
  "CONFIG_SPIRAM_SPEED 80"
  "CONFIG_GBSP_RENDER_CORE1 1"
  "CONFIG_GBSP_RENDER_CORE1_HYBRID 1"
  "CONFIG_GBSP_RENDER_CORE1_ADAPTIVE 1"
  "CONFIG_RG_DISPLAY_ACTIVITY 1"
  "CONFIG_GBSP_AUDIO_CLOCK_COMP 1"
  "CONFIG_GBSP_CPU_ALERT_PER_INSTRUCTION 1"
  "CONFIG_GBSP_THUMB_RESIDENT_PC 1"
  "CONFIG_GBSP_ROM_SECOND_CHANCE 1"
  "CONFIG_GBSP_AUDIO_LIVE_RATE 1"
  "CONFIG_GBSP_WAITSTATE_COMMON_BASE 1"
  "CONFIG_GBSP_ROM_BOUNCE_READ 1"
  "CONFIG_GBSP_ROM_BOUNCE_KB 16"
  "CONFIG_GBSP_ROM_CACHE_512K_TAIL 1"
  "CONFIG_GBSP_ROM_CACHE_LAZY_TAIL 1"
  "CONFIG_RG_AUDIO_I2S_LIVE_RATE 1"
)

check_cfg() {
  local f="$1" line name
  [ -f "$f" ] || die "$f missing"
  for line in "${required_cfg[@]}"; do
    grep -qxF "$line" "$f" || die "$f: missing '$line'"
  done
  for name in "${forbidden[@]}"; do
    if grep -qE "^${name}=" "$f"; then die "$f: '$name' must be disabled"; fi
  done
}

check_header() {
  local f="$1" line name
  [ -f "$f" ] || die "$f missing"
  for line in "${required_h[@]}"; do
    grep -qxF "#define $line" "$f" || die "$f: missing '#define $line'"
  done
  for name in "${forbidden[@]}"; do
    if grep -qE "^#define ${name} " "$f"; then die "$f: '$name' must be undefined"; fi
  done
}

check_cfg "$profile"
cp "$profile" gbsp/sdkconfig
check_cfg gbsp/sdkconfig

./rg_tool.py build gbsp --target="$target" --no-networking

check_cfg gbsp/sdkconfig
check_header gbsp/build/config/sdkconfig.h
[ -f gbsp/build/gbsp.bin ] || die "gbsp/build/gbsp.bin missing"
size=$(wc -c < gbsp/build/gbsp.bin | tr -d ' ')
[ "$size" -le $((0xd0000)) ] || die "gbsp.bin ($size bytes) exceeds the 0xd0000 gbsp partition"
echo "build-performance-v12: OK, gbsp/build/gbsp.bin $size bytes (partition 0xd0000)"
