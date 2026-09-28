#include "dsp_tables.h"
#include <math.h>

float SINE_TAB[SINE_SIZE + 1];
float TANH_TAB[TANH_SIZE + 1];
float FILT_G[FILT_TAB_SIZE];
float FILT_HZ[FILT_TAB_SIZE];
float NOTE_FREQ[128];
float INV_SQRT_N[MAX_VOICES + 2];

// Built once in setup(), before core1 is allowed to render.  Total cost is a
// few thousand libm calls — a couple of milliseconds at 250 MHz, paid at boot
// so nothing on the audio path ever calls sinf/expf/powf again.
void dspTablesInit() {
    for (int i = 0; i < SINE_SIZE; i++)
        SINE_TAB[i] = sinf(2.0f * (float)M_PI * (float)i / (float)SINE_SIZE);
    SINE_TAB[SINE_SIZE] = SINE_TAB[0];          // wrap guard for the interpolator

    for (int i = 0; i <= TANH_SIZE; i++) {
        float x = -TANH_RANGE + (2.0f * TANH_RANGE) * (float)i / (float)TANH_SIZE;
        TANH_TAB[i] = tanhf(x);
    }

    for (int i = 0; i < FILT_TAB_SIZE; i++) {
        float hz = FILT_HZ_MIN * powf(2.0f, FILT_OCTAVES * (float)i / (float)FILT_TAB_SIZE);
        FILT_HZ[i] = hz;
        // Bilinear-prewarped integrator gain for the TPT state-variable filter:
        // g = tan(pi * fc / fs).  The table runs past Nyquist at the top end,
        // which is fine — the SVF is unconditionally stable for any g, it just
        // becomes a bypass up there.
        float w = (float)M_PI * hz * INV_SAMPLE_RATE;
        if (w > 1.5533f) w = 1.5533f;         // ~0.4945 * fs, keeps tan() finite
        FILT_G[i] = tanf(w);
    }

    for (int n = 0; n < 128; n++)
        NOTE_FREQ[n] = 440.0f * powf(2.0f, (float)(n - 69) / 12.0f);

    INV_SQRT_N[0] = 1.0f;
    for (int n = 1; n <= MAX_VOICES + 1; n++)
        INV_SQRT_N[n] = 1.0f / sqrtf((float)n);
}
