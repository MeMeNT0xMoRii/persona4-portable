/* Included after mesh_state. Keep RGB_ONLY glass behind UI, after opaque 3D.
 * Geometry is copied into the current GE list so later CPU scratch/cache use
 * cannot change it. No source asset, alpha value or alpha-test rule is changed. */
enum { LATE_MAX=32, LATE_COPY_BYTES=32*1024 };
typedef struct {
    Matrix model,view;
    ScePspFMatrix4 projection;
    void *camera;
    Raster *raster;
    int render[MAX_STATE];
    unsigned sky[8],mask;
    float depth;
    int prim,vtype,count;
    const void *vertices;
} LateMesh;
static LateMesh *late_meshes;
static unsigned late_count,late_bytes,late_frame;
static unsigned late_queued,late_drawn,late_refused;
static int late_flushing;

static int translucent_defer(int prim,int vtype,int count,const void *indices,const void *vertices,const Material *material,int modulate)
{
    if(late_flushing||!translucent_material(material,modulate)||!in_frame||!current_camera||
       !vertices||count<=0||!render_state[RS_ZTEST]||draw_material_ge||
       (vtype&~GU_INDEX_BITS)!=(VT_3D&~GU_INDEX_BITS)||
       ((vtype&GU_INDEX_BITS)&&((vtype&GU_INDEX_BITS)!=GU_INDEX_16BIT||!indices))||
       prim<GU_TRIANGLES||prim>GU_TRIANGLE_FAN)
        return 0;
    unsigned lo,hi;
    if(!draw_alpha_known||!draw_vertex_alpha(vtype,count,indices,vertices,&lo,&hi))return 0;
    unsigned highest=p4_alpha_product_ceiling(bound_alpha_max,hi);
    if(!p4_alpha_all_fail(sky_state[3],draw_alpha_reference(sky_state[3]),highest))return 0;
    if(late_frame!=cache_frame){late_frame=cache_frame;late_bytes=0;}
    unsigned bytes=(unsigned)count*sizeof(Vertex3D);
    if((unsigned)count>LATE_COPY_BYTES/sizeof(Vertex3D)||late_count>=LATE_MAX||
       bytes>LATE_COPY_BYTES-late_bytes){++late_refused;return 0;}
    if(!late_meshes&&!(late_meshes=calloc(LATE_MAX,sizeof *late_meshes))){++late_refused;return 0;}
    const Matrix *view=(const Matrix *)((const char *)current_camera+CAMERA_VIEW);
    const unsigned short *ix=(vtype&GU_INDEX_BITS)?indices:0;
    /* Stable average view-space depth for back-to-front mesh order. */
    float depth=0;
    for(int i=0;i<count;++i){
        const Vertex3D *v=(const Vertex3D *)vertices+(ix?ix[i]:i);
        const Matrix *m=&translucent_model;
        float x=v->x*m->right.x+v->y*m->up.x+v->z*m->at.x+m->pos.x;
        float y=v->x*m->right.y+v->y*m->up.y+v->z*m->at.y+m->pos.y;
        float z=v->x*m->right.z+v->y*m->up.z+v->z*m->at.z+m->pos.z;
        depth+=x*view->right.z+y*view->up.z+z*view->at.z+view->pos.z;
    }
    depth/=count;
    if(!isfinite(depth)){++late_refused;return 0;}
    Vertex3D *copy=sceGuGetMemory(bytes);
    for(int i=0;i<count;++i)copy[i]=((const Vertex3D *)vertices)[ix?ix[i]:i];
    LateMesh *e=&late_meshes[late_count++];
    e->model=translucent_model;e->view=*view;e->camera=current_camera;
    camera_projection(&e->projection);
    e->raster=material->texture?*(Raster **)material->texture:0;
    memcpy(e->render,render_state,sizeof e->render);memcpy(e->sky,sky_state,sizeof e->sky);
    e->mask=pixel_mask;e->depth=depth;e->prim=prim;e->vtype=vtype&~GU_INDEX_BITS;
    e->count=count;e->vertices=copy;late_bytes+=bytes;++late_queued;
    return 1;
}
static void translucent_flush(void)
{
#ifdef TP_BOUNDARY
    TP_BOUNDARY(); /* rwsky.c: a pending instance batch (far_lod.h) is drawn first */
#endif
    if(!late_count||late_flushing)return;
    if(!in_frame){p4_log("late RGB pass lost frame: %u draws\n",late_count);late_count=0;return;}
    late_flushing=1;
    unsigned order[LATE_MAX];
    for(unsigned i=0;i<late_count;++i){
        unsigned j=i;
        while(j&&late_meshes[order[j-1]].depth<late_meshes[i].depth){order[j]=order[j-1];--j;}
        order[j]=i;
    }
    int saved_render[MAX_STATE];unsigned saved_sky[8],saved_mask=pixel_mask;
    memcpy(saved_render,render_state,sizeof saved_render);memcpy(saved_sky,sky_state,sizeof saved_sky);
    void *saved_camera=current_camera;Matrix saved_model=translucent_model;
    for(unsigned i=0;i<late_count;++i){
        const LateMesh *e=&late_meshes[order[i]];
        current_camera=e->camera;pixel_mask=e->mask;
        memcpy(render_state,e->render,sizeof e->render);memcpy(sky_state,e->sky,sizeof e->sky);
    sl_state_dirty = 1; /* restoring a saved pass changes the key inputs */
        gu_dirty();mesh_state(&e->model,0);
        set_matrix(GU_VIEW,&e->view);sceGuSetMatrix(GU_PROJECTION,&e->projection);
        st_enable(GU_LIGHTING,0);
        bind_mask(255);bind_raster(e->raster,GU_REPEAT);
        /* Every fragment was proved to fail ATEST with AFAIL=RGB_ONLY.
         * Preserve destination alpha and depth, but keep depth testing/blending. */
        st_alpha_func(GU_ALWAYS,0);st_depth_mask(GU_TRUE);st_pixel_mask(e->mask|0xff000000u);
        gu_draw(e->prim,e->vtype,e->count,0,e->vertices);++late_drawn;
    }
    late_count=0;
    current_camera=saved_camera;pixel_mask=saved_mask;
    memcpy(render_state,saved_render,sizeof saved_render);memcpy(sky_state,saved_sky,sizeof saved_sky);
    sl_state_dirty = 1; /* restoring a saved pass changes the key inputs */
    gu_dirty();mesh_state(&saved_model,0);st_pixel_mask(saved_mask);gu_dirty();
    late_flushing=0;
}
static void translucent_report(void)
{
    p4_log("late RGB pass %d: queued %u drawn %u refused %u per 256 frames\n",
        p4_translucent_late,late_queued,late_drawn,late_refused);
    late_queued=late_drawn=late_refused=0;
}
