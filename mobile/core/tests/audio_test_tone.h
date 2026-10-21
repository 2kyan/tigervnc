/* Deterministic sine-wave test tone, S16LE stereo interleaved, matching
 * the fixed decoded-PCM format the TigerVNC-native audio extension
 * always uses (see common/rfb/audioTypes.h). Used as
 * the shared reference by both test_server.c (which Opus-encodes it
 * frame-by-frame before sending, exactly like the real
 * AudioCapturePulse) and smoke_test.cxx (which compares the *decoded*
 * PCM it receives back against this reference within a tolerance, not
 * byte-exact -- Opus is lossy, so exact reproduction was never the
 * right test once the wire format stopped being raw PCM).
 *
 * A sine wave, not the square wave this used to be: a square wave's
 * sharp edges/rich harmonic content make it a genuinely adversarial
 * signal for a perceptual codec like Opus to reproduce accurately, so
 * a square wave would need a needlessly loose tolerance to avoid false
 * failures -- masking real regressions. A sine wave is representative
 * of the smooth, band-limited content Opus is actually designed for
 * (voice/music), so a tight tolerance here is a meaningful test, not
 * just a workaround. No longer using plain integer arithmetic for
 * cross-language exactness either, since exactness stopped being the
 * point -- both sides run on the same machine in the same build, so
 * libm's sin() reproduces consistently within a run either way. */
#ifndef TIGERVNC_CORE_TESTS_AUDIO_TEST_TONE_H
#define TIGERVNC_CORE_TESTS_AUDIO_TEST_TONE_H

#include <math.h>
#include <stddef.h>
#include <stdint.h>

#define AUDIO_TEST_TONE_FRAMES 14400 /* 300ms at 48000 Hz -- 15 exact 20ms Opus frames */
#define AUDIO_TEST_TONE_CHANNELS 2
#define AUDIO_TEST_TONE_SAMPLE_RATE 48000
#define AUDIO_TEST_TONE_HZ 440.0 /* concert A -- same as the speaker-test tone used for manual checks */
#define AUDIO_TEST_TONE_AMPLITUDE 12000
#define AUDIO_TEST_TONE_BYTES \
  (AUDIO_TEST_TONE_FRAMES * AUDIO_TEST_TONE_CHANNELS * 2)

/* Fills `out` (must point at AUDIO_TEST_TONE_BYTES bytes) with the tone. */
static inline void audio_test_tone_fill(uint8_t* out)
{
  int frame, ch;
  for (frame = 0; frame < AUDIO_TEST_TONE_FRAMES; frame++) {
    double t = (double)frame / AUDIO_TEST_TONE_SAMPLE_RATE;
    int16_t sample = (int16_t)(AUDIO_TEST_TONE_AMPLITUDE *
                               sin(2.0 * M_PI * AUDIO_TEST_TONE_HZ * t));
    for (ch = 0; ch < AUDIO_TEST_TONE_CHANNELS; ch++) {
      size_t idx = (size_t)(frame * AUDIO_TEST_TONE_CHANNELS + ch) * 2;
      out[idx] = (uint8_t)(sample & 0xff);
      out[idx + 1] = (uint8_t)((sample >> 8) & 0xff);
    }
  }
}

#endif
