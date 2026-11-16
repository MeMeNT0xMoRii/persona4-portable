#ifndef P4_UI_QUAD_H
#define P4_UI_QUAD_H
/* Normalized UV and packed colour; the renderer applies its current texture/target scale. */
typedef struct {
    struct {
        struct { float u, v; unsigned colour; struct { float x, y, z; } screen; } els;
    } u;
} P4UiVertex;
extern int p4_ui_font, p4_ui_sprite;
int p4_ui_quad_draw(const P4UiVertex *vertices);
static inline void p4_ui_colour(P4UiVertex *v, unsigned char r, unsigned char g,
                                unsigned char b, unsigned char a)
{
    v->u.els.colour = (unsigned)r | (unsigned)g << 8 | (unsigned)b << 16 | (unsigned)a << 24;
}
static inline void p4_ui_position(P4UiVertex v[4], const void *points)
{
    const float *p = (const float *)points;
    int i;
    for (i = 0; i < 4; i++) {
        v[i].u.els.screen.x = p[i * 2];
        v[i].u.els.screen.y = p[i * 2 + 1];
    }
}
#endif
