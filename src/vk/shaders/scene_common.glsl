/* Shared by the scene pass's shaders (vkr.c scene_pass): the frame's depth,
 * and the camera that turns a pixel back into a position.
 *
 * Positions are relative to the eye, in the title's world units (SSX 3:
 * centimetres, Y up). inv_m maps (guest pixel x, y, depth - zmax, 1) to
 * them (homogeneous); it is the inverse of the terrain program's own
 * transform, measured on the CPU each frame (vkr_backend.c scene_camera).
 * Depth is used as (d - 1) * zmax: d is within 1% of 1 in a race, and the
 * difference is exact in float where d * zmax - zmax would not be. */

layout(set = 0, binding = 0) uniform sampler2D img;        /* the frame */
#ifdef MS_DEPTH
layout(set = 0, binding = 1) uniform sampler2DMS dep;      /* its depth (MSAA: sample 0) */
#else
layout(set = 0, binding = 1) uniform sampler2D dep;        /* its depth */
#endif

layout(set = 0, binding = 4, std140) uniform S {
    mat4 inv_m;         /* (gx, gy, d - zmax, 1) -> eye-relative position */
    mat4 prev_m;        /* eye-relative position -> last frame's (x w, y w, z w, w) */
    vec4 mb;            /* motion blur strength, near, min distance (nearer: HUD), tags on */
    vec4 dims;          /* guest w, h; guest pixels per target pixel x, y */
    vec4 dims2;         /* zmax, target w, h, time */
    vec4 mb2;           /* motion blur: edge boost, longest blur (pixels at 1080 lines) */
    vec4 mb3;           /* speed blur: focus of expansion (guest pixels), strength now, reach */
    vec4 rider_v[4];    /* rider slot k's motion on screen (guest pixels): .xy even k, .zw odd */
    vec4 ink;           /* Ink & Color: colour, sharpen, lines (0..1), on */
} sc;

ivec2 tsize() { return ivec2(sc.dims2.yz); }

float depth_at(ivec2 p)
{
    p = clamp(p, ivec2(0), tsize() - 1);
    return texelFetch(dep, p, 0).r;
}

/* p: target pixel (centre), d: its depth (0..1) */
vec3 pos_at(vec2 p, float d)
{
    vec4 q = sc.inv_m * vec4(p * sc.dims.zw, (d - 1.0) * sc.dims2.x, 1.0);
    return q.xyz / q.w;
}

bool is_sky(float d) { return d >= 1.0; }

/* Rider tags (xbox.frag TAGS) in the frame's alpha, times 255: 255 player 1
 * (kept sharp), 1..7 another rider's slot (blurred by its own motion), 0 the
 * rest. Without tags (no dualSrcBlend) the alpha is the game's: no pixel is
 * a rider then, and only the near distance keeps the rider sharp. */
int tag_of(float t) { return sc.mb.w > 0.5 ? int(t * 255.0 + 0.5) : 0; }
bool tag_player(float t) { return tag_of(t) == 255; }

/* the motion on screen of the rider a pixel's tag names, guest pixels;
 * false for player 1 and untagged pixels */
bool rider_motion(float t, out vec2 v)
{
    int k = tag_of(t);
    v = vec2(0.0);
    if (k < 1 || k > 7)
        return false;
    vec4 q = sc.rider_v[k >> 1];
    v = (k & 1) != 0 ? q.zw : q.xy;
    return true;
}

/* Interleaved gradient noise (Jimenez, SIGGRAPH 2014): per pixel, fixed
 * over time -- a pattern that changes every frame reads as shimmer. */
float ign(vec2 p)
{
    return fract(52.9829189 * fract(dot(p, vec2(0.06711056, 0.00583715))));
}
