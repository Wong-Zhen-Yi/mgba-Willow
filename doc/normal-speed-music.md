# Normal-speed Emerald music

During capped AI play above 1x, the verified English Emerald layouts use a
private GBA core for background music. Settings → Audio → **Normal-speed Emerald
music during AI play** enables this by default. Returning to 1x, disabling AI
control or disabling the option restores ordinary game audio. Mute and volume
settings still apply. Unsupported games and unbounded speed retain the existing
audio path.

The companion clones the loaded ROM and emulated state into memory; it never
opens a save file. Its main CPU idles, gameplay and graphics callbacks are
cleared, and the ROM's VBlank/VCount sound routines continue running. Only the
ten-track BGM player remains in the sound-driver chain. Other PCM and PSG voices
are stopped, and old mixed PCM is discarded. Sound effects, cries and fanfares
are suppressed. Temporary fanfare pauses do not interrupt background music;
genuine stops produce silence.

Song-header changes and song restarts reseed the companion. Checkpoint loads,
ordinary state loads and resets invalidate it. Each accelerated gameplay frame
contributes `1 / speed` of a companion frame, so the music keeps its original
tempo and pitch. The frontend resamples this separate queue at its native rate.
The main audio producer bypasses its accelerated PCM queue while this mode is
active; the companion queue supplies audio pacing. Its queue is bounded to
4096 stereo frames. The companion has a dummy renderer and no core callbacks,
scripts or link peripherals.

The ROM assumptions are documented in the Emerald decompilation's
[main loop and interrupt handlers](https://github.com/pret/pokeemerald/blob/master/src/main.c),
[music player table](https://github.com/pret/pokeemerald/blob/master/sound/music_player_table.inc)
and [sound-driver structures](https://github.com/pret/pokeemerald/blob/master/include/gba/m4a_internal.h).
Identity checks use the existing Emerald adapter's verified hash list.

With `BUILD_AI_TESTS=ON`, `test-qt-emeraldmusic` checks fallback behavior. Pass
`<ROM> <checkpoint> [output.pcm] [second-checkpoint]` for isolated integration
checks: identical native-rate music at 8x and 16x, an unchanged gameplay state,
song changes and accelerated gameplay with audio sync enabled. Inputs are read
only; gameplay advancement takes place solely in a temporary test core. PCM
output is signed 16-bit little-endian stereo at the ROM's emulated sample rate.
