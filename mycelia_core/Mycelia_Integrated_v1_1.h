/*
 * Mycelia Integrated Interface v1.1
 * Emergent MycelFS linkage between CipherCore, SubQG, and address logic.
 */

#ifndef MYCELIA_INTEGRATED_V1_1_H
#define MYCELIA_INTEGRATED_V1_1_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Fehlercodes für MycelFS
typedef enum {
    M_OK = 0,
    M_ERR_UNKNOWN = -1,
    M_ERR_NO_GPU = -2,
    M_ERR_NOT_READY = -3,
    M_ERR_COLD_FIELD = -4,
    M_ERR_DESYNC = -5,
    M_ERR_OPENCL = -6
} mycelia_result;

// Map-Entry lebt ausschließlich im VRAM (Host darf ihn nicht dauerhaft halten).
typedef struct {
    uint64_t logical_id;
    uint64_t physical_pos;
    float    local_potential;
    uint32_t noise_epoch;
} MycelMapEntry;

// Initialisiert das gesamte MycelFS-Subsystem mit User-Seed.
int mycelia_init_all(uint64_t seed);

// Übersetzt Logical-ID zu physischer Position (fail-closed).
int mycelia_fs_map_logical_to_physical(uint64_t logical_id, uint64_t* out_physical_pos);

// Zyklus-Update: hält das Myzel am Leben (SubQG-Wachstum).
int mycelia_cycle_update(void);

// Liefert die aktuelle Noise-Epoche des Myzels.
uint32_t mycelia_get_noise_epoch(void);

#ifdef __cplusplus
}
#endif

#endif /* MYCELIA_INTEGRATED_V1_1_H */
