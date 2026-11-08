#ifndef P4_FEEDBACK_H
#define P4_FEEDBACK_H

/* Included after GuVertex. Only uniform, axis-aligned feedback rectangles are
   eligible: sprites use the second vertex's colour/depth and cannot preserve
   arbitrary triangle gradients. Other geometry keeps the triangle path. */
#define P4_FEEDBACK_MAX_VERTICES 30
typedef struct { GuVertex corner[4]; } P4FeedbackRect;

static int p4_feedback_rect(const GuVertex *v, int count, P4FeedbackRect *rect)
{
    if (count != 6)
        return 0;
    float left = v[0].x, right = left, top = v[0].y, bottom = top;
    for (int i = 0; i < 6; i++) {
        /* Ordered comparisons also reject NaNs and infinities. */
        if (!(v[i].x >= 0 && v[i].x <= 480 && v[i].y >= 0 && v[i].y <= 272 &&
              v[i].u >= -8192 && v[i].u <= 8192 && v[i].v >= -8192 && v[i].v <= 8192 &&
              v[i].z >= 0 && v[i].z <= 65535) ||
            v[i].colour != v[0].colour || v[i].z != v[0].z)
            return 0;
        if (v[i].x < left) left = v[i].x;
        if (v[i].x > right) right = v[i].x;
        if (v[i].y < top) top = v[i].y;
        if (v[i].y > bottom) bottom = v[i].y;
    }
    if (!(left < right && top < bottom))
        return 0;
    int seen[4] = {0}, triangle = 0;
    for (int i = 0; i < 6; i++) {
        if ((v[i].x != left && v[i].x != right) || (v[i].y != top && v[i].y != bottom))
            return 0;
        int corner = (v[i].x == right) + 2 * (v[i].y == bottom);
        if (triangle & (1 << corner))
            return 0;
        triangle |= 1 << corner;
        if (i == 2) triangle = 0;
        if (seen[corner] && (rect->corner[corner].u != v[i].u || rect->corner[corner].v != v[i].v))
            return 0;
        rect->corner[corner] = v[i];
        seen[corner]++;
    }
    if (!((seen[0] == 2 && seen[3] == 2 && seen[1] == 1 && seen[2] == 1) ||
          (seen[1] == 2 && seen[2] == 2 && seen[0] == 1 && seen[3] == 1)))
        return 0;
    const GuVertex *c = rect->corner;
    if (c[0].u != c[2].u || c[1].u != c[3].u || c[0].v != c[1].v || c[2].v != c[3].v)
        return 0;
    float a = (v[1].x-v[0].x)*(v[2].y-v[0].y) - (v[1].y-v[0].y)*(v[2].x-v[0].x);
    float b = (v[4].x-v[3].x)*(v[5].y-v[3].y) - (v[4].y-v[3].y)*(v[5].x-v[3].x);
    if (!((a > 0 && b > 0) || (a < 0 && b < 0)))
        return 0;
    return 1;
}

static int p4_feedback_emit(GuVertex *out, const P4FeedbackRect *rect)
{
    const GuVertex *tl = &rect->corner[0], *br = &rect->corner[3];
    float du = (br->u - tl->u) / (br->x - tl->x);
    int count = 0;
    for (float x = tl->x; x < br->x; x += 32) {
        float end = x + 32 < br->x ? x + 32 : br->x;
        GuVertex a = *tl, b = *br;
        a.x = x;
        b.x = end;
        a.u = tl->u + (x - tl->x) * du;
        b.u = end == br->x ? br->u : tl->u + (end - tl->x) * du;
        out[count++] = a;
        out[count++] = b;
    }
    return count;
}

#endif
