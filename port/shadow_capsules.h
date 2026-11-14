/* b197: per-bone volumes fitted to influenced bind vertices; one union per clump,
 * then one depth-tested planar quad. Never borrows the scene's stencil/depth as scratch.
 * Capsule endpoints come from the current body pose; no full-model vertex skinning. */
typedef struct { V3 a, b; float radius, score; } ShadowCapsule;
typedef struct { unsigned colour; float x, y, z; } CapsuleMaskVertex;
typedef struct { float u, v; unsigned colour; float x, y, z; } CapsuleQuadVertex;

static void capsule_report(unsigned frames)
{
    p4_log("capsule shadow %d: draws %u segments %u fallback %u rigid %u per %u frames\n",
           p4_shadow_capsule, capsule_draws, capsule_segments, capsule_fallbacks, capsule_rigid, frames);
    capsule_draws = capsule_segments = capsule_fallbacks = capsule_rigid = 0;
}

static RttSlot *capsule_mask_slot(void)
{
    static Raster mask={.width=128,.height=128,.depth=32,.cType=TYPE_CAMERA_TEXTURE};
    return rtt_get(&mask);
}

/* Clip the textured receiver itself; a partly offscreen shadow must not vanish wholesale. */
static float capsule_plane(const CapsuleQuadVertex *v, Mat4 vp, int plane, float near)
{
    float c[4];
    for(int j=0;j<4;j++)c[j]=v->x*vp[0][j]+v->y*vp[1][j]+v->z*vp[2][j]+vp[3][j];
    switch(plane) {
    case 0:return c[3]-near;
    case 1:return GUARD*c[3]+c[0];
    case 2:return GUARD*c[3]-c[0];
    case 3:return GUARD*c[3]+c[1];
    default:return GUARD*c[3]-c[1];
    }
}

static int capsule_receiver(CapsuleQuadVertex out[16], const V3 corners[4], unsigned alpha)
{
    CapsuleQuadVertex a[16],b[16];int count=4;
    static const int order[4]={0,1,3,2};
    for(int i=0;i<4;i++) {
        int j=order[i];V3 p=corners[j];
        a[i]=(CapsuleQuadVertex){j&1?1:0,j&2?1:0,alpha<<24,p.x,p.y,p.z};
    }
    Mat4 vp;guard_matrix(vp,0);
    float near=*(float *)((char *)current_camera+CAMERA_NEAR)*1.01f;
    for(int plane=0;plane<5 && count;plane++) {
        int used=0;CapsuleQuadVertex prev=a[count-1];float d0=capsule_plane(&prev,vp,plane,near);
        for(int i=0;i<count;i++) {
            CapsuleQuadVertex next=a[i];float d1=capsule_plane(&next,vp,plane,near);
            if((d0>=0)!=(d1>=0)) {
                float t=d0/(d0-d1);
                if(used>=16)return 0;
                b[used++]=(CapsuleQuadVertex){prev.u+(next.u-prev.u)*t,prev.v+(next.v-prev.v)*t,alpha<<24,
                    prev.x+(next.x-prev.x)*t,prev.y+(next.y-prev.y)*t,prev.z+(next.z-prev.z)*t};
            }
            if(d1>=0) {if(used>=16)return 0;b[used++]=next;}
            prev=next;d0=d1;
        }
        count=used;memcpy(a,b,count*sizeof *a);
    }
    memcpy(out,a,count*sizeof *a);return count;
}

static int capsule_render(ShadowCapsule *cap, int count, float floor_y)
{
    float x0 = 1e30f, z0 = 1e30f, x1 = -1e30f, z1 = -1e30f;
    for (int i = 0; i < count; i++) {
        const V3 *p[2] = {&cap[i].a, &cap[i].b};
        for (int j = 0; j < 2; j++) {
            if (p[j]->x - cap[i].radius < x0) x0 = p[j]->x - cap[i].radius;
            if (p[j]->x + cap[i].radius > x1) x1 = p[j]->x + cap[i].radius;
            if (p[j]->z - cap[i].radius < z0) z0 = p[j]->z - cap[i].radius;
            if (p[j]->z + cap[i].radius > z1) z1 = p[j]->z + cap[i].radius;
        }
    }
    /* Leave a transparent border for bilinear filtering. */
    float padx = (x1 - x0) * .025f, padz = (z1 - z0) * .025f;
    x0 -= padx; x1 += padx; z0 -= padz; z1 += padz;
    float y = floor_y;
    V3 corners[4] = {{x0,y,z0},{x1,y,z0},{x0,y,z1},{x1,y,z1}};
    CapsuleQuadVertex receiver[16];
    int receiver_count=capsule_receiver(receiver,corners,capsule_alpha);
    if(receiver_count<3)return 1; /* entirely outside the view, not a failed proxy */
    RttSlot *slot = capsule_mask_slot();
    if (!slot) return 0;
    unsigned alpha = capsule_alpha;
    if (!alpha) return 1;
    begin_frame(); gu_dirty();
    static const int states[] = {GU_DEPTH_TEST,GU_BLEND,GU_ALPHA_TEST,GU_STENCIL_TEST,GU_FOG,GU_CULL_FACE,GU_LIGHTING,GU_TEXTURE_2D};
    int was[8];
    for (int i = 0; i < 8; i++) {was[i]=sceGuGetStatus(states[i]);sceGuDisable(states[i]);}
    sceGuTexSync();
    rtt_bind_target(slot);
    sceGuPixelMask(0);
    sceGuDepthMask(GU_TRUE); /* mask rendering must never touch scene depth */
    sceGuClearColor(0); sceGuClearStencil(0);
    sceGuClear(GU_COLOR_BUFFER_BIT | GU_STENCIL_BUFFER_BIT);
    /* GE framebuffer alpha is stencil: explicitly stamp 255 for the opaque union. */
    sceGuEnable(GU_STENCIL_TEST);
    sceGuStencilFunc(GU_ALWAYS, 255, 255);
    sceGuStencilOp(GU_KEEP, GU_KEEP, GU_REPLACE);
    static const float circle[16][2] = {
        {1,0},{.923880f,.382683f},{.707107f,.707107f},{.382683f,.923880f},
        {0,1},{-.382683f,.923880f},{-.707107f,.707107f},{-.923880f,.382683f},
        {-1,0},{-.923880f,-.382683f},{-.707107f,-.707107f},{-.382683f,-.923880f},
        {0,-1},{.382683f,-.923880f},{.707107f,-.707107f},{.923880f,-.382683f}};
    float sx = slot->w / (x1-x0), sz = slot->h / (z1-z0);
    for (int i = 0; i < count; i++) {
        ShadowCapsule *c = &cap[i];
        CapsuleMaskVertex *v = sceGuGetMemory(18 * sizeof *v);
        v[0] = (CapsuleMaskVertex){0xffffffff,((c->a.x+c->b.x)*.5f-x0)*sx,((c->a.z+c->b.z)*.5f-z0)*sz,0};
        for (int j = 0; j < 16; j++) {
            float dx=circle[j][0], dz=circle[j][1];
            const V3 *p = dx*(c->b.x-c->a.x)+dz*(c->b.z-c->a.z)>=0 ? &c->b : &c->a;
            v[j+1]=(CapsuleMaskVertex){0xffffffff,(p->x+dx*c->radius-x0)*sx,(p->z+dz*c->radius-z0)*sz,0};
        }
        v[17]=v[1];
        sceGuDrawArray(GU_TRIANGLE_FAN,GU_COLOR_8888|GU_VERTEX_32BITF|GU_TRANSFORM_2D,18,0,v);
    }
    rtt_bind_target(0); sceGuTexSync(); sceGuTexFlush();
    /* Project the mask back as world geometry: the scene depth still occludes it. */
    sceGuDisable(GU_STENCIL_TEST);
    set_camera();
    ScePspFMatrix4 identity={{1,0,0,0},{0,1,0,0},{0,0,1,0},{0,0,0,1}};
    sceGuSetMatrix(GU_MODEL,&identity);
    sceGuEnable(GU_TEXTURE_2D); sceGuEnable(GU_DEPTH_TEST); sceGuDepthFunc(GU_GEQUAL); sceGuDepthMask(GU_TRUE);
    sceGuEnable(GU_BLEND); sceGuBlendFunc(GU_ADD,GU_SRC_ALPHA,GU_ONE_MINUS_SRC_ALPHA,0,0);
    sceGuPixelMask(0xff000000); /* preserve the game's destination-alpha/stencil */
    sceGuTexMode(GU_PSM_8888,0,0,0);
    sceGuTexImage(0,slot->w,slot->h,slot->w,(void *)(0x04000000u+slot->offset));
    sceGuTexFunc(GU_TFX_MODULATE,GU_TCC_RGBA);sceGuTexFilter(GU_LINEAR,GU_LINEAR);sceGuTexWrap(GU_CLAMP,GU_CLAMP);
    sceGuTexScale(1,1);sceGuTexOffset(0,0);
    CapsuleQuadVertex *v=sceGuGetMemory(receiver_count*sizeof *v);
    memcpy(v,receiver,receiver_count*sizeof *v);
    sceGuDrawArray(GU_TRIANGLE_FAN,GU_TEXTURE_32BITF|GU_COLOR_8888|GU_VERTEX_32BITF|GU_TRANSFORM_3D,receiver_count,0,v);
    sceGuDepthMask(render_state[RS_ZWRITE]?GU_FALSE:GU_TRUE);
    for(int i=0;i<8;i++) {if(was[i])sceGuEnable(states[i]);else sceGuDisable(states[i]);}
    gu_dirty();
    draws_since_stall += count+1; stall_flush();
    capsule_draws++; capsule_segments+=count;
    return 1;
}

/* Stable volume proxies fitted to the geometry influenced by each bone. No controller links. */
enum { CAPSULE_GROUP_MAX = 512, CAPSULE_RIGS = 64 };
typedef struct { V3 a,b; float radius; unsigned short bone; } CapsulePiece;
typedef struct {
    const Geometry *geometry;
    const Skin *skin;
    const Matrix *bind;
    const V3 *verts;
    int nodes, vertices, count;
    unsigned serial, sample;
    CapsulePiece *piece;
} CapsuleRig;
static CapsuleRig *capsule_rigs;
static unsigned capsule_rig_next;
static ShadowCapsule *capsule_group;
static int capsule_group_count, capsule_group_on;
static float capsule_floor;

static void capsule_begin(void)
{
    capsule_group_count = 0;
    capsule_floor = 1e30f;
    capsule_group_on = 0;
    if (!p4_shadow_capsule || rtt_target) return;
    if (!capsule_group) capsule_group = malloc(CAPSULE_GROUP_MAX * sizeof *capsule_group);
    if (capsule_group && capsule_mask_slot()) capsule_group_on = 1;
}

static int capsule_append(const ShadowCapsule *cap, int count, float floor)
{
    if (!capsule_group_on || capsule_group_count + count > CAPSULE_GROUP_MAX) return 0;
    memcpy(capsule_group + capsule_group_count, cap, count * sizeof *cap);
    capsule_group_count += count;
    if (floor < capsule_floor) capsule_floor = floor;
    return 1;
}

static void capsule_end(void)
{
    if (capsule_group_on && capsule_group_count)
        capsule_render(capsule_group, capsule_group_count, capsule_floor + .05f);
    capsule_group_on = 0;
}

static CapsuleRig *capsule_rig(Geometry *g,const Skin *skin,const Hierarchy *h)
{
    int n=skin->numBones<h->numNodes?skin->numBones:h->numNodes;
    if(n<1 || n>MAX_BONES || !skin->skinToBone || !skin->indices)return 0;
    unsigned sample=hash_sample(2166136261u,g->morphTarget->verts,g->numVertices,sizeof(V3));
    if(!capsule_rigs)capsule_rigs=calloc(CAPSULE_RIGS,sizeof *capsule_rigs);
    if(!capsule_rigs)return 0;
    for(int i=0;i<CAPSULE_RIGS;i++) {
        CapsuleRig *r=&capsule_rigs[i];
        if(r->geometry==g && r->skin==skin && r->bind==skin->skinToBone && r->verts==g->morphTarget->verts &&
           r->vertices==g->numVertices && r->nodes==n && r->serial==g->mesh->serial && r->sample==sample)return r;
    }
    CapsuleRig *r=&capsule_rigs[capsule_rig_next++%CAPSULE_RIGS];
    free(r->piece);memset(r,0,sizeof *r);
    V3 lo[MAX_BONES],hi[MAX_BONES];unsigned count[MAX_BONES]={0};
    for(int j=0;j<n;j++) {lo[j]=(V3){1e30f,1e30f,1e30f};hi[j]=(V3){-1e30f,-1e30f,-1e30f};}
    for(int i=0;i<g->numVertices;i++) {
        unsigned char bone[4];float weight[4];int k=vertex_bones(skin,n,i,bone,weight),main=0;
        for(int w=1;w<k;w++)if(weight[w]>weight[main])main=w;
        for(int w=0;w<k;w++) {
            /* Shared joints overlap rather than opening gaps where blended parts meet. */
            if(w!=main && weight[w]<.25f)continue;
            int j=bone[w];V3 v=transform(&g->morphTarget->verts[i],&skin->skinToBone[j]);
            if(!(fabsf(v.x)<1e6f && fabsf(v.y)<1e6f && fabsf(v.z)<1e6f))continue;
            if(v.x<lo[j].x)lo[j].x=v.x;if(v.y<lo[j].y)lo[j].y=v.y;if(v.z<lo[j].z)lo[j].z=v.z;
            if(v.x>hi[j].x)hi[j].x=v.x;if(v.y>hi[j].y)hi[j].y=v.y;if(v.z>hi[j].z)hi[j].z=v.z;
            count[j]++;
        }
    }
    r->piece=calloc(n,sizeof *r->piece);
    if(!r->piece)return 0;
    for(int j=0;j<n;j++) {
        if(count[j]<2)continue;
        float half[3]={(hi[j].x-lo[j].x)*.5f,(hi[j].y-lo[j].y)*.5f,(hi[j].z-lo[j].z)*.5f};
        int axis=half[1]>half[0]?1:0;if(half[2]>half[axis])axis=2;
        float cross=0;for(int k=0;k<3;k++)if(k!=axis)cross+=half[k]*half[k];
        float radius=sqrtf(cross)*1.12f;
        if(radius<half[axis]*.05f)radius=half[axis]*.05f;
        if(!(radius>1e-5f))continue;
        V3 a={(hi[j].x+lo[j].x)*.5f,(hi[j].y+lo[j].y)*.5f,(hi[j].z+lo[j].z)*.5f},b=a;
        float extent=half[axis]-radius;if(extent<0)extent=0;
        if(axis==0){a.x-=extent;b.x+=extent;}else if(axis==1){a.y-=extent;b.y+=extent;}else{a.z-=extent;b.z+=extent;}
        r->piece[r->count++]=(CapsulePiece){a,b,radius,j};
    }
    if(!r->count) {free(r->piece);r->piece=0;return 0;}
    r->geometry=g;r->skin=skin;r->bind=skin->skinToBone;r->verts=g->morphTarget->verts;
    r->nodes=n;r->vertices=g->numVertices;r->serial=g->mesh->serial;r->sample=sample;
    p4_log("capsule volume g %p h %p vertices %d bones %d pieces %d\n",g,h,g->numVertices,n,r->count);
    return r;
}

static float capsule_scale(const Matrix *m)
{
    float a=m->right.x*m->right.x+m->right.y*m->right.y+m->right.z*m->right.z;
    float b=m->up.x*m->up.x+m->up.y*m->up.y+m->up.z*m->up.z;
    float c=m->at.x*m->at.x+m->at.y*m->at.y+m->at.z*m->at.z;
    return sqrtf(a>b?(a>c?a:c):(b>c?b:c));
}

static int capsule_shadow_body(void *atomic,Geometry *g,char *frame,const Hierarchy *h)
{
    if(!capsule_group_on || !h->matrices || (h->flags&HIERARCHY_NO_MATRICES))return 0;
    const Skin *skin=SKIN_OF_GEOMETRY(g);CapsuleRig *r=capsule_rig(g,skin,h);
    if(!r)return 0;
    const Matrix *model=p4r_003e9700(frame);
    float local_scale=h->flags&HIERARCHY_LOCAL_SPACE?capsule_scale(model):1;
    ShadowCapsule cap[MAX_BONES];int count=0;
    for(int i=0;i<r->count;i++) {
        CapsulePiece *p=&r->piece[i];const Matrix *pose=&h->matrices[p->bone];
        if(!matrix_sane(pose))continue;
        V3 a=transform(&p->a,pose),b=transform(&p->b,pose);
        if(h->flags&HIERARCHY_LOCAL_SPACE){a=transform(&a,model);b=transform(&b,model);}
        a=transform(&a,shadow_shear_m);b=transform(&b,shadow_shear_m);
        float radius=p->radius*capsule_scale(pose)*local_scale;
        cap[count++]=(ShadowCapsule){a,b,radius,0};
    }
    /* Every geometry contributes: a first head/hand atomic must not stand in for the whole clump. */
    return !count || capsule_append(cap,count,model->pos.y*.01f);
}

/* Cached bind-space bounds for rigid attachments (swords, fans, etc.). */
static int capsule_shadow_rigid(Geometry *g, char *frame)
{
    typedef struct { const Geometry *g; const V3 *v; int n; V3 a, b; float r; } Bounds;
    static Bounds cache[32];
    static unsigned next;
    Bounds *e = 0;
    for (int i = 0; i < 32; i++)
        if (cache[i].g == g && cache[i].v == g->morphTarget->verts && cache[i].n == g->numVertices) {e=&cache[i];break;}
    if (!e) {
        V3 lo={1e30f,1e30f,1e30f}, hi={-1e30f,-1e30f,-1e30f};
        for (int i=0;i<g->numVertices;i++) {
            V3 v=g->morphTarget->verts[i];
            if (!(fabsf(v.x)<1e6f && fabsf(v.y)<1e6f && fabsf(v.z)<1e6f)) return 0;
            if(v.x<lo.x)lo.x=v.x;if(v.y<lo.y)lo.y=v.y;if(v.z<lo.z)lo.z=v.z;
            if(v.x>hi.x)hi.x=v.x;if(v.y>hi.y)hi.y=v.y;if(v.z>hi.z)hi.z=v.z;
        }
        float size[3]={hi.x-lo.x,hi.y-lo.y,hi.z-lo.z};
        int axis=size[1]>size[0]?1:0;if(size[2]>size[axis])axis=2;
        float *low=&lo.x,*high=&hi.x;
        V3 a={(lo.x+hi.x)*.5f,(lo.y+hi.y)*.5f,(lo.z+hi.z)*.5f},b=a;
        (&a.x)[axis]=low[axis];(&b.x)[axis]=high[axis];
        float radius=0;
        for(int k=0;k<3;k++)if(k!=axis && size[k]*.5f>radius)radius=size[k]*.5f;
        if(radius<size[axis]*.025f)radius=size[axis]*.025f;
        if(!(radius>0))return 0;
        e=&cache[next++%32];*e=(Bounds){g,g->morphTarget->verts,g->numVertices,a,b,radius};
    }
    const Matrix *model=p4r_003e9700(frame);
    V3 a=transform(&e->a,model),b=transform(&e->b,model);
    a=transform(&a,shadow_shear_m);b=transform(&b,shadow_shear_m);
    float scale=sqrtf(model->right.x*model->right.x+model->right.y*model->right.y+model->right.z*model->right.z);
    ShadowCapsule c={a,b,e->r*scale,0};
    int result=capsule_append(&c,1,model->pos.y*.01f);
    capsule_rigid+=result;
    return result;
}

static int capsule_shadow(void *atomic, Geometry *g, char *frame)
{
    const Skin *skin=SKIN_OF_GEOMETRY(g);
    const Hierarchy *h=HIERARCHY_OF_ATOMIC(atomic);
    if (!capsule_group_on) return 0;
    if (!skin || !h) return capsule_shadow_rigid(g, frame);
    int result=capsule_shadow_body(atomic,g,frame,h);
    if(!result)capsule_fallbacks++;
    return result;
}
