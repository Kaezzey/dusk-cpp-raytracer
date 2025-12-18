export void normalize_batch(uniform float xyz_inout[], uniform int count) {
    // Simple uniform loop per-element: compatible with a wide range of ISPC versions.
    // Array syntax ensures the pointer itself is uniform (required for exported functions).
    for (uniform int i = 0; i < count; ++i) {
        uniform int base = 3 * i;
        uniform float x = xyz_inout[base + 0];
        uniform float y = xyz_inout[base + 1];
        uniform float z = xyz_inout[base + 2];
        uniform float len = sqrt(x*x + y*y + z*z);
        if (len > 1e-12f) {
            uniform float inv = 1.0f / len;
            xyz_inout[base + 0] = x * inv;
            xyz_inout[base + 1] = y * inv;
            xyz_inout[base + 2] = z * inv;
        }
    }
}

export void compute_specular(uniform float Nx[], uniform float Ny[], uniform float Nz[],
                             uniform float Vx[], uniform float Vy[], uniform float Vz[],
                             uniform float Lx[], uniform float Ly[], uniform float Lz[],
                             uniform float F0r[], uniform float F0g[], uniform float F0b[],
                             uniform float alpha[], uniform int count,
                             uniform float out_r[], uniform float out_g[], uniform float out_b[])
{
    // Use uniform loop for export compatibility
    for (uniform int i = 0; i < count; ++i) {
        uniform float Nx_v = Nx[i]; uniform float Ny_v = Ny[i]; uniform float Nz_v = Nz[i];
        uniform float Vx_v = Vx[i]; uniform float Vy_v = Vy[i]; uniform float Vz_v = Vz[i];
        uniform float Lx_v = Lx[i]; uniform float Ly_v = Ly[i]; uniform float Lz_v = Lz[i];

        // Normals and vectors assumed normalized
        uniform float NdotL = max(0.0f, Nx_v*Lx_v + Ny_v*Ly_v + Nz_v*Lz_v);
        uniform float NdotV = max(0.0001f, Nx_v*Vx_v + Ny_v*Vy_v + Nz_v*Vz_v);

        // Half vector
        uniform float Hx = Vx_v + Lx_v;
        uniform float Hy = Vy_v + Ly_v;
        uniform float Hz = Vz_v + Lz_v;
        uniform float Hlen = sqrt(Hx*Hx + Hy*Hy + Hz*Hz) + 1e-12f;
        Hx /= Hlen; Hy /= Hlen; Hz /= Hlen;

        uniform float NdotH = max(0.0001f, Nx_v*Hx + Ny_v*Hy + Nz_v*Hz);
        uniform float VdotH = max(0.0001f, Vx_v*Hx + Vy_v*Hy + Vz_v*Hz);

        uniform float a = alpha[i];
        uniform float a2 = a*a;

        // GGX D
        uniform float denom = (NdotH*NdotH)*(a2 - 1.0f) + 1.0f;
        uniform float D = (a2) / (3.14159265f * denom * denom + 1e-12f);

        // Geometry (Schlick-GGX)
        uniform float k = (a2) * 0.5f;
        uniform float geomV = NdotV / (NdotV * (1.0f - k) + k);
        uniform float geomL = NdotL / (NdotL * (1.0f - k) + k);
        uniform float G = geomV * geomL;

        // Fresnel (Schlick) per-channel
        uniform float F0r_v = F0r[i]; uniform float F0g_v = F0g[i]; uniform float F0b_v = F0b[i];
        uniform float x = 1.0f - VdotH;
        uniform float x5 = x*x; x5 = x5*x5*x;
        uniform float Fr = F0r_v + (1.0f - F0r_v) * x5;
        uniform float Fg = F0g_v + (1.0f - F0g_v) * x5;
        uniform float Fb = F0b_v + (1.0f - F0b_v) * x5;

        // specular term = (D * G) / (4 * NdotV * NdotL) * F
        uniform float denom2 = max(1e-6f, 4.0f * NdotV * NdotL);
        uniform float spec_factor = (D * G) / denom2;

        out_r[i] = spec_factor * Fr * NdotL;
        out_g[i] = spec_factor * Fg * NdotL;
        out_b[i] = spec_factor * Fb * NdotL;
    }
}

export void compute_shade(uniform float Ngeomx[], uniform float Ngeomy[], uniform float Ngeomz[],
                          uniform float Tx[], uniform float Ty[], uniform float Tz[],
                          uniform float Bx[], uniform float By[], uniform float Bz[],
                          uniform float ntr[], uniform float ntg[], uniform float ntb[],
                          uniform float normal_strength[],
                          uniform float Vx[], uniform float Vy[], uniform float Vz[],
                          uniform float Lx[], uniform float Ly[], uniform float Lz[],
                          uniform float baser[], uniform float baseg[], uniform float baseb[],
                          uniform float metallic[],
                          uniform float dielr[], uniform float dielg[], uniform float dielb[],
                          uniform float alpha[], uniform int count,
                          uniform float out_r[], uniform float out_g[], uniform float out_b[])
{
    for (uniform int i = 0; i < count; ++i) {
        // reconstruct geometry & tangent space
        uniform float Nx_g = Ngeomx[i]; uniform float Ny_g = Ngeomy[i]; uniform float Nz_g = Ngeomz[i];
        uniform float Tx_v = Tx[i]; uniform float Ty_v = Ty[i]; uniform float Tz_v = Tz[i];
        uniform float Bx_v = Bx[i]; uniform float By_v = By[i]; uniform float Bz_v = Bz[i];
        uniform float ntr_v = ntr[i]; uniform float ntg_v = ntg[i]; uniform float ntb_v = ntb[i];
        uniform float nstr = normal_strength[i];

        // tangent-space normal from texture
        uniform float nx_raw = 2.0f * ntr_v - 1.0f;
        uniform float ny_raw = 2.0f * ntg_v - 1.0f;
        uniform float nz_raw = 2.0f * ntb_v - 1.0f;
        uniform float nx_t = nx_raw * nstr;
        uniform float ny_t = ny_raw * nstr;
        uniform float nz_t = nz_raw;
        // normalize tangent-space normal
        uniform float nlen = sqrt(nx_t*nx_t + ny_t*ny_t + nz_t*nz_t) + 1e-12f;
        nx_t /= nlen; ny_t /= nlen; nz_t /= nlen;

        // transform to world
        uniform float Nx = nx_t * Tx_v + ny_t * Bx_v + nz_t * Nx_g;
        uniform float Ny = nx_t * Ty_v + ny_t * By_v + nz_t * Ny_g;
        uniform float Nz = nx_t * Tz_v + ny_t * Bz_v + nz_t * Nz_g;
        // grazing fade
        uniform float Vx_v = Vx[i]; uniform float Vy_v = Vy[i]; uniform float Vz_v = Vz[i];
        uniform float NdotV_geo = max(0.0f, Nx_g*Vx_v + Ny_g*Vy_v + Nz_g*Vz_v);
        uniform float strength = clamp(NdotV_geo * 5.0f, 0.0f, 1.0f);
        // mix with geometry normal
        Nx = Nx * strength + Nx_g * (1.0f - strength);
        Ny = Ny * strength + Ny_g * (1.0f - strength);
        Nz = Nz * strength + Nz_g * (1.0f - strength);
        // normalize final N
        uniform float nlen2 = sqrt(Nx*Nx + Ny*Ny + Nz*Nz) + 1e-12f;
        Nx /= nlen2; Ny /= nlen2; Nz /= nlen2;

        // lighting math (GGX + Schlick Fresnel + energy-conserving diffuse)
        uniform float Lx_v = Lx[i]; uniform float Ly_v = Ly[i]; uniform float Lz_v = Lz[i];
        uniform float NdotL = max(0.0f, Nx*Lx_v + Ny*Ly_v + Nz*Lz_v);
        uniform float NdotV = max(0.0001f, Nx*Vx_v + Ny*Vy_v + Nz*Vz_v);

        // half vector
        uniform float Hx = Vx_v + Lx_v; uniform float Hy = Vy_v + Ly_v; uniform float Hz = Vz_v + Lz_v;
        uniform float Hlen = sqrt(Hx*Hx + Hy*Hy + Hz*Hz) + 1e-12f;
        Hx /= Hlen; Hy /= Hlen; Hz /= Hlen;
        uniform float NdotH = max(0.0001f, Nx*Hx + Ny*Hy + Nz*Hz);
        uniform float VdotH = max(0.0001f, Vx_v*Hx + Vy_v*Hy + Vz_v*Hz);

        uniform float a = alpha[i]; uniform float a2 = a*a;
        uniform float denom = (NdotH*NdotH)*(a2 - 1.0f) + 1.0f;
        uniform float D = (a2) / (3.14159265f * denom * denom + 1e-12f);
        uniform float k = (a2) * 0.5f;
        uniform float geomV = NdotV / (NdotV * (1.0f - k) + k);
        uniform float geomL = NdotL / (NdotL * (1.0f - k) + k);
        uniform float G = geomV * geomL;

        // Fresnel
        uniform float dielr_v = dielr[i]; uniform float dielg_v = dielg[i]; uniform float dielb_v = dielb[i];
        uniform float baser_v = baser[i]; uniform float baseg_v = baseg[i]; uniform float baseb_v = baseb[i];
        uniform float metall = metallic[i];
        uniform float F0r_v = dielr_v * (1.0f - metall) + baser_v * metall;
        uniform float F0g_v = dielg_v * (1.0f - metall) + baseg_v * metall;
        uniform float F0b_v = dielb_v * (1.0f - metall) + baseb_v * metall;
        uniform float x = 1.0f - VdotH; uniform float x5 = x*x; x5 = x5*x5*x;
        uniform float Fr = F0r_v + (1.0f - F0r_v) * x5;
        uniform float Fg = F0g_v + (1.0f - F0g_v) * x5;
        uniform float Fb = F0b_v + (1.0f - F0b_v) * x5;

        uniform float denom2 = max(1e-6f, 4.0f * NdotV * NdotL);
        uniform float spec_factor = (D * G) / denom2;
        uniform float specr = spec_factor * Fr * NdotL;
        uniform float specg = spec_factor * Fg * NdotL;
        uniform float specb = spec_factor * Fb * NdotL;

        // diffuse
        uniform float Favg = (F0r_v + F0g_v + F0b_v) * (1.0f/3.0f);
        uniform float kdiff = (1.0f - metall) * (1.0f - Favg);
        uniform float dr = baser_v * kdiff * (1.0f / 3.14159265f);
        uniform float dg = baseg_v * kdiff * (1.0f / 3.14159265f);
        uniform float db = baseb_v * kdiff * (1.0f / 3.14159265f);

        out_r[i] = specr + dr * NdotL;
        out_g[i] = specg + dg * NdotL;
        out_b[i] = specb + db * NdotL;
    }
}
