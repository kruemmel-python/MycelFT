// Noise control implementation for CipherCore. Adjusts and reports a global
// noise scaling factor based on observed variance to keep downstream
// computations numerically stable.
#include "CipherCore_NoiseCtrl.h"

#include <math.h>

#ifndef THRESH_HIGH
#define THRESH_HIGH 1.5f
#endif

#ifndef THRESH_LOW
#define THRESH_LOW 0.5f
#endif

float g_noise_factor = 1.0f;

// Adapt the noise factor whenever the measured variance crosses the
// configured thresholds. This keeps the factor within a healthy operating
// window and avoids runaway amplification or suppression.
void update_noise(float variance) {
    if (variance > THRESH_HIGH) {
        g_noise_factor *= 0.9f;
    } else if (variance < THRESH_LOW) {
        g_noise_factor *= 1.1f;
    }
    if (g_noise_factor < 0.1f) {
        g_noise_factor = 0.1f;
    } else if (g_noise_factor > 2.0f) {
        g_noise_factor = 2.0f;
    }
}

// Explicitly set the noise factor while clamping it to the supported range,
// so callers cannot accidentally drive the control loop into invalid states.
void set_noise_factor(float value) {
    if (value < 0.1f) {
        value = 0.1f;
    } else if (value > 2.0f) {
        value = 2.0f;
    }
    g_noise_factor = value;
}

// Expose the current noise factor so other modules can scale their signals
// consistently with the control loop's internal state.
float get_noise_factor(void) {
    return g_noise_factor;
}

// Reset the noise factor to the neutral baseline used during initialisation.
void reset_noise_factor(void) {
    g_noise_factor = 1.0f;
}

// Convert a variance reading into an error metric that reflects the absolute
// deviation from the nominal value. The result is scaled to moderate the
// influence of extreme outliers.
static float compute_error_from_variance(float variance) {
    float deviation = variance - 1.0f;
    return fabsf(deviation) * 0.5f;
}

// Public measurement entry point: update the control loop with the latest
// variance, optionally report the raw variance, and output the derived error
// metric for diagnostics or logging.
void noisectrl_measure(float variance, float* error_out, float* variance_out) {
    update_noise(variance);
    if (variance_out) {
        *variance_out = variance;
    }
    if (error_out) {
        *error_out = compute_error_from_variance(variance);
    }
}
