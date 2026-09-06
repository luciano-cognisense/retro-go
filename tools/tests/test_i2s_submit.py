"""Host regression test for sample loss at I2S submission boundaries.

Run with python3 tools/tests/test_i2s_submit.py (requires a C compiler).
The real driver_submit body is tested with an in-memory i2s_write stub.
"""

from pathlib import Path
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[2]
source = (ROOT / "components/retro-go/drivers/audio/i2s.c").read_text()
body = source.split("static bool driver_submit(", 1)[1]
body = "static bool driver_submit(" + body.split("static bool driver_set_mute", 1)[0]

harness = r"""
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#define DMA_BUFFER_LEN 180
#define RG_COUNT(a) (sizeof(a) / sizeof((a)[0]))
#define RG_AUDIO_USE_INT_DAC 0
#define I2S_NUM_0 0
#define ESP_OK 0
#define RG_LOGW(...) ((void)0)
typedef struct { int16_t left, right; } rg_audio_frame_t;
static struct { bool muted; int volume, device; } state;
static rg_audio_frame_t received[1024];
static size_t received_count;
static int i2s_write(int port, void *data, size_t bytes, size_t *written, int timeout)
{
    (void)port; (void)timeout;
    assert(bytes > 0 && bytes <= DMA_BUFFER_LEN * sizeof(rg_audio_frame_t));
    assert(bytes % sizeof(rg_audio_frame_t) == 0);
    assert(received_count + bytes / sizeof(rg_audio_frame_t) <= RG_COUNT(received));
    memcpy(received + received_count, data, bytes);
    received_count += bytes / sizeof(rg_audio_frame_t);
    *written = bytes;
    return ESP_OK;
}
""" + body + r"""
int main(void)
{
    rg_audio_frame_t frames[1024];
    for (size_t i = 0; i < RG_COUNT(frames); ++i)
        frames[i] = (rg_audio_frame_t){(int16_t)(i * 12), (int16_t)(-((int)i) * 8)};
    state.device = 1;
    for (int mode = 0; mode < 3; ++mode) {
        state.volume = mode == 1 ? 50 : 100;
        state.muted = mode == 2;
        for (size_t count = 0; count <= RG_COUNT(frames); ++count) {
            received_count = 0;
            assert(driver_submit(frames, count));
            assert(received_count == count);
            for (size_t i = 0; i < count; ++i) {
                int divisor = mode == 1 ? 2 : 1;
                assert(received[i].left == (state.muted ? 0 : frames[i].left / divisor));
                assert(received[i].right == (state.muted ? 0 : frames[i].right / divisor));
            }
        }
    }
    return 0;
}
"""

with tempfile.TemporaryDirectory(prefix="retrogo-i2s-") as tmp:
    c_file = Path(tmp) / "test.c"
    binary = Path(tmp) / "test"
    c_file.write_text(harness)
    subprocess.run(["cc", "-std=c99", "-Wall", "-Wextra", "-Werror",
                    str(c_file), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
print("PASS: 0–1024 frames, stereo order, full/half volume and mute")
