/*
 * Modern race camera (off by default; RECOMP_CAM=1 or the launcher's
 * "Modern camera" checkbox turns it on): the game's chase
 * camera, after each in-game update, gets
 *   - a smoother follow: the eye's offset from the rider and the view
 *     direction are spring-filtered (heights more than the rest, so
 *     landings and bumps no longer jolt the view), plus a little look-ahead
 *     into turns;
 *   - roll into carves, and shake on landings (by impact speed) and at
 *     high speed;
 *   - a wider field of view with speed (sub_0024EA13 wrapper,
 *     ssx3_CamFovScale).
 *
 * The camera: player 1's view object [[ingame state+0x84]+4] (vtable
 * 0x0028630C), finished each update by sub_0005C000: +0x30 eye, +0x40
 * orientation quaternion (its matrix has the rows forward, -right, up),
 * +0x50 the D3D view matrix (rows (r.x u.x f.x 0) .. and -eye.(r u f) 1),
 * +0x90 forward. The renderer reads them after the update. The game's own
 * values are put back before the next update (ssx3_CamPreUpdate), so its
 * camera logic never sees ours.
 *
 * The filtered eye can lag into rising terrain, so it is kept clear
 * (keep_clear): never more than 15 cm below the game's eye, pulled back
 * where the line from the game's eye to it crosses terrain, and held 40 cm
 * above the ground under it -- the world's collision query, the same one
 * the rider physics use.
 *
 * Only while the race is on: not at the start gate (player mode Static 3 /
 * Gate 12), not on other cameras (fly-over, replays). A camera cut (the
 * game's eye jumping) resets the filters.
 *
 *   RECOMP_CAM=1              on
 *   RECOMP_CAM_SMOOTH=<f>     follow smoothing scale (default 1, 0 = none)
 *   RECOMP_CAM_FOV=<deg>      extra vertical field of view at top speed (8)
 *   RECOMP_CAM_ROLL=<f>       roll scale (default 1)
 *   RECOMP_CAM_SHAKE=<f>      shake scale (default 1)
 *   RECOMP_CAM_TRACE=1        log landings and filter state once a second
 */
#ifdef _WIN32
#include <windows.h>
#else
#include "platform/win32_compat.h"
#endif
#include "recomp/gen/recomp_types.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define VT_INGAME   VA(0x00299D7C)
#define VT_RIDERMAN VA(0x0028216C)
#define VT_CAMERA   VA(0x0028630C)
#define CAM_SAVE    0x70u            /* +0x30 .. +0xA0: eye, quaternion, view, forward */

extern uint32_t xbox_TickCountMs(void);

static int s_inited, s_on, s_trace;
static float s_k_smooth = 1.0f, s_fov_deg = 8.0f, s_k_roll = 1.0f, s_k_shake = 1.0f;

/* game thread */
static uint32_t s_cam;                    /* camera we changed, 0 = none */
static uint32_t s_saved[CAM_SAVE / 4];    /* the game's values there */
static int s_have;                        /* filters primed */
static float s_off[3], s_fwd[3];          /* filtered eye offset, forward */
static float s_prev_eye[3], s_prev_head, s_yawrate, s_roll;
static float s_shake, s_speed_s, s_prev_vz;
static int s_prev_mode = -1;
static uint32_t s_last_ms, s_seed = 1;
static float s_ph[6];

/* read by the projection wrapper */
static volatile float s_fov_scale = 1.0f; /* tan(fov/2) multiplier */

static int guest_ok(uint32_t va)
{
    return va >= 0x10000u && (va < 0x04000000u || (va >= 0x80000000u && va < 0x84000000u));
}

static float rdf(uint32_t va) { return MEMF(va); }
static void wrf(uint32_t va, float v) { MEMF(va) = v; }
static float dot3(const float *a, const float *b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
static float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }

static float norm3(float *v)
{
    float l = sqrtf(dot3(v, v));
    if (l > 1e-6f) { v[0] /= l; v[1] /= l; v[2] /= l; }
    return l;
}

static void cross3(const float *a, const float *b, float *o)
{
    o[0] = a[1] * b[2] - a[2] * b[1];
    o[1] = a[2] * b[0] - a[0] * b[2];
    o[2] = a[0] * b[1] - a[1] * b[0];
}

static float wrap_pi(float a)
{
    while (a > 3.14159265f) a -= 6.2831853f;
    while (a < -3.14159265f) a += 6.2831853f;
    return a;
}

/* Exponential approach towards a target over time constant tau (s). */
static float follow(float dt, float tau)
{
    return tau <= 1e-4f ? 1.0f : 1.0f - expf(-dt / tau);
}

static void cam_init(void)
{
    const char *e;
    s_inited = 1;
    e = getenv("RECOMP_CAM");
    s_on = e && *e && *e != '0';
    if ((e = getenv("RECOMP_CAM_SMOOTH"))) s_k_smooth = (float)atof(e);
    if ((e = getenv("RECOMP_CAM_FOV"))) s_fov_deg = (float)atof(e);
    if ((e = getenv("RECOMP_CAM_ROLL"))) s_k_roll = (float)atof(e);
    if ((e = getenv("RECOMP_CAM_SHAKE"))) s_k_shake = (float)atof(e);
    s_trace = getenv("RECOMP_CAM_TRACE") != NULL;
    if (s_on)
        fprintf(stderr, "[CAM] modern race camera on (smooth %.2f, fov +%.1f deg, roll %.2f, shake %.2f)\n",
                s_k_smooth, s_fov_deg, s_k_roll, s_k_shake);
}

/* Player 1's rider while the race is on, and the chase camera. */
static uint32_t player_and_camera(uint32_t state, uint32_t *cam, int *mode)
{
    uint32_t g = MEM32(VA(0x00369534)), a, m, b, pm, phys, c;
    if (!guest_ok(state) || MEM32(state) != VT_INGAME)
        return 0;
    if (!guest_ok(g) || !guest_ok(a = MEM32(g + 0x74)) || !guest_ok(m = MEM32(a + 0xC))
            || MEM32(m) != VT_RIDERMAN || !guest_ok(b = MEM32(m + 0x28))
            || !guest_ok(phys = MEM32(b + 0x74C)))
        return 0;
    pm = MEM32(state + 0x84);
    if (!guest_ok(pm) || MEM32(pm + 0x10) < 1)
        return 0;
    c = MEM32(pm + 4);
    if (!guest_ok(c) || MEM32(c) != VT_CAMERA)
        return 0;
    *cam = c;
    *mode = (int)MEM32(phys + 0xE60);
    return b;
}

/* Quaternion of the rotation whose matrix has rows a, b, c. */
static void mat_to_quat(const float a[3], const float b[3], const float c[3], float q[4])
{
    float tr = a[0] + b[1] + c[2], s;
    if (tr > 0.0f) {
        s = sqrtf(tr + 1.0f) * 2.0f;
        q[3] = 0.25f * s; q[0] = (c[1] - b[2]) / s; q[1] = (a[2] - c[0]) / s; q[2] = (b[0] - a[1]) / s;
    } else if (a[0] > b[1] && a[0] > c[2]) {
        s = sqrtf(1.0f + a[0] - b[1] - c[2]) * 2.0f;
        q[3] = (c[1] - b[2]) / s; q[0] = 0.25f * s; q[1] = (a[1] + b[0]) / s; q[2] = (a[2] + c[0]) / s;
    } else if (b[1] > c[2]) {
        s = sqrtf(1.0f + b[1] - a[0] - c[2]) * 2.0f;
        q[3] = (a[2] - c[0]) / s; q[0] = (a[1] + b[0]) / s; q[1] = 0.25f * s; q[2] = (b[2] + c[1]) / s;
    } else {
        s = sqrtf(1.0f + c[2] - a[0] - b[1]) * 2.0f;
        q[3] = (b[0] - a[1]) / s; q[0] = (a[2] + c[0]) / s; q[1] = (b[2] + c[1]) / s; q[2] = 0.25f * s;
    }
}

/* Write eye and basis (right r, up u, forward f; orthonormal). */
static void write_camera(uint32_t cam, const float eye[3], const float r[3], const float u[3], const float f[3])
{
    float nr[3] = {-r[0], -r[1], -r[2]}, q[4];
    int i;
    for (i = 0; i < 3; i++) wrf(cam + 0x30 + 4u * i, eye[i]);
    wrf(cam + 0x3C, 1.0f);
    mat_to_quat(f, nr, u, q);
    for (i = 0; i < 4; i++) wrf(cam + 0x40 + 4u * i, q[i]);
    for (i = 0; i < 3; i++) {
        wrf(cam + 0x50 + 0x10u * i + 0, r[i]);
        wrf(cam + 0x50 + 0x10u * i + 4, u[i]);
        wrf(cam + 0x50 + 0x10u * i + 8, f[i]);
        wrf(cam + 0x50 + 0x10u * i + 12, 0.0f);
    }
    wrf(cam + 0x80, -dot3(r, eye));
    wrf(cam + 0x84, -dot3(u, eye));
    wrf(cam + 0x88, -dot3(f, eye));
    wrf(cam + 0x8C, 1.0f);
    for (i = 0; i < 3; i++) wrf(cam + 0x90 + 4u * i, f[i]);
    wrf(cam + 0x9C, 0.0f);
}

/* ---------------------------------------------------- terrain checks
 *
 * The world's collision query, as the rider physics use it: sub_001BD340
 * (thiscall on a collision set: query, hits, max, cache; ret 0x10; returns
 * the count of 0x80-byte hit records, +0x00 contact, +0x10 normal, +0x40 t)
 * with a segment query built by sub_001BA0F0 (start*, end*, refT, mode;
 * ret 0x10). A rider's own set [rider+0x830] holds only what is around
 * the rider, so a set is built for the segment's box: sub_001C0310(box*)
 * fills one from the scene (its +0), box = min vec4, max vec4.
 */
extern void sub_001BA0F0(void);
extern void sub_001BD340(void);
extern void sub_001C0310(void);

/* The first terrain hit along a -> b: its t (0..1) and point; up_only
 * skips hits on surfaces facing down or sideways. */
static int terrain_hit(uint32_t world, const float a[3], const float b[3], int up_only,
                       float *t_out, float hit[3])
{
    uint32_t sv_eax = g_eax, sv_ebx = g_ebx, sv_ecx = g_ecx, sv_edx = g_edx;
    uint32_t sv_esi = g_esi, sv_edi = g_edi, sv_ebp = g_ebp, sv_esp = g_esp;
    /* below the live stack: calls' frames under base; points, box, query,
     * 32 hit records (plus slack: the terrain loop does not check the
     * maximum) and the collision set above it */
    uint32_t base = (g_esp - 0x3000u) & ~0xFu, pa = base, pb = base + 0x10, box = base + 0x20;
    uint32_t q = base + 0x40, hits = base + 0x200, tw = base + 0x2000, n, i, best = 0;
    float best_t = 2.0f;
    int k;
    if (!guest_ok(world))
        return 0;
    for (k = 0; k < 3; k++) {
        wrf(pa + 4u * k, a[k]);
        wrf(pb + 4u * k, b[k]);
        wrf(box + 4u * k, fminf(a[k], b[k]) - 100.0f);
        wrf(box + 0x10 + 4u * k, fmaxf(a[k], b[k]) + 100.0f);
    }
    wrf(pa + 12, 1.0f); wrf(pb + 12, 1.0f);
    wrf(box + 12, 1.0f); wrf(box + 0x1C, 1.0f);

    memset((void *)&MEM8(tw), 0, 0xA20);
    ST32(tw, MEM32(world));
    g_esp = base;
    PUSH32(g_esp, box);
    PUSH32(g_esp, 0);
    g_ecx = tw;
    sub_001C0310();

    g_esp = base;
    PUSH32(g_esp, 2);                          /* mode, as the ground probe */
    PUSH32(g_esp, 0x3F000000u);                /* refT 0.5 */
    PUSH32(g_esp, pb);
    PUSH32(g_esp, pa);
    PUSH32(g_esp, 0);
    g_ecx = q;
    sub_001BA0F0();

    g_esp = base;
    PUSH32(g_esp, 0);                          /* no cache */
    PUSH32(g_esp, 32);
    PUSH32(g_esp, hits);
    PUSH32(g_esp, q);
    PUSH32(g_esp, 0);
    g_ecx = tw;
    sub_001BD340();
    n = g_eax;

    for (i = 0; i < n && i < 32; i++) {
        uint32_t r = hits + i * 0x80;
        float t = rdf(r + 0x40);
        if (t > 0 && t < best_t && (!up_only || rdf(r + 0x18) > 0.3f)) { best_t = t; best = r; }
    }
    if (best) {
        *t_out = best_t;
        for (k = 0; k < 3; k++) hit[k] = rdf(best + 4u * k);
    }
    g_eax = sv_eax; g_ebx = sv_ebx; g_ecx = sv_ecx; g_edx = sv_edx;
    g_esi = sv_esi; g_edi = sv_edi; g_ebp = sv_ebp; g_esp = sv_esp;
    return best != 0;
}

/* Keep the eye out of the terrain: no lower than 15 cm under the game's
 * eye E (its own camera avoids the ground); pulled back towards E where
 * terrain lies between them; and at least 40 cm above the ground under it. */
static int s_clip_fixes;

static void keep_clear(uint32_t world, const float E[3], float eye[3])
{
    float t, hit[3], top[3], bot[3];
    int k;
    if (eye[2] < E[2] - 15.0f)
        eye[2] = E[2] - 15.0f;
    if (terrain_hit(world, E, eye, 0, &t, hit)) {
        /* stop short of the surface, 30 cm back along the line */
        float d[3] = {eye[0] - E[0], eye[1] - E[1], eye[2] - E[2]}, l = sqrtf(dot3(d, d));
        float keep = l > 1e-3f ? fmaxf(0.0f, t - 30.0f / l) : 0.0f;
        for (k = 0; k < 3; k++) eye[k] = E[k] + d[k] * keep;
        s_clip_fixes++;
    }
    for (k = 0; k < 3; k++) { top[k] = eye[k]; bot[k] = eye[k]; }
    top[2] += 150.0f;
    bot[2] -= 40.0f;
    if (terrain_hit(world, top, bot, 1, &t, hit) && hit[2] > eye[2] - 40.0f) {
        eye[2] = hit[2] + 40.0f;
        s_clip_fixes++;
    }
}

static float rnd01(void)
{
    s_seed = s_seed * 1664525u + 1013904223u;
    return (float)(s_seed >> 8) / 16777216.0f;
}

/* Before the in-game update: give the game its own camera back. */
void ssx3_CamPreUpdate(void)
{
    uint32_t k;
    if (!s_cam)
        return;
    if (guest_ok(s_cam) && MEM32(s_cam) == VT_CAMERA)
        for (k = 0; k < CAM_SAVE / 4; k++)
            ST32(s_cam + 0x30 + 4u * k, s_saved[k]);
    s_cam = 0;
}

/* After the in-game update (this = the in-game state). */
void ssx3_CamPostUpdate(uint32_t state)
{
    uint32_t cam = 0, me, now = xbox_TickCountMs(), k;
    float E[3], r[3], u[3], f[3], P[3], V[3], dt, speed, off[3], eye[3], head, a;
    int mode = -1, i;

    if (!s_inited)
        cam_init();
    me = s_on ? player_and_camera(state, &cam, &mode) : 0;
    if (!me || mode == 3 || mode == 12) {
        s_have = 0;
        s_fov_scale = 1.0f;
        s_prev_mode = -1;
        return;
    }
    dt = s_last_ms && now > s_last_ms ? (float)(now - s_last_ms) / 1000.0f : 1.0f / 60.0f;
    if (dt > 0.1f) dt = 0.1f;
    s_last_ms = now;

    for (i = 0; i < 3; i++) {
        E[i] = rdf(cam + 0x30 + 4u * i);
        r[i] = rdf(cam + 0x50 + 0x10u * i + 0);
        u[i] = rdf(cam + 0x50 + 0x10u * i + 4);
        f[i] = rdf(cam + 0x50 + 0x10u * i + 8);
        P[i] = rdf(me + 0x180 + 4u * i);
        V[i] = rdf(me + 0x250 + 4u * i);
    }
    if (!isfinite(E[0] + E[1] + E[2] + P[0] + P[1] + P[2] + V[0] + V[1] + V[2]) || norm3(f) < 0.5f)
        return;
    speed = sqrtf(dot3(V, V));
    for (i = 0; i < 3; i++) off[i] = E[i] - P[i];

    /* a cut: the game's eye jumped, or a fresh start */
    if (s_have) {
        float d[3] = {E[0] - s_prev_eye[0], E[1] - s_prev_eye[1], E[2] - s_prev_eye[2]};
        if (sqrtf(dot3(d, d)) > 600.0f + speed * dt * 2.0f || dot3(f, s_fwd) < 0.5f)
            s_have = 0;
    }
    memcpy(s_prev_eye, E, sizeof E);
    head = atan2f(V[1], V[0]);
    if (!s_have) {
        memcpy(s_off, off, sizeof off);
        memcpy(s_fwd, f, sizeof f);
        s_prev_head = head;
        s_yawrate = s_roll = s_shake = 0.0f;
        s_speed_s = speed;
        for (i = 0; i < 6; i++) s_ph[i] = rnd01() * 6.2831853f;
        s_have = 1;
    }

    /* follow: horizontal offset 0.10 s, height 0.22 s, view 0.08 s */
    a = follow(dt, 0.10f * s_k_smooth);
    s_off[0] += (off[0] - s_off[0]) * a;
    s_off[1] += (off[1] - s_off[1]) * a;
    s_off[2] += (off[2] - s_off[2]) * follow(dt, 0.22f * s_k_smooth);
    a = follow(dt, 0.08f * s_k_smooth);
    for (i = 0; i < 3; i++) s_fwd[i] += (f[i] - s_fwd[i]) * a;
    norm3(s_fwd);
    for (i = 0; i < 3; i++) eye[i] = P[i] + s_off[i];
    {
        /* never more than 1.2 m from where the game put it (terrain) */
        float d[3] = {eye[0] - E[0], eye[1] - E[1], eye[2] - E[2]}, l = sqrtf(dot3(d, d));
        if (l > 120.0f)
            for (i = 0; i < 3; i++) eye[i] = E[i] + d[i] * 120.0f / l;
    }

    /* turn rate of the travel direction (only when moving) */
    if (sqrtf(V[0] * V[0] + V[1] * V[1]) > 300.0f) {
        float yr = wrap_pi(head - s_prev_head) / dt;
        s_yawrate += (clampf(yr, -3.0f, 3.0f) - s_yawrate) * follow(dt, 0.15f);
    } else
        s_yawrate *= 1.0f - follow(dt, 0.3f);
    s_prev_head = head;

    /* look-ahead: turn the view a little further into the turn */
    {
        float yaw = clampf(s_yawrate * 0.10f, -0.08f, 0.08f), c = cosf(yaw), s = sinf(yaw);
        float fx = s_fwd[0] * c - s_fwd[1] * s, fy = s_fwd[0] * s + s_fwd[1] * c;
        f[0] = fx; f[1] = fy; f[2] = s_fwd[2];
        norm3(f);
    }

    /* landing: impact from the vertical speed just before touching down */
    if (s_prev_mode == 1 && mode == 0) {
        float imp = clampf((-s_prev_vz - 900.0f) / 3000.0f, 0.0f, 1.0f);
        if (imp > s_shake) s_shake = imp;
        if (s_trace)
            fprintf(stderr, "[CAM] t=%u landing at %.0f cm/s down: shake %.2f\n", now, -s_prev_vz, imp);
    }
    s_prev_mode = mode;
    s_prev_vz = V[2];
    s_shake *= expf(-dt / 0.28f);

    /* basis: right = f x Z, up = right x f, then the roll about f */
    r[0] = f[1]; r[1] = -f[0]; r[2] = 0.0f;
    if (norm3(r) < 1e-4f) { r[0] = 1.0f; r[1] = 0.0f; }
    cross3(r, f, u);
    {
        float target = clampf(s_yawrate * 0.12f, -0.06f, 0.06f) * s_k_roll, c, s, nr[3], nu[3];
        s_roll += (target - s_roll) * follow(dt, 0.25f);
        c = cosf(s_roll); s = sinf(s_roll);
        for (i = 0; i < 3; i++) {
            nr[i] = r[i] * c + u[i] * s;
            nu[i] = u[i] * c - r[i] * s;
        }
        memcpy(r, nr, sizeof r);
        memcpy(u, nu, sizeof u);
    }

    /* shake: landing impact plus a high-speed rumble; a few incommensurate
     * sines per axis, on the eye (cm) and a little on the view (rad) */
    {
        float rumble = clampf((speed - 1800.0f) / 2000.0f, 0.0f, 1.0f) * 0.25f;
        float amp = (s_shake + rumble) * s_k_shake, t = (float)now / 1000.0f, sx, sy, pj, yj, fo[3];
        sx = sinf(t * 2.0f * 3.14159f * 17.0f + s_ph[0]) * 0.6f + sinf(t * 2.0f * 3.14159f * 29.0f + s_ph[1]) * 0.4f;
        sy = sinf(t * 2.0f * 3.14159f * 21.0f + s_ph[2]) * 0.6f + sinf(t * 2.0f * 3.14159f * 33.0f + s_ph[3]) * 0.4f;
        pj = sinf(t * 2.0f * 3.14159f * 13.0f + s_ph[4]);
        yj = sinf(t * 2.0f * 3.14159f * 11.0f + s_ph[5]);
        for (i = 0; i < 3; i++)
            eye[i] += (r[i] * sx * 4.0f + u[i] * sy * 6.0f) * amp;
        for (i = 0; i < 3; i++)
            fo[i] = f[i] + (u[i] * pj * 0.012f + r[i] * yj * 0.008f) * amp;
        memcpy(f, fo, sizeof f);
        norm3(f);
        /* re-orthonormalise r and u around the shaken f, keeping the roll */
        {
            float d = dot3(r, f);
            for (i = 0; i < 3; i++) r[i] -= f[i] * d;
            norm3(r);
            cross3(r, f, u);
        }
    }

    keep_clear(MEM32(me + 0x830), E, eye);

    /* field of view: wider with speed (smoothed over ~0.4 s) */
    {
        /* 0.776 = tan(half the vertical fov) of SSX 3's chase camera */
        float sk, half;
        s_speed_s += (speed - s_speed_s) * follow(dt, 0.4f);
        sk = clampf((s_speed_s - 800.0f) / 2200.0f, 0.0f, 1.0f);
        sk = sk * sk * (3.0f - 2.0f * sk);
        half = atanf(0.776f) + sk * s_fov_deg * 3.14159265f / 360.0f;
        s_fov_scale = tanf(half) / 0.776f;
    }

    /* keep the game's values for the next update, then write ours */
    for (k = 0; k < CAM_SAVE / 4; k++)
        s_saved[k] = MEM32(cam + 0x30 + 4u * k);
    s_cam = cam;
    write_camera(cam, eye, r, u, f);

    if (s_trace) {
        static uint32_t next;
        if (now >= next) {
            float d[3] = {eye[0] - E[0], eye[1] - E[1], eye[2] - E[2]};
            fprintf(stderr, "[CAM] speed %.0f, eye off the game's %.0f cm, yaw rate %.2f, roll %.3f, shake %.2f, fov x%.3f, terrain fixes %d\n",
                    speed, sqrtf(dot3(d, d)), s_yawrate, s_roll, s_shake, s_fov_scale, s_clip_fixes);
            s_clip_fixes = 0;
            {
                /* self-check: the ground under the eye (must be found) */
                float t, hit[3], lo[3] = {eye[0], eye[1], eye[2] - 1000.0f};
                if (terrain_hit(MEM32(me + 0x830), eye, lo, 1, &t, hit))
                    fprintf(stderr, "[CAM] ground %.0f cm under the eye\n", eye[2] - hit[2]);
                else
                    fprintf(stderr, "[CAM] no ground within 10 m under the eye\n");
            }
            next = now + 1000;
        }
    }
}

/* The projection wrapper's field-of-view factor for tan(fovy / 2). */
float ssx3_CamFovScale(void)
{
    if (s_trace && s_cam) {
        static uint32_t next;
        uint32_t now = xbox_TickCountMs();
        if (now >= next) {
            fprintf(stderr, "[CAM] projection: fov x%.3f\n", s_fov_scale);
            next = now + 1000;
        }
    }
    return s_cam ? s_fov_scale : 1.0f;
}
