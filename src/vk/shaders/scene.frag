#version 450
#extension GL_GOOGLE_include_directive : require

/* The scene pass (vkr.c scene_pass), on the finished 3D image before the HUD
 * is drawn over it (vkr_scene_run): motion blur that sells speed.
 *
 * Each pixel's blur is the sum of
 *   camera blur (RECOMP_MBLUR)  the pixel reprojected with last frame's
 *                               camera: turns, and the near snow rushing by;
 *   speed blur (speed=)         radial streaks away from where the camera
 *                               is heading, growing with the camera's speed
 *                               (vkr_backend.c scene_flip) -- far terrain
 *                               and sky barely move between frames, so
 *                               reprojection alone shows little speed;
 *                               full near the camera, less far away (reach=);
 * stronger towards the screen edges (edge=) and at most cap= pixels long.
 *
 * Gather along the blur in the manner of McGuire et al., "A Reconstruction
 * Filter for Plausible Motion Blur" (I3D 2012): each sample is weighed by
 * which of the two is in front (a soft depth compare) and whether the
 * nearer one's blur reaches the other, so near geometry does not smear
 * into what lies behind it and vice versa. Player 1 (tag) and HUD models
 * stay sharp and are never sampled; the other riders blur by their own
 * motion on screen (vkr_backend.c scene_flip), so one riding alongside
 * stays sharp and one passing streaks.
 *
 * Then the Ink & Color look (RECOMP_INK, specs ssx3-mods.md), on the 3D
 * world only (3D HUD models, nearer than mindist, keep their look):
 *   Color    saturation pushed out from each pixel's luminance, more for dull
 *            colours than strong ones; greys stay grey, nothing clips;
 *   Sharpen  the pixel minus its four neighbours' mean, added back, less
 *            where the local contrast is already high (no halos);
 *   Ink      dark lines where the distance to the eye jumps (a step on one
 *            side of a pixel, not the other), across four axes, and around
 *            silhouettes against the sky. Slopes and creases move both sides
 *            alike and give none, so the snow stays clean; specks a pixel or
 *            two wide give none either. Lighter far away. */

#include "scene_common.glsl"

layout(location = 0) out vec4 o;

const int TAPS = 16;

float scl;                      /* pixel sizes are for 1080 lines */

/* the blur at target pixel p with depth d, in target pixels; zero where
 * nothing blurs (player 1, HUD models, nearer than the near distance) */
vec2 blur_at(vec2 p, float d, float tag, out float dist)
{
    bool sky = is_sky(d);
    vec3 P = pos_at(p, sky ? 1.0 : d);
    vec2 v;
    dist = sky ? 1e9 : length(P);
    if (tag_player(tag) || (!sky && (dist < sc.mb.z || dist < sc.mb.y)))
        return vec2(0.0);
    if (rider_motion(tag, v)) {
        v = v / sc.dims.zw * sc.mb.x;
    } else {
        v = vec2(0.0);
        vec4 pc = sc.prev_m * vec4(P, 1.0);
        if (pc.w > 1e-3)
            v = (p - pc.xy / pc.w / sc.dims.zw) * sc.mb.x;
        /* near snow streaks, the far backdrop much less (it barely moves) */
        v += (p - sc.mb3.xy / sc.dims.zw) * sc.mb3.z * clamp(sc.mb3.w / dist, 0.15, 1.0);
        vec2 e = p / sc.dims2.yz * 2.0 - 1.0;
        v *= 1.0 + sc.mb2.x * dot(e, e) * 0.5;
    }
    float len = length(v), mx = sc.mb2.y * scl;
    return len > mx ? v * (mx / len) : v;
}

float cone(float x, float len) { return clamp(1.0 - x / max(len, 1e-3), 0.0, 1.0); }
float cylinder(float x, float len) { return 1.0 - smoothstep(0.95 * len, 1.05 * len, x); }
/* 1 when a is in front of b, fading over 5% of the distance */
float in_front(float a, float b) { return clamp(1.0 - (a - b) / (0.05 * max(b, 1.0)), 0.0, 1.0); }

float lum(vec3 c) { return dot(c, vec3(0.2126, 0.7152, 0.0722)); }

/* distance to the eye at a target pixel; 1e9 for the sky */
float dist_at(ivec2 q)
{
    q = clamp(q, ivec2(0), tsize() - 1);
    float d = depth_at(q);
    return is_sky(d) ? 1e9 : length(pos_at(vec2(q) + 0.5, d));
}

vec3 fetch(ivec2 q) { return texelFetch(img, clamp(q, ivec2(0), tsize() - 1), 0).rgb; }

vec3 ink_look(ivec2 ip, vec3 col, float dc)
{
    if (sc.ink.y > 0.0) {
        vec3 n = fetch(ip + ivec2(0, 1)), s = fetch(ip - ivec2(0, 1));
        vec3 e = fetch(ip + ivec2(1, 0)), w = fetch(ip - ivec2(1, 0));
        float lo = min(min(min(lum(n), lum(s)), min(lum(e), lum(w))), lum(col));
        float hi = max(max(max(lum(n), lum(s)), max(lum(e), lum(w))), lum(col));
        float k = sc.ink.y * 0.8 * (1.0 - smoothstep(0.08, 0.5, hi - lo));
        col = clamp(col + (col - (n + s + e + w) * 0.25) * k, 0.0, 1.0);
    }
    if (sc.ink.x > 0.0) {
        float l = lum(col);
        float chroma = max(col.r, max(col.g, col.b)) - min(col.r, min(col.g, col.b));
        col = clamp(vec3(l) + (col - vec3(l)) * (1.0 + sc.ink.x * 0.7 * (1.0 - chroma)), 0.0, 1.0);
    }
    if (sc.ink.z > 0.0 && dc < 1e8) {
        const ivec2 ax[4] = ivec2[](ivec2(1, 0), ivec2(0, 1), ivec2(1, 1), ivec2(1, -1));
        int r = max(1, int(scl + 0.5));        /* ~2 px lines at 1080 lines, more above */
        float edge = 0.0;
        for (int i = 0; i < 4; i++) {
            float a = dist_at(ip + ax[i] * r), b = dist_at(ip - ax[i] * r);
            if (a > 1e8 || b > 1e8 || dist_at(ip + ax[i] * 2 * r) > 1e8 || dist_at(ip - ax[i] * 2 * r) > 1e8) {
                edge = 1.0;                     /* against the sky: a silhouette */
                continue;
            }
            /* A step: one side jumps, the other follows the surface. A slope or
             * a crease moves both sides alike, so it cancels: no lines on snow. */
            float step = abs(abs(a - dc) - abs(b - dc)) / dc;
            /* A speck (a tiny or far object, a pixel or two wide) gets no line:
             * the nearer side must carry on one step further. */
            ivec2 nd = a < b ? ax[i] : -ax[i];
            float dn = min(a, b), beyond = dist_at(ip + nd * 2 * r);
            if (abs(beyond - dn) > 0.1 * dn)
                step = 0.0;
            edge = max(edge, step);
        }
        float line = smoothstep(0.04, 0.14, edge);
        float fade = 1.0 - 0.75 * smoothstep(3000.0, 40000.0, dc);   /* 30 m .. 400 m */
        col = mix(col, vec3(0.03), sc.ink.z * line * fade);
    }
    return col;
}

void main()
{
    scl = sc.dims2.z / 1080.0;
    vec2 p = gl_FragCoord.xy;
    vec4 src = texelFetch(img, ivec2(p), 0);
    float zc;
    vec2 v = blur_at(p, depth_at(ivec2(p)), src.a, zc);
    float lc = length(v);
    vec3 col = src.rgb;
    if (lc >= 0.5) {
        /* McGuire's weights: the centre counts as 1 / its blur length, each
         * sample by how much of it can reach (or be seen behind) the centre */
        float wc = 1.0 / lc;
        vec3 acc = src.rgb * wc;
        float wsum = wc, j = ign(p) - 0.5;
        for (int i = 0; i < TAPS; i++) {
            float t = (float(i) + 0.5 + j) / float(TAPS) - 0.5;
            if (abs(t) * lc < 0.5)
                continue;                                   /* the centre itself */
            vec2 q = clamp(p + v * t, vec2(0.5), sc.dims2.yz - 0.5);
            vec4 sq = texelFetch(img, ivec2(q), 0);
            if (tag_player(sq.a))
                continue;
            float zs;
            float ls = length(blur_at(q, depth_at(ivec2(q)), sq.a, zs));
            float x = length(q - p);
            float w = in_front(zs, zc) * cone(x, ls)       /* a nearer sample blurs over us */
                    + in_front(zc, zs) * cone(x, lc)       /* we blur over what is behind */
                    + cylinder(x, ls) * cylinder(x, lc) * 2.0;
            acc += textureLod(img, q / sc.dims2.yz, 0.0).rgb * w;
            wsum += w;
        }
        col = clamp(acc / wsum, 0.0, 1.0);
    }
    if (sc.ink.w > 0.5 && zc >= sc.mb.z)
        col = ink_look(ivec2(p), col, zc);
    o = vec4(col, src.a);
}
