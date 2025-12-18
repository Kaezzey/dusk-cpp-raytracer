#ifndef MNEE_H
#define MNEE_H

#include "vec3.h"
#include "vec2.h"
#include "ray.h"
#include "colour.h"
#include "hittable.h"
#include "interval.h"
#include <cmath>
#include <cstdint>
#include <unordered_map>

struct mnee_config {
    int    newton_max_iters = 8;
    double newton_tol       = 1e-5;
    double step_eps         = 1e-3;   // finite-difference step in param space
    bool   debug            = false;
    int    per_thread_budget = 1024;  // max solves per thread per frame/signature
    int    sun_samples       = 4;      // sample sun disc for a broader footprint
    double sun_ang_radius    = 0.0;    // radians; 0 = directional
    double gain_scale        = 1.0;    // boost MNEE contribution
};

// Use global clamp01 declared elsewhere; do not redefine here.

// Ray-sphere intersection: returns nearest t>0 if hit
static inline bool intersect_sphere(const point3& ro, const vec3& rd, const point3& C, double R, double& tHit)
{
    vec3 oc = ro - C;
    double a = dot(rd, rd);
    double b = 2.0 * dot(oc, rd);
    double c = dot(oc, oc) - R*R;
    double disc = b*b - 4*a*c;
    if (disc < 0.0) return false;
    double sdisc = std::sqrt(disc);
    double t0 = (-b - sdisc) / (2.0*a);
    double t1 = (-b + sdisc) / (2.0*a);
    double t = 1e30;
    if (t0 > 1e-6) t = t0; else if (t1 > 1e-6) t = t1; else return false;
    tHit = t;
    return true;
}

// Fresnel (dielectric) using Schlick's approximation with true eta ratio
static inline double fresnel_schlick_dielectric(double cosThetaI, double eta_i_over_t)
{
    // Convert ratio to indices
    double eta_i = 1.0;
    double eta_t = 1.0;
    if (eta_i_over_t > 0.0) {
        eta_t = 1.0 / eta_i_over_t;
        eta_i = 1.0;
    }
    // Use exact F0 for dielectric interfaces
    double R0 = (eta_i - eta_t) / (eta_i + eta_t);
    R0 = R0 * R0;
    double m = clamp01(1.0 - std::fabs(cosThetaI));
    return R0 + (1.0 - R0) * m*m*m*m*m;
}

// Ideal refraction. Returns false on total internal reflection.
static inline bool refract_dir(const vec3& wi, const vec3& n, double eta_i_over_t, vec3& wt, double& cosThetaI)
{
    // wi: incident direction pointing TOWARD surface (i.e., from previous point to surface)
    vec3 nn = n;
    double eta = eta_i_over_t;
    cosThetaI = dot(-wi, nn); // positive if entering
    double sin2ThetaI = std::max(0.0, 1.0 - cosThetaI * cosThetaI);
    double sin2ThetaT = eta * eta * sin2ThetaI;
    if (sin2ThetaT > 1.0) return false; // TIR
    double cosThetaT = std::sqrt(std::max(0.0, 1.0 - sin2ThetaT));
    wt = eta * wi + (eta * cosThetaI - cosThetaT) * nn;
    wt = unit_vector(wt);
    return true;
}

struct SolveResult {
    bool   ok = false;
    vec3   d0;     // initial direction from receiver
    point3 p1;     // entry point
    point3 p2;     // exit point
    vec3   dir_out;// final direction after refraction
    double T1 = 0.0, T2 = 0.0; // transmittance per interface (1-F)
};

// Evaluate path for parameter u=(u,v) controlling initial direction d0 around a guess
static inline SolveResult evaluate_path(const point3& recvP, const vec3& d_guess,
                                        const vec3& e1, const vec3& e2,
                                        const point3& C, double R, double ior,
                                        const vec3& target_out)
{
    SolveResult sr;
    vec3 d0 = unit_vector(d_guess + e1 * (float)d_guess.x() + e2 * (float)d_guess.y()); // placeholder, overwritten by caller
    (void)d0; (void)e1; (void)e2; (void)ior; (void)target_out; (void)C; (void)R; (void)recvP;
    return sr;
}

// Newton solve for d0 so that final direction matches target_out (-sun_dir)
inline colour mnee_single_sphere_estimate(
    const point3& recvP,
    const vec3&   recvN,
    const vec3&   sun_dir,       // FROM scene toward the sun
    const colour& sun_radiance,
    const point3& sphC,
    double        sphR,
    double        ior,
    const hittable& world,
    const mnee_config& cfg = {})
{
    (void)recvN; // kept for future normal-based clamping

    // Cheap distance gate: skip if receiver is very far from sphere
    {
        vec3 dC = recvP - sphC;
        double d2 = dot(dC, dC);
        double maxR = sphR * 40.0; // generous support radius
        if (d2 > maxR * maxR) return colour(0,0,0);
    }

    // Thread-local tiny cache to avoid repeated solves for nearby receiver points
    struct Key {
        int x, y, z; std::uint64_t sig;
        bool operator==(const Key& o) const { return x==o.x && y==o.y && z==o.z && sig==o.sig; }
    };
    struct KeyHash {
        size_t operator()(const Key& k) const noexcept {
            std::uint64_t h = 1469598103934665603ull;
            auto mix = [&](std::uint64_t v){ h ^= v; h *= 1099511628211ull; };
            mix((std::uint64_t)k.x * 11400714819323198485ull);
            mix((std::uint64_t)k.y * 14029467366897019727ull);
            mix((std::uint64_t)k.z *  9650029242287828579ull);
            mix(k.sig);
            return (size_t)h;
        }
    };
    auto quant = [](double v, double cs)->int{ return (int)std::floor(v / cs); };
    auto qf = [](double v)->std::int64_t{ return (std::int64_t)std::llround(v * 1000.0); };
    std::uint64_t sig = 1469598103934665603ull;
    auto mixu = [&](std::uint64_t v){ sig ^= v; sig *= 1099511628211ull; };
    mixu((std::uint64_t)qf(sun_dir.x())); mixu((std::uint64_t)qf(sun_dir.y())); mixu((std::uint64_t)qf(sun_dir.z()));
    mixu((std::uint64_t)qf(sphC.x()));    mixu((std::uint64_t)qf(sphC.y()));    mixu((std::uint64_t)qf(sphC.z()));
    mixu((std::uint64_t)qf(sphR));        mixu((std::uint64_t)qf(ior));

    Key key{ quant(recvP.x(), 0.01), quant(recvP.y(), 0.01), quant(recvP.z(), 0.01), sig };

    static thread_local std::unordered_map<Key, colour, KeyHash> s_cache;
    static thread_local std::uint64_t s_last_sig = 0;
    static thread_local int s_budget_left = 0;
    if (s_last_sig != sig) { s_cache.clear(); s_last_sig = sig; s_budget_left = cfg.per_thread_budget; }
    if (auto it = s_cache.find(key); it != s_cache.end()) {
        return it->second;
    }
    if (s_budget_left <= 0) return colour(0,0,0);
    --s_budget_left;

    // Target outgoing direction (toward sun): S = -sun_dir
    vec3 S = unit_vector(-sun_dir);
    // Build basis perpendicular to S
    vec3 a = (std::fabs(S.x()) > 0.1) ? vec3(0,1,0) : vec3(1,0,0);
    vec3 sU = unit_vector(cross(a, S));
    vec3 sV = cross(S, sU);

    // Initial guess direction: toward sphere center
    vec3 d_init = unit_vector(sphC - recvP);
    // Local basis around d_init
    vec3 tA = (std::fabs(d_init.x()) > 0.1) ? vec3(0,1,0) : vec3(1,0,0);
    vec3 e1 = unit_vector(cross(tA, d_init));
    vec3 e2 = cross(d_init, e1);

    auto eval = [&](const vec3& d0) -> SolveResult {
        SolveResult out;
        // Intersect receiver ray with sphere (first hit)
        double t1 = 0.0;
        if (!intersect_sphere(recvP + d0 * 1e-4, d0, sphC, sphR, t1)) return out;
        point3 p1 = recvP + d0 * t1;
        vec3 n1 = unit_vector(p1 - sphC);
        // Refract into sphere (air->glass)
        vec3 dir1; double cosI1 = 0.0;
        if (!refract_dir(d0, n1, 1.0/ior, dir1, cosI1)) return out; // TIR shouldn't happen here
        double F1 = fresnel_schlick_dielectric(cosI1, 1.0/ior);
        double T1 = 1.0 - F1;

        // Intersect inside to far side
        double t2 = 0.0;
        if (!intersect_sphere(p1 + dir1 * 1e-4, dir1, sphC, sphR, t2)) return out;
        point3 p2 = p1 + dir1 * t2;
        vec3 n2 = unit_vector(p2 - sphC);
        // exiting: normal should face outward, but wi is inside -> flip normal
        n2 = -n2;

        // Refract out (glass->air)
        vec3 dir2; double cosI2 = 0.0;
        if (!refract_dir(dir1, n2, ior/1.0, dir2, cosI2)) return out; // may TIR for high IOR
        double F2 = fresnel_schlick_dielectric(cosI2, ior/1.0);
        double T2 = 1.0 - F2;

        out.ok = true;
        out.d0 = d0;
        out.p1 = p1;
        out.p2 = p2;
        out.dir_out = unit_vector(dir2);
        out.T1 = T1; out.T2 = T2;
        return out;
    };

    auto residual2 = [&](const vec3& d0) -> vec3 {
        SolveResult r = eval(d0);
        if (!r.ok) return vec3(1e3, 1e3, 0); // large residual
        // Project difference onto (sU, sV)
        vec3 D = r.dir_out - S;
        return vec3(dot(D, sU), dot(D, sV), 0.0f);
    };

    // 2D parameterization u = (ux, uy) around d_init
    auto dir_from_param = [&](double ux, double uy) -> vec3 {
        return unit_vector(d_init + e1 * (float)ux + e2 * (float)uy);
    };

    // Define solver for a given target sun direction
    auto solve_for_S = [&](const vec3& Sdir) -> colour {
        double ux = 0.0, uy = 0.0;
        for (int it = 0; it < cfg.newton_max_iters; ++it) {
            vec3 d0 = dir_from_param(ux, uy);
            // Project residual against the sampled sun direction
            vec3 Sold = S; S = Sdir;
            vec3 f  = residual2(d0);
            S = Sold;
            double err2 = dot(f, f);
            if (err2 < cfg.newton_tol * cfg.newton_tol) {
                SolveResult sol = eval(d0);
                if (!sol.ok) break;
                // Visibility checks
                {
                    hit_record rc;
                    ray visR(recvP + d0 * 1e-4, d0, 0.0);
                    if (world.hit(visR, interval(1e-4, (sol.p1 - recvP).length() - 1e-4), rc)) return colour(0,0,0);
                }
                {
                    hit_record rc;
                    ray visR(sol.p2 + sol.dir_out * 1e-4, sol.dir_out, 0.0);
                    if (world.hit(visR, interval(1e-4, 1e6), rc)) return colour(0,0,0);
                }
                // Ensure exit direction lies within sampled sun cone to suppress outliers
                double cos_accept = std::cos(std::max(0.0, cfg.sun_ang_radius) + 1e-3);
                if (dot(sol.dir_out, unit_vector(Sdir)) < cos_accept) return colour(0,0,0);

                double eta12 = (1.0/ior);
                double eta21 = (ior/1.0);
                double eta_fac = (eta12*eta12) * (eta21*eta21);
                double gain = sol.T1 * sol.T2 * eta_fac * cfg.gain_scale;
                gain = std::max(0.0, std::min(10.0, gain));
                colour contrib = sun_radiance * (float)gain;
                return contrib;
            }
            // Jacobian via forward differences relative to Sdir
            double h = cfg.step_eps;
            vec3 f_x1, f_y1;
            vec3 Sold2 = S; S = Sdir;
            f_x1 = residual2(dir_from_param(ux + h, uy));
            f_y1 = residual2(dir_from_param(ux, uy + h));
            vec2 Jx((f_x1.x() - f.x())/h, (f_x1.y() - f.y())/h);
            vec2 Jy((f_y1.x() - f.x())/h, (f_y1.y() - f.y())/h);
            double a11 = Jx.x(), a21 = Jx.y();
            double a12 = Jy.x(), a22 = Jy.y();
            double det = a11*a22 - a12*a21;
            if (std::fabs(det) < 1e-12) break;
            double inv11 =  a22 / det;
            double inv12 = -a12 / det;
            double inv21 = -a21 / det;
            double inv22 =  a11 / det;
            double dx = -(inv11 * f.x() + inv12 * f.y());
            double dy = -(inv21 * f.x() + inv22 * f.y());
            double alpha = 1.0;
            for (int ls = 0; ls < 5; ++ls) {
                vec3 d_try = dir_from_param(ux + alpha*dx, uy + alpha*dy);
                vec3 Sold3 = S; S = Sdir;
                vec3 f_try = residual2(d_try);
                S = Sold3;
                if (dot(f_try, f_try) < err2) { ux += alpha*dx; uy += alpha*dy; break; }
                alpha *= 0.5;
                if (ls == 4) { ux += alpha*dx; uy += alpha*dy; }
            }
        }
        return colour(0,0,0);
    };

    // Continue into sun-disc sampling below
    int SAMPLES = std::max(1, cfg.sun_samples);
    double ang = std::max(0.0, cfg.sun_ang_radius);
    double cos_theta_max = std::cos(ang);
    colour accum(0,0,0);
    for (int si = 0; si < SAMPLES; ++si) {
        vec3 Sdir = S;
        if (ang > 0.0 && SAMPLES > 1) {
            double u = (double)(si+0.5) / (double)SAMPLES;
            double v = std::fmod(0.61803398875 * si, 1.0); // golden sequence
            double cos_theta = (1.0 - u) + u * cos_theta_max;
            double sin_theta = std::sqrt(std::max(0.0, 1.0 - cos_theta*cos_theta));
            double phi = 2.0 * M_PI * v;
            double x = sin_theta * std::cos(phi);
            double y = sin_theta * std::sin(phi);
            double z = cos_theta;
            Sdir = unit_vector(sU * (float)x + sV * (float)y + S * (float)z);
        }
        accum += solve_for_S(Sdir);
    }
    accum *= (float)(1.0 / (double)SAMPLES);
    // Cache the averaged result only (prevents single-sample outliers from sticking)
    if (s_cache.size() < 4096) s_cache.emplace(key, accum); else { s_cache.clear(); s_cache.emplace(key, accum); }
    return accum;
}

// Estimate caustic contribution for a single point light by searching for
// a refracted path through a single sphere that exits toward the point light
// position. This is a Monte-Carlo / budgeted search (cheap fallback to the
// sun-directional Newton solver) and intentionally simple to keep runtime
// bounded per shading point.
inline colour mnee_single_pointlight_estimate(
    const point3& recvP,
    const vec3&   recvN,
    const point3& lightPos,
    const colour& light_radiance,
    const point3& sphC,
    double        sphR,
    double        ior,
    const hittable& world,
    const mnee_config& cfg = {})
{
    // Cheap distance gate
    {
        vec3 dC = recvP - sphC;
        double d2 = dot(dC, dC);
        double maxR = sphR * 40.0;
        if (d2 > maxR * maxR) return colour(0,0,0);
    }

    int budget = std::max(1, cfg.per_thread_budget);
    colour accum(0,0,0);
    int found = 0;

    // Helper eval function mirrors the logic in the sun solver (intersect,
    // refract in/out) and returns success with key points.
    auto eval = [&](const vec3& d0) -> SolveResult {
        SolveResult out;
        double t1 = 0.0;
        if (!intersect_sphere(recvP + d0 * 1e-4, d0, sphC, sphR, t1)) return out;
        point3 p1 = recvP + d0 * t1;
        vec3 n1 = unit_vector(p1 - sphC);
        vec3 dir1; double cosI1 = 0.0;
        if (!refract_dir(d0, n1, 1.0/ior, dir1, cosI1)) return out;
        double F1 = fresnel_schlick_dielectric(cosI1, 1.0/ior);
        double T1 = 1.0 - F1;

        double t2 = 0.0;
        if (!intersect_sphere(p1 + dir1 * 1e-4, dir1, sphC, sphR, t2)) return out;
        point3 p2 = p1 + dir1 * t2;
        vec3 n2 = unit_vector(p2 - sphC);
        n2 = -n2;

        vec3 dir2; double cosI2 = 0.0;
        if (!refract_dir(dir1, n2, ior/1.0, dir2, cosI2)) return out;
        double F2 = fresnel_schlick_dielectric(cosI2, ior/1.0);
        double T2 = 1.0 - F2;

        out.ok = true;
        out.d0 = d0;
        out.p1 = p1; out.p2 = p2; out.dir_out = unit_vector(dir2);
        out.T1 = T1; out.T2 = T2;
        return out;
    };

    // Sampling loop: importance sample directions toward sphere center
    vec3 toCenter = unit_vector(sphC - recvP);
    for (int i = 0; i < budget; ++i) {
        // jittered direction around center: sample small cone
        double u1 = random_double();
        double u2 = random_double();
        // map u1,u2 to a cosine-weighted perturbation around toCenter
        double phi = 2.0 * M_PI * u2;
        double cosTheta = 1.0 - 0.6 * u1; // bias toward center
        double sinTheta = std::sqrt(std::max(0.0, 1.0 - cosTheta * cosTheta));
        // build orthonormal basis
        vec3 a = (std::fabs(toCenter.x()) > 0.1) ? vec3(0,1,0) : vec3(1,0,0);
        vec3 u = unit_vector(cross(a, toCenter));
        vec3 v = cross(toCenter, u);
        vec3 d0 = unit_vector(u * (float)(sinTheta * std::cos(phi)) + v * (float)(sinTheta * std::sin(phi)) + toCenter * (float)cosTheta);

        SolveResult sol = eval(d0);
        if (!sol.ok) continue;

        // Direction from exit point to light
        vec3 dir_to_light = unit_vector(lightPos - sol.p2);
        double ang = std::acos(std::max(-1.0, std::min(1.0, dot(sol.dir_out, dir_to_light))));
        // Accept small angular error (tunable); allow wider for near lights
        double accept_ang = 0.12; // ~7 degrees
        if (ang > accept_ang) continue;

        // Visibility checks: recv->p1 and p2->light
        {
            hit_record rc;
            ray visR(recvP + sol.d0 * 1e-4, sol.d0, 0.0);
            if (world.hit(visR, interval(1e-4, (sol.p1 - recvP).length() - 1e-4), rc)) continue;
        }
        {
            hit_record rc;
            ray visR(sol.p2 + sol.dir_out * 1e-4, sol.dir_out, 0.0);
            double dist_to_light = (lightPos - sol.p2).length();
            if (world.hit(visR, interval(1e-4, dist_to_light - 1e-4), rc)) continue;
        }

        // Ensure exit direction actually points toward light within tolerance of distance
        double dist = (lightPos - sol.p2).length();
        if (dist < 1e-6) continue;

        // Compute contribution: light radiance attenuated by inverse-square and transmission
        double eta12 = (1.0/ior);
        double eta21 = (ior/1.0);
        double eta_fac = (eta12*eta12) * (eta21*eta21);
        double gain = sol.T1 * sol.T2 * eta_fac * cfg.gain_scale;
        gain = std::max(0.0, std::min(10.0, gain));
        // Attenuate by distance squared
        colour contrib = light_radiance * (float)(gain / (dist * dist));
        accum += contrib;
        ++found;
        // Optionally early exit when we have a hit
        if (found >= 1) break;
    }

    if (found == 0) return colour(0,0,0);
    // average over samples used (simple estimator)
    accum *= (float)(1.0 / (double)found);
    return accum;
}

#endif
