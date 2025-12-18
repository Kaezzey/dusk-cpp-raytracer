#include "ispc_brdf_spec.h"
#include <cmath>

#if defined(HAVE_ISPC)
extern "C" void compute_specular(float* Nx, float* Ny, float* Nz,
                                 float* Vx, float* Vy, float* Vz,
                                 float* Lx, float* Ly, float* Lz,
                                 float* F0r, float* F0g, float* F0b,
                                 float* alpha, int count,
                                 float* out_r, float* out_g, float* out_b);
#endif

void ispc_compute_specular(const float* Nx, const float* Ny, const float* Nz,
                           const float* Vx, const float* Vy, const float* Vz,
                           const float* Lx, const float* Ly, const float* Lz,
                           const float* F0r, const float* F0g, const float* F0b,
                           const float* alpha, int count,
                           float* out_r, float* out_g, float* out_b)
{
#if defined(HAVE_ISPC)
    // ISPC expects non-const pointers for exported functions
    compute_specular((float*)Nx, (float*)Ny, (float*)Nz,
                     (float*)Vx, (float*)Vy, (float*)Vz,
                     (float*)Lx, (float*)Ly, (float*)Lz,
                     (float*)F0r, (float*)F0g, (float*)F0b,
                     (float*)alpha, count,
                     out_r, out_g, out_b);
#else
    // CPU fallback: scalar implementation
    for (int i = 0; i < count; ++i) {
        float Nx_v = Nx[i]; float Ny_v = Ny[i]; float Nz_v = Nz[i];
        float Vx_v = Vx[i]; float Vy_v = Vy[i]; float Vz_v = Vz[i];
        float Lx_v = Lx[i]; float Ly_v = Ly[i]; float Lz_v = Lz[i];

        float NdotL = fmax(0.0f, Nx_v*Lx_v + Ny_v*Ly_v + Nz_v*Lz_v);
        float NdotV = fmax(0.0001f, Nx_v*Vx_v + Ny_v*Vy_v + Nz_v*Vz_v);

        float Hx = Vx_v + Lx_v;
        float Hy = Vy_v + Ly_v;
        float Hz = Vz_v + Lz_v;
        float Hlen = std::sqrt(Hx*Hx + Hy*Hy + Hz*Hz) + 1e-12f;
        Hx /= Hlen; Hy /= Hlen; Hz /= Hlen;

        float NdotH = fmax(0.0001f, Nx_v*Hx + Ny_v*Hy + Nz_v*Hz);
        float VdotH = fmax(0.0001f, Vx_v*Hx + Vy_v*Hy + Vz_v*Hz);

        float a = alpha[i];
        float a2 = a*a;

        float denom = (NdotH*NdotH)*(a2 - 1.0f) + 1.0f;
        float D = (a2) / (3.14159265f * denom * denom + 1e-12f);

        float k = (a2) * 0.5f;
        auto geomfunc = [&](float NdotX){ return NdotX / (NdotX * (1.0f - k) + k); };
        float G = geomfunc(NdotV) * geomfunc(NdotL);

        float F0r_v = F0r[i]; float F0g_v = F0g[i]; float F0b_v = F0b[i];
        float x = 1.0f - VdotH;
        float x5 = x*x; x5 = x5*x5*x;
        float Fr = F0r_v + (1.0f - F0r_v) * x5;
        float Fg = F0g_v + (1.0f - F0g_v) * x5;
        float Fb = F0b_v + (1.0f - F0b_v) * x5;

        float denom2 = fmax(1e-6f, 4.0f * NdotV * NdotL);
        float spec_factor = (D * G) / denom2;

        out_r[i] = spec_factor * Fr * NdotL;
        out_g[i] = spec_factor * Fg * NdotL;
        out_b[i] = spec_factor * Fb * NdotL;
    }
#endif
}

#if defined(HAVE_ISPC)
extern "C" void compute_shade(float* Ngeomx, float* Ngeomy, float* Ngeomz,
                               float* Tx, float* Ty, float* Tz,
                               float* Bx, float* By, float* Bz,
                               float* ntr, float* ntg, float* ntb,
                               float* normal_strength,
                               float* Vx, float* Vy, float* Vz,
                               float* Lx, float* Ly, float* Lz,
                               float* baser, float* baseg, float* baseb,
                               float* metallic,
                               float* dielr, float* dielg, float* dielb,
                               float* alpha, int count,
                               float* out_r, float* out_g, float* out_b);
#endif

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
                        float* out_r, float* out_g, float* out_b)
{
#if defined(HAVE_ISPC)
    compute_shade((float*)Ngeomx, (float*)Ngeomy, (float*)Ngeomz,
                  (float*)Tx, (float*)Ty, (float*)Tz,
                  (float*)Bx, (float*)By, (float*)Bz,
                  (float*)ntr, (float*)ntg, (float*)ntb,
                  (float*)normal_strength,
                  (float*)Vx, (float*)Vy, (float*)Vz,
                  (float*)Lx, (float*)Ly, (float*)Lz,
                  (float*)baser, (float*)baseg, (float*)baseb,
                  (float*)metallic,
                  (float*)dielr, (float*)dielg, (float*)dielb,
                  (float*)alpha, count,
                  out_r, out_g, out_b);
#else
    // CPU fallback: scalar loop mirroring the ISPC kernel
    for (int i = 0; i < count; ++i) {
        float Nx_g = Ngeomx[i]; float Ny_g = Ngeomy[i]; float Nz_g = Ngeomz[i];
        float Tx_v = Tx[i]; float Ty_v = Ty[i]; float Tz_v = Tz[i];
        float Bx_v = Bx[i]; float By_v = By[i]; float Bz_v = Bz[i];
        float ntr_v = ntr[i]; float ntg_v = ntg[i]; float ntb_v = ntb[i];
        float nstr = normal_strength[i];

        float nx_raw = 2.0f * ntr_v - 1.0f;
        float ny_raw = 2.0f * ntg_v - 1.0f;
        float nz_raw = 2.0f * ntb_v - 1.0f;
        float nx_t = nx_raw * nstr;
        float ny_t = ny_raw * nstr;
        float nz_t = nz_raw;
        float nlen = std::sqrt(nx_t*nx_t + ny_t*ny_t + nz_t*nz_t) + 1e-12f;
        nx_t /= nlen; ny_t /= nlen; nz_t /= nlen;

        float Nx = nx_t * Tx_v + ny_t * Bx_v + nz_t * Nx_g;
        float Ny = nx_t * Ty_v + ny_t * By_v + nz_t * Ny_g;
        float Nz = nx_t * Tz_v + ny_t * Bz_v + nz_t * Nz_g;
        float Vx_v = Vx[i]; float Vy_v = Vy[i]; float Vz_v = Vz[i];
        float NdotV_geo = std::max(0.0f, Nx_g*Vx_v + Ny_g*Vy_v + Nz_g*Vz_v);
        float strength = std::clamp(NdotV_geo * 5.0f, 0.0f, 1.0f);
        Nx = Nx * strength + Nx_g * (1.0f - strength);
        Ny = Ny * strength + Ny_g * (1.0f - strength);
        Nz = Nz * strength + Nz_g * (1.0f - strength);
        float nlen2 = std::sqrt(Nx*Nx + Ny*Ny + Nz*Nz) + 1e-12f;
        Nx /= nlen2; Ny /= nlen2; Nz /= nlen2;

        float Lx_v = Lx[i]; float Ly_v = Ly[i]; float Lz_v = Lz[i];
        float NdotL = std::max(0.0f, Nx*Lx_v + Ny*Ly_v + Nz*Lz_v);
        float NdotV = std::max(0.0001f, Nx*Vx_v + Ny*Vy_v + Nz*Vz_v);

        float Hx = Vx_v + Lx_v; float Hy = Vy_v + Ly_v; float Hz = Vz_v + Lz_v;
        float Hlen = std::sqrt(Hx*Hx + Hy*Hy + Hz*Hz) + 1e-12f;
        Hx /= Hlen; Hy /= Hlen; Hz /= Hlen;
        float NdotH = std::max(0.0001f, Nx*Hx + Ny*Hy + Nz*Hz);
        float VdotH = std::max(0.0001f, Vx_v*Hx + Vy_v*Hy + Vz_v*Hz);

        float a = alpha[i]; float a2 = a*a;
        float denom = (NdotH*NdotH)*(a2 - 1.0f) + 1.0f;
        float D = (a2) / (3.14159265f * denom * denom + 1e-12f);
        float k = (a2) * 0.5f;
        float geomV = NdotV / (NdotV * (1.0f - k) + k);
        float geomL = NdotL / (NdotL * (1.0f - k) + k);
        float G = geomV * geomL;

        float dielr_v = dielr[i]; float dielg_v = dielg[i]; float dielb_v = dielb[i];
        float baser_v = baser[i]; float baseg_v = baseg[i]; float baseb_v = baseb[i];
        float metall = metallic[i];
        float F0r_v = dielr_v * (1.0f - metall) + baser_v * metall;
        float F0g_v = dielg_v * (1.0f - metall) + baseg_v * metall;
        float F0b_v = dielb_v * (1.0f - metall) + baseb_v * metall;
        float x = 1.0f - VdotH; float x5 = x*x; x5 = x5*x5*x;
        float Fr = F0r_v + (1.0f - F0r_v) * x5;
        float Fg = F0g_v + (1.0f - F0g_v) * x5;
        float Fb = F0b_v + (1.0f - F0b_v) * x5;

        float denom2 = std::max(1e-6f, 4.0f * NdotV * NdotL);
        float spec_factor = (D * G) / denom2;
        float specr = spec_factor * Fr * NdotL;
        float specg = spec_factor * Fg * NdotL;
        float specb = spec_factor * Fb * NdotL;

        float Favg = (F0r_v + F0g_v + F0b_v) * (1.0f/3.0f);
        float kdiff = (1.0f - metall) * (1.0f - Favg);
        float dr = baser_v * kdiff * (1.0f / 3.14159265f);
        float dg = baseg_v * kdiff * (1.0f / 3.14159265f);
        float db = baseb_v * kdiff * (1.0f / 3.14159265f);

        out_r[i] = specr + dr * NdotL;
        out_g[i] = specg + dg * NdotL;
        out_b[i] = specb + db * NdotL;
    }
#endif
}
