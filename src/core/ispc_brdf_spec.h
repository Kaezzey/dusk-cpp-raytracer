#pragma once

// Batch compute specular contribution per shading sample.
// Inputs are pointer arrays of length `count` for each vector/component.
void ispc_compute_specular(const float* Nx, const float* Ny, const float* Nz,
                           const float* Vx, const float* Vy, const float* Vz,
                           const float* Lx, const float* Ly, const float* Lz,
                           const float* F0r, const float* F0g, const float* F0b,
                           const float* alpha, int count,
                           float* out_r, float* out_g, float* out_b);

// Batch compute full shaded response (normal-map transform + specular + diffuse)
void ispc_compute_shade(const float* Ngeomx, const float* Ngeomy, const float* Ngeomz,
                        const float* Tx, const float* Ty, const float* Tz,
                        const float* Bx, const float* By, const float* Bz,
                        const float* ntr, const float* ntg, const float* ntb,
                        const float* normal_strength,
                        const float* Vx, const float* Vy, const float* Vz,
                        const float* Lx, const float* Ly, const float* Lz,
                        const float* baser, const float* baseg, const float* baseb,
                        const float* metallic,
                        const float* dielr, const float* dielg, const float* dielb,
                        const float* alpha, int count,
                        float* out_r, float* out_g, float* out_b);
