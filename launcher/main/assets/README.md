# Boot sound

`boot_c3.pcm` is signed 16-bit little-endian PCM, mono, 22050 Hz
(49392 samples / 2.24 seconds). It is embedded directly into the launcher.

To soften the boot sound, the original asset from commit `366dba4c` was
processed offline with a second-order Butterworth low-pass filter at 1800 Hz
(Q = 1/sqrt(2)), followed by -6 dB gain and linear fades of 25 ms at the start
and 80 ms at the end. The PCM format and duration are unchanged. This only
changes the boot sound; emulator audio and the user's volume setting are
unaffected.

For further tuning, start from the original asset in Git rather than filtering
this already processed file again.
