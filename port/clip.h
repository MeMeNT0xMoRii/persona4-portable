#ifndef P4_CLIP_H
#define P4_CLIP_H

/* The GE has no near-plane clipping: a triangle with a corner behind the camera is dropped whole, which shows as
   hard straight edges in dense fog. Clip a particle quad here instead. Needs Vertex3D {u, v, colour, x, y, z}. */
enum { CLIP_MAX_OUT = 9 }; /* a quad cut by one plane has at most 5 corners: 3 triangles */

/* quad: 4 corners in order around the edge; depth[i]: view distance of each; keeps depth >= near.
   Writes triangles to out, returns the vertex count (0, 6 or up to 9). */
static int clip_quad_near(const Vertex3D quad[4], const float depth[4], float near, Vertex3D *out)
{
    Vertex3D poly[5];
    int n = 0;
    for (int i = 0; i < 4; i++) {
        const Vertex3D *a = &quad[i], *b = &quad[(i + 1) & 3];
        float da = depth[i] - near, db = depth[(i + 1) & 3] - near;
        if (da >= 0)
            poly[n++] = *a;
        if ((da >= 0) != (db >= 0)) {
            float t = da / (da - db);
            Vertex3D *v = &poly[n++];
            v->u = a->u + (b->u - a->u) * t;
            v->v = a->v + (b->v - a->v) * t;
            v->colour = a->colour;
            v->x = a->x + (b->x - a->x) * t;
            v->y = a->y + (b->y - a->y) * t;
            v->z = a->z + (b->z - a->z) * t;
        }
    }
    int count = 0;
    for (int i = 1; i + 1 < n; i++) {
        out[count++] = poly[0];
        out[count++] = poly[i];
        out[count++] = poly[i + 1];
    }
    return count;
}

/* The GE also drops a whole triangle when one corner lands outside its 4096-pixel window (2048 px each side of the
   viewport centre): big ground triangles beside the camera vanish on the PSP, PPSSPP still draws them. Clip those
   in clip space against the near plane and a guard band of GUARD x the view (both inside the window), interpolating
   the model-space vertex, so the result goes through the same matrices as the rest of the mesh. */
enum { GUARD_OUT_MAX = 3 * 6 }; /* 5 planes on a triangle: at most 8 corners, 6 triangles */
#define GUARD 8.0f

typedef struct { float c[4]; Vertex3D v; float n[3]; } ClipVertex; /* c: x y z w after model, view and projection; n: normal (GE-lit) */

/* Outcode bits: 1 in front of near, then x > G w, x < -G w, y > G w, y < -G w. */
static inline float guard_distance(const float c[4], int plane, float near)
{
    switch (plane) {
    case 0: return c[3] - near;
    case 1: return GUARD * c[3] - c[0];
    case 2: return GUARD * c[3] + c[0];
    case 3: return GUARD * c[3] - c[1];
    default: return GUARD * c[3] + c[1];
    }
}

static inline unsigned guard_outcode(const float c[4], float near)
{
    unsigned code = 0;
    for (int plane = 0; plane < 5; plane++)
        if (guard_distance(c, plane, near) < 0)
            code |= 1u << plane;
    return code;
}

static inline unsigned lerp_colour(unsigned a, unsigned b, float t)
{
    unsigned out = 0;
    for (int shift = 0; shift < 32; shift += 8) {
        float ca = a >> shift & 255, cb = b >> shift & 255, c = ca + (cb - ca) * t + 0.5f;
        out |= (c <= 0 ? 0u : c >= 255 ? 255u : (unsigned)c) << shift;
    }
    return out;
}

/* From the inside corner to the outside one, so two triangles sharing the edge get the same point. */
static void guard_cut(const ClipVertex *in, const ClipVertex *out, float din, float dout, ClipVertex *r)
{
    float t = din / (din - dout);
    for (int k = 0; k < 4; k++)
        r->c[k] = in->c[k] + (out->c[k] - in->c[k]) * t;
    r->v.u = in->v.u + (out->v.u - in->v.u) * t;
    r->v.v = in->v.v + (out->v.v - in->v.v) * t;
    r->v.colour = lerp_colour(in->v.colour, out->v.colour, t);
    r->v.x = in->v.x + (out->v.x - in->v.x) * t;
    r->v.y = in->v.y + (out->v.y - in->v.y) * t;
    r->v.z = in->v.z + (out->v.z - in->v.z) * t;
    for (int k = 0; k < 3; k++)
        r->n[k] = in->n[k] + (out->n[k] - in->n[k]) * t;
}

/* Writes up to GUARD_OUT_MAX vertices (a triangle list) to out, and their normals to normal unless it is NULL;
   returns how many. planes: the OR of the corners' outcodes. A plane no corner is beyond cannot cut the polygon (it
   is convex, its new corners lie on its edges), so it is skipped: the same output, most triangles cross one plane. */
static int clip_triangle_guard(const ClipVertex tri[3], float near, Vertex3D *out, float (*normal)[3], unsigned planes)
{
    ClipVertex poly[2][9];
    int n = 3, from = 0;
    for (int i = 0; i < 3; i++)
        poly[0][i] = tri[i];
    for (int plane = 0; plane < 5 && n >= 3; plane++) {
        if (!(planes >> plane & 1))
            continue;
        const ClipVertex *src = poly[from];
        ClipVertex *dst = poly[from ^ 1];
        int m = 0;
        for (int i = 0; i < n; i++) {
            const ClipVertex *a = &src[i], *b = &src[(i + 1) % n];
            float da = guard_distance(a->c, plane, near), db = guard_distance(b->c, plane, near);
            if (da >= 0)
                dst[m++] = *a;
            if ((da >= 0) != (db >= 0)) {
                if (da >= 0)
                    guard_cut(a, b, da, db, &dst[m++]);
                else
                    guard_cut(b, a, db, da, &dst[m++]);
            }
        }
        n = m;
        from ^= 1;
    }
    int count = 0;
    for (int i = 1; i + 1 < n; i++) {
        const int corner[3] = { 0, i, i + 1 };
        for (int k = 0; k < 3; k++, count++) {
            out[count] = poly[from][corner[k]].v;
            if (normal)
                for (int a = 0; a < 3; a++)
                    normal[count][a] = poly[from][corner[k]].n[a];
        }
    }
    return count;
}

/* clip_triangle_guard without struct copies: corners live in a pool and the polygon is a list of pointers; colour is
   kept as four floats, rounded at every cut exactly as lerp_colour does, so the output is the same bytes (b102 checks
   every 64th triangle against clip_triangle_guard on the device). b101 station: 5.2 k cycles a clipped triangle. */
typedef struct { float c[4], u, v, x, y, z, col[4], n[3]; } GuardFastVertex;

static inline float guard_fast_distance(const float *c, int plane, float near)
{
    switch (plane) {
    case 0: return c[3] - near;
    case 1: return GUARD * c[3] - c[0];
    case 2: return GUARD * c[3] + c[0];
    case 3: return GUARD * c[3] - c[1];
    default: return GUARD * c[3] + c[1];
    }
}

static inline void guard_fast_cut(const GuardFastVertex *in, const GuardFastVertex *out, float din, float dout, GuardFastVertex *r)
{
    float t = din / (din - dout);
    for (int k = 0; k < 4; k++)
        r->c[k] = in->c[k] + (out->c[k] - in->c[k]) * t;
    r->u = in->u + (out->u - in->u) * t;
    r->v = in->v + (out->v - in->v) * t;
    for (int k = 0; k < 4; k++) {
        float c = in->col[k] + (out->col[k] - in->col[k]) * t + 0.5f;
        r->col[k] = (float)(c <= 0 ? 0u : c >= 255 ? 255u : (unsigned)c);
    }
    r->x = in->x + (out->x - in->x) * t;
    r->y = in->y + (out->y - in->y) * t;
    r->z = in->z + (out->z - in->z) * t;
    for (int k = 0; k < 3; k++)
        r->n[k] = in->n[k] + (out->n[k] - in->n[k]) * t;
}

static int clip_triangle_guard_fast(const ClipVertex tri[3], float near, Vertex3D *out, float (*normal)[3], unsigned planes)
{
    GuardFastVertex pool[3 + 2 * 5];
    const GuardFastVertex *poly[2][9];
    int used = 3, n = 3, from = 0;
    for (int i = 0; i < 3; i++) {
        GuardFastVertex *p = &pool[i];
        for (int k = 0; k < 4; k++) {
            p->c[k] = tri[i].c[k];
            p->col[k] = (float)(tri[i].v.colour >> (8 * k) & 255);
        }
        p->u = tri[i].v.u, p->v = tri[i].v.v, p->x = tri[i].v.x, p->y = tri[i].v.y, p->z = tri[i].v.z;
        p->n[0] = tri[i].n[0], p->n[1] = tri[i].n[1], p->n[2] = tri[i].n[2];
        poly[0][i] = p;
    }
    for (int plane = 0; plane < 5 && n >= 3; plane++) {
        if (!(planes >> plane & 1))
            continue;
        const GuardFastVertex *const *src = poly[from];
        const GuardFastVertex **dst = poly[from ^ 1];
        float d[9];
        for (int i = 0; i < n; i++)
            d[i] = guard_fast_distance(src[i]->c, plane, near);
        int m = 0;
        for (int i = 0; i < n; i++) {
            int j = i + 1 == n ? 0 : i + 1;
            float da = d[i], db = d[j];
            if (da >= 0)
                dst[m++] = src[i];
            if ((da >= 0) != (db >= 0) && used < 3 + 2 * 5) {
                GuardFastVertex *r = &pool[used++];
                if (da >= 0)
                    guard_fast_cut(src[i], src[j], da, db, r);
                else
                    guard_fast_cut(src[j], src[i], db, da, r);
                dst[m++] = r;
            }
        }
        n = m;
        from ^= 1;
    }
    int count = 0;
    for (int i = 1; i + 1 < n; i++) {
        const GuardFastVertex *corner[3] = { poly[from][0], poly[from][i], poly[from][i + 1] };
        for (int k = 0; k < 3; k++, count++) {
            const GuardFastVertex *p = corner[k];
            out[count].u = p->u, out[count].v = p->v;
            out[count].colour = (unsigned)p->col[0] | (unsigned)p->col[1] << 8 | (unsigned)p->col[2] << 16 | (unsigned)p->col[3] << 24;
            out[count].x = p->x, out[count].y = p->y, out[count].z = p->z;
            if (normal)
                normal[count][0] = p->n[0], normal[count][1] = p->n[1], normal[count][2] = p->n[2];
        }
    }
    return count;
}

#endif
