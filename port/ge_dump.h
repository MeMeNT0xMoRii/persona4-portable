/* PPSSPP GE frame dump (.ppdmp, format version 6) written on the PSP itself. Hold L + R + SQUARE: the next frame runs
   unpipelined; at its end the GE is idle and its register state becomes the dump's INIT, the frame after that is
   recorded by walking its display list (following CALL/RET/JUMP) and written to <game dir>/ge_NNNN.ppdmp. Open the
   file in PPSSPP like a game; the GE debugger steps through it.
   Follows PPSSPP's GPU/Debugger/Record.cpp, with differences a walk after the fact needs:
   - memory is read when the frame is over, so vertices/textures the CPU rewrote during the frame show their last
     state (the port's per-frame pools are append-only, so this should be rare);
   - a VRAM texture overlapping something this frame drew before using it is marked "unchanged" (flags 1|2|4) and
     carries no pixels: the replay samples what it drew itself. Other VRAM textures carry their end-of-frame pixels;
   - a block transfer from VRAM keeps its registers and reads the replay's own VRAM;
   - BJUMP is taken as not jumping (the draw it guards is recorded), SIGNAL is skipped with its END.
   Sections are zstd frames made of raw blocks: valid zstd, no compressor needed. */
extern char p4_game_dir[160];

enum { GD_INIT = 0, GD_REGISTERS = 1, GD_VERTICES = 2, GD_INDICES = 3, GD_CLUT = 4, GD_TRANSFERSRC = 5,
       GD_DISPLAY = 9, GD_CLUTADDR = 10, GD_TEXTURE0 = 0x10, GD_FRAMEBUF0 = 0x18 };

typedef struct { unsigned ptr, size, arena; const unsigned char *mem; } GdPiece; /* mem 0: bytes in the arena */
typedef struct { const void *src; unsigned size, tag, ptr; } GdSeen;
enum { GD_SEEN = 16384, GD_RANGES = 64, GD_TARGETS = 32 };

static struct {
    int stage, saved_pipeline;
    unsigned init[512], cmd[256], clut[256], clut_bytes;
    unsigned char *cmds; unsigned ncmds, cmds_cap;
    unsigned char *arena; unsigned arena_used, arena_cap;
    GdPiece *pieces; unsigned npieces, pieces_cap;
    unsigned *regs; unsigned nregs, regs_cap;
    GdSeen *seen;
    unsigned bufsz, failed, vaddr, iaddr, offset;
    unsigned drawn_lo[GD_RANGES], drawn_hi[GD_RANGES], ndrawn;
    unsigned targets[GD_TARGETS], ntargets;
    unsigned prims, textures, bytes_mem;
} gd;

static int gd_grow(void *pp, unsigned *cap, unsigned need, unsigned elem)
{
    if (need <= *cap)
        return 1;
    unsigned n = *cap ? *cap : 1024;
    while (n < need)
        n *= 2;
    void *p = realloc(*(void **)pp, n * elem);
    if (!p) {
        gd.failed = 1;
        return 0;
    }
    *(void **)pp = p;
    *cap = n;
    return 1;
}

static int gd_vram(unsigned a) { return a >= 0x04000000 && a < 0x04800000; }
/* what user mode may read: VRAM and its mirrors, user RAM of the 64 MB mode */
static unsigned gd_clamp(unsigned a, unsigned n)
{
    unsigned end = gd_vram(a) ? 0x04800000 : a >= 0x08800000 && a < 0x0C000000 ? 0x0C000000 : 0;
    return !end ? 0 : n > end - a ? end - a : n;
}
static const void *gd_cpu(unsigned a) { return (const void *)(gd_vram(a) ? a | 0x40000000u : a); }

static void gd_command(unsigned type, unsigned size, unsigned ptr)
{
    if (!gd_grow(&gd.cmds, &gd.cmds_cap, (gd.ncmds + 1) * 9, 1))
        return;
    unsigned char *c = gd.cmds + gd.ncmds++ * 9;
    c[0] = type;
    memcpy(c + 1, &size, 4);
    memcpy(c + 5, &ptr, 4);
}
static void gd_piece(unsigned ptr, const void *mem, const void *bytes, unsigned size)
{
    if (!size || !gd_grow(&gd.pieces, &gd.pieces_cap, gd.npieces + 1, sizeof *gd.pieces))
        return;
    GdPiece *p = &gd.pieces[gd.npieces++];
    p->ptr = ptr, p->size = size, p->mem = mem, p->arena = gd.arena_used;
    if (!mem) {
        if (!gd_grow(&gd.arena, &gd.arena_cap, gd.arena_used + size, 1)) {
            gd.npieces--;
            return;
        }
        memcpy(gd.arena + gd.arena_used, bytes, size);
        gd.arena_used += size;
    } else
        gd.bytes_mem += size;
}
static GdSeen *gd_seen(const void *src, unsigned size, unsigned tag)
{
    unsigned h = ((unsigned)src >> 4 ^ size * 2654435761u ^ tag) & (GD_SEEN - 1);
    for (int i = 0; i < 64; i++, h = (h + 1) & (GD_SEEN - 1))
        if (!gd.seen[h].src || (gd.seen[h].src == src && gd.seen[h].size == size && gd.seen[h].tag == tag))
            return &gd.seen[h];
    return 0; /* table crowded: no dedup for this one */
}
static void gd_flush_registers(void)
{
    if (!gd.nregs)
        return;
    unsigned ptr = (gd.bufsz + 3) & ~3u, size = gd.nregs * 4;
    gd_piece(ptr, 0, gd.regs, size);
    gd.bufsz = ptr + size;
    gd_command(GD_REGISTERS, size, ptr);
    gd.nregs = 0;
}
/* a command whose payload is an optional small header followed by memory (deduplicated by address, size, tag) */
static void gd_emit(unsigned type, const void *header, unsigned hsize, const void *mem, unsigned size, unsigned align, unsigned tag)
{
    gd_flush_registers();
    GdSeen *s = mem ? gd_seen(mem, size, tag) : 0;
    if (s && s->src) {
        gd_command(type, hsize + size, s->ptr);
        return;
    }
    unsigned ptr = (gd.bufsz + align - 1) & ~(align - 1);
    gd_piece(ptr, 0, header, hsize);
    gd_piece(ptr + hsize, mem, 0, size);
    gd.bufsz = ptr + hsize + size;
    if (s)
        s->src = mem, s->size = size, s->tag = tag, s->ptr = ptr;
    gd_command(type, hsize + size, ptr);
}
static void gd_push(unsigned op)
{
    if (gd_grow(&gd.regs, &gd.regs_cap, gd.nregs + 1, 4))
        gd.regs[gd.nregs++] = op;
}

/* VRAM bookkeeping, offsets within the 2 MB */
static void gd_drawn(unsigned a, unsigned bytes)
{
    a &= 0x1fffff;
    for (unsigned i = 0; i < gd.ndrawn; i++)
        if (gd.drawn_lo[i] == a) {
            if (a + bytes > gd.drawn_hi[i])
                gd.drawn_hi[i] = a + bytes;
            return;
        }
    if (gd.ndrawn < GD_RANGES)
        gd.drawn_lo[gd.ndrawn] = a, gd.drawn_hi[gd.ndrawn++] = a + bytes;
}
static int gd_was_drawn(unsigned a, unsigned bytes)
{
    a &= 0x1fffff;
    for (unsigned i = 0; i < gd.ndrawn; i++)
        if (a < gd.drawn_hi[i] && gd.drawn_lo[i] < a + bytes)
            return 1;
    return 0;
}
static void gd_target(unsigned a)
{
    a = 0x04000000 | (a & 0x1fffff);
    for (unsigned i = 0; i < gd.ntargets; i++)
        if (gd.targets[i] == a)
            return;
    if (gd.ntargets < GD_TARGETS)
        gd.targets[gd.ntargets++] = a;
}
static int gd_is_target(unsigned a)
{
    a = 0x04000000 | (a & 0x1fffff);
    for (unsigned i = 0; i < gd.ntargets; i++)
        if (gd.targets[i] == a)
            return 1;
    return 0;
}

static unsigned gd_relative(unsigned data) { return (((gd.cmd[0x10] & 0x000f0000) << 8 | (data & 0xffffff)) + gd.offset) & 0x0fffffff; }

static unsigned gd_vertex_size(unsigned vt)
{
    static const unsigned char comp[4] = {0, 1, 2, 4}, colour[8] = {0, 0, 0, 0, 2, 2, 2, 4};
    unsigned at = 0, align = 1;
    const unsigned elem[5] = {comp[vt >> 9 & 3], comp[vt & 3], colour[vt >> 2 & 7], comp[vt >> 5 & 3], comp[vt >> 7 & 3]};
    const unsigned count[5] = {(vt >> 14 & 7) + 1, 2, 1, 3, 3};
    for (int i = 0; i < 5; i++)
        if (elem[i]) {
            at = (at + elem[i] - 1) & ~(elem[i] - 1);
            at += elem[i] * count[i];
            if (elem[i] > align)
                align = elem[i];
        }
    return ((at + align - 1) & ~(align - 1)) * ((vt >> 18 & 7) + 1);
}

static void gd_texture(int level, unsigned a)
{
    static const unsigned char bits[16] = {16, 16, 16, 32, 4, 8, 16, 32, 4, 8, 8, 0, 0, 0, 0, 0};
    unsigned format = gd.cmd[0xC3] & 0xf, size = gd.cmd[0xB8 + level];
    unsigned w = 1u << (size & 0xf), h = 1u << (size >> 8 & 0xf), bufw = gd.cmd[0xA8 + level] & 0x7ff;
    if (w > 512) w = 512;
    if (h > 512) h = 512;
    unsigned extraw = w > bufw ? w - bufw : 0;
    unsigned bytes = gd_clamp(a, bits[format] * (bufw * h + extraw) / 8);
    if (!bytes)
        return;
    gd.textures++;
    if (!gd_vram(a)) {
        gd_emit(GD_TEXTURE0 + level, 0, 0, gd_cpu(a), bytes, 16, 0);
        return;
    }
    unsigned flags = gd_is_target(a);
    if (gd_was_drawn(a, bytes))
        flags |= 2 | 4; /* drawn earlier in this frame: the replay has it already */
    unsigned header[4] = {a, bufw, flags, 0};
    gd_emit(GD_FRAMEBUF0 + level, header, 16, flags & 2 ? 0 : gd_cpu(a), flags & 2 ? 0 : bytes, 16, 0x80000000u | flags | bufw << 8);
}

static void gd_prim(unsigned op, unsigned count)
{
    unsigned fb = (gd.cmd[0x9C] & 0xfffff0) | (gd.cmd[0x9D] & 0xff0000) << 8, stride = gd.cmd[0x9D] & 0x7fc;
    unsigned zb = (gd.cmd[0x9E] & 0xfffff0) | (gd.cmd[0x9F] & 0xff0000) << 8, zstride = gd.cmd[0x9F] & 0x7fc;
    gd_target(fb);
    gd_target(zb);
    unsigned vt = gd.cmd[0x12];
    if ((gd.cmd[0x1E] & 1) || (gd.cmd[0x25] & 1) || (vt & 3))
        for (int level = 0; level < 8; level++) {
            unsigned a = (gd.cmd[0xA0 + level] & 0xfffff0) | (gd.cmd[0xA8 + level] << 8 & 0x0f000000);
            if (a)
                gd_texture(level, a);
        }
    unsigned vsize = gd_vertex_size(vt), itype = vt >> 11 & 3, isize = itype == 3 ? 4 : itype;
    unsigned vcount = count;
    if (itype && gd_clamp(gd.iaddr, count * isize) == count * isize) {
        unsigned top = 0;
        const unsigned char *ix = gd_cpu(gd.iaddr);
        for (unsigned i = 0; i < count; i++) {
            unsigned v = isize == 1 ? ix[i] : isize == 2 ? ((const unsigned short *)ix)[i] : ((const unsigned *)ix)[i];
            if (v > top)
                top = v;
        }
        vcount = count ? top + 1 : 0;
        gd_emit(GD_INDICES, 0, 0, ix, count * isize, 4, 0);
    }
    unsigned vbytes = gd_clamp(gd.vaddr, vcount * vsize);
    if (vbytes)
        gd_emit(GD_VERTICES, 0, 0, gd_cpu(gd.vaddr), vbytes, 4, 0);
    gd_push(op);
    gd.prims++;
    /* what this draw wrote */
    unsigned sy = gd.cmd[0xD5] >> 10 & 0x3ff, ry = gd.cmd[0x16] >> 10 & 0x3ff, lines = (sy < ry ? sy : ry) + 1;
    int clear = gd.cmd[0xD3] & 1;
    if ((clear && (gd.cmd[0xD3] & 0x400)) || (!clear && (gd.cmd[0x23] & 1) && !(gd.cmd[0xE7] & 1)))
        gd_drawn(zb, 2 * zstride * lines);
    gd_drawn(fb, ((gd.cmd[0xD2] & 3) == 3 ? 4 : 2) * stride * lines);
    if ((op >> 24) == 0x04) { /* the GE moves its pointers past what it read */
        if (itype)
            gd.iaddr += count * isize;
        else
            gd.vaddr += count * vsize;
    }
}

static void gd_load_clut(unsigned op)
{
    unsigned a = (gd.cmd[0xB0] & 0xfffff0) | (gd.cmd[0xB1] << 8 & 0x0f000000);
    unsigned blocks = (op & 0x7f) == 0x40 ? 0x40 : op & 0x3f, bytes = gd_clamp(a, blocks * 32);
    if (bytes) {
        if (gd_vram(a)) {
            unsigned data[2] = {a, gd_is_target(a) | (gd_was_drawn(a, bytes) ? 4 : 0)};
            gd_flush_registers();
            unsigned ptr = (gd.bufsz + 3) & ~3u;
            gd_piece(ptr, 0, data, 8);
            gd.bufsz = ptr + 8;
            gd_command(GD_CLUTADDR, 8, ptr);
        }
        gd_emit(GD_CLUT, 0, 0, gd_cpu(a), bytes, 16, 0);
    }
    gd_push(op);
}

static void gd_transfer(unsigned op)
{
    gd_flush_registers();
    unsigned dst = (gd.cmd[0xB4] & 0xfffff0) | (gd.cmd[0xB5] & 0xff0000) << 8;
    if (!gd_vram(dst))
        return; /* PPSSPP's recorder drops these too */
    unsigned src = (gd.cmd[0xB2] & 0xfffff0) | (gd.cmd[0xB3] & 0xff0000) << 8;
    unsigned sstride = gd.cmd[0xB3] & 0x7f8, dstride = gd.cmd[0xB5] & 0x7f8, bpp = op & 1 ? 4 : 2;
    unsigned sx = gd.cmd[0xEB] & 0x3ff, sy = gd.cmd[0xEB] >> 10 & 0x3ff, dx = gd.cmd[0xEC] & 0x3ff, dy = gd.cmd[0xEC] >> 10 & 0x3ff;
    unsigned w = (gd.cmd[0xEE] & 0x3ff) + 1, h = (gd.cmd[0xEE] >> 10 & 0x3ff) + 1;
    unsigned sbytes = gd_clamp(src, ((sy + h - 1) * sstride + sx + w) * bpp);
    if (sbytes && !gd_vram(src)) /* from VRAM: the replay reads its own */
        gd_emit(GD_TRANSFERSRC, 0, 0, gd_cpu(src), sbytes, 16, 0);
    gd_drawn(dst, ((dy + h - 1) * dstride + dx + w) * bpp);
    gd_push(op);
}

static void gd_walk(unsigned pc, unsigned end)
{
    unsigned stack[8], depth = 0;
    for (unsigned n = 0; n < (4u << 20) && !gd.failed; n++) {
        if ((!depth && pc >= end) || gd_clamp(pc, 4) < 4)
            return;
        unsigned op = *(const unsigned *)gd_cpu(pc), cmd = op >> 24, next = pc + 4;
        switch (cmd) {
        case 0x01: gd.vaddr = gd_relative(op); break;
        case 0x02: gd.iaddr = gd_relative(op); break;
        case 0x04: gd_prim(op, op & 0xffff); break;
        case 0x05: case 0x06: gd_prim(op, (op & 0xff) * (op >> 8 & 0xff)); break;
        case 0x07: case 0x09: case 0x0F: break; /* BOUNDINGBOX, BJUMP (not taken), FINISH */
        case 0x08: next = gd_relative(op) & ~3u; break;
        case 0x0A:
            if (depth < 8) {
                stack[depth++] = next;
                next = gd_relative(op) & ~3u;
            }
            break;
        case 0x0B:
            if (!depth)
                return;
            next = stack[--depth];
            break;
        case 0x0C: return;
        case 0x0E: next += 4; break; /* SIGNAL and its END */
        case 0x13: gd.offset = op << 8; break;
        case 0x14: gd.offset = pc; break;
        case 0xC4: gd_load_clut(op); break;
        case 0xEA: gd_transfer(op); break;
        default:
            if (cmd != 0x10)
                gd_push(op);
            break;
        }
        gd.cmd[cmd] = op;
        pc = next;
    }
}

/* GE state at the start of the dumped frame, in the sceGeSaveContext layout PPSSPP's GEState::Restore reads */
static void gd_capture_init(void)
{
    static const unsigned char ranges[][2] = {{0x00, 0x02}, {0x10, 0x10}, {0x12, 0x28}, {0x2c, 0x33}, {0x36, 0x38},
        {0x42, 0x4D}, {0x50, 0x51}, {0x53, 0x58}, {0x5B, 0xB5}, {0xB8, 0xC3}, {0xC5, 0xD0}, {0xD2, 0xE9}, {0xEB, 0xEC},
        {0xEE, 0xEE}, {0xF0, 0xF6}, {0xF8, 0xF9}};
    memset(gd.init, 0, sizeof gd.init);
    gd.offset = 0;
    for (int c = 0; c < 256; c++)
        gd.cmd[c] = (unsigned)c << 24 | (sceGeGetCmd(c) & 0xffffff);
    unsigned *w = gd.init + 17;
    for (unsigned r = 0; r < sizeof ranges / sizeof ranges[0]; r++)
        for (int c = ranges[r][0]; c <= ranges[r][1]; c++)
            *w++ = gd.cmd[c];
    gd.init[5] = gd_relative(gd.cmd[0x01]);
    gd.init[6] = gd_relative(gd.cmd[0x02]);
    static const struct { int type, count; unsigned char num, data; } matrices[] = {
        {PSP_GE_MATRIX_BONE0, 96, 0x2A, 0x2B}, {PSP_GE_MATRIX_WORLD, 12, 0x3A, 0x3B}, {PSP_GE_MATRIX_VIEW, 12, 0x3C, 0x3D},
        {PSP_GE_MATRIX_PROJECTION, 16, 0x3E, 0x3F}, {PSP_GE_MATRIX_TEXGEN, 12, 0x40, 0x41}};
    for (int m = 0; m < 5; m++) {
        unsigned v[96];
        *w++ = (unsigned)matrices[m].num << 24;
        if (matrices[m].type == PSP_GE_MATRIX_BONE0)
            for (int b = 0; b < 8; b++)
                sceGeGetMtx(PSP_GE_MATRIX_BONE0 + b, v + 12 * b);
        else
            sceGeGetMtx(matrices[m].type, v);
        /* the firmware may hand back floats or the GE's 24-bit values: floats have a top byte */
        int floats = 0;
        for (int i = 0; i < matrices[m].count; i++)
            floats |= v[i] >> 24 != 0;
        for (int i = 0; i < matrices[m].count; i++)
            *w++ = (unsigned)matrices[m].data << 24 | ((floats ? v[i] >> 8 : v[i]) & 0xffffff);
    }
    for (int m = 0; m < 5; m++)
        *w++ = (unsigned)matrices[m].num << 24;
    *w++ = 0x0Cu << 24; /* END: marks the new layout */
    /* the loaded CLUT, which a draw may use without loading it again */
    unsigned a = (gd.cmd[0xB0] & 0xfffff0) | (gd.cmd[0xB1] << 8 & 0x0f000000);
    gd.clut_bytes = gd_clamp(a, 1024) == 1024 ? 1024 : 0;
    if (gd.clut_bytes)
        memcpy(gd.clut, gd_cpu(a), 1024);
}

/* output: a buffered file writer and a zstd frame of raw blocks around each section */
static struct { SceUID fd; unsigned char *buf; unsigned used, block_left, total_left, error; } gw;
static void gw_out(const void *p, unsigned n)
{
    while (n && !gw.error) {
        unsigned k = 65536 - gw.used < n ? 65536 - gw.used : n;
        memcpy(gw.buf + gw.used, p, k);
        gw.used += k, p = (const char *)p + k, n -= k;
        if (gw.used == 65536) {
            if (sceIoWrite(gw.fd, gw.buf, gw.used) != (int)gw.used)
                gw.error = 1;
            gw.used = 0;
        }
    }
}
static void gw_u32(unsigned v) { gw_out(&v, 4); }
static void gw_block_header(void)
{
    unsigned n = gw.total_left < 131072 ? gw.total_left : 131072, h = n << 3 | (n == gw.total_left);
    gw_out(&h, 3);
    gw.block_left = n;
}
static void gw_zstd_begin(unsigned total)
{
    unsigned blocks = total ? (total + 131071) / 131072 : 1;
    gw_u32(4 + 1 + 4 + 3 * blocks + total); /* PPSSPP's compressed size word */
    gw_u32(0xFD2FB528);
    unsigned char descriptor = 0xA0; /* 4-byte content size, single segment, no checksum */
    gw_out(&descriptor, 1);
    gw_u32(total);
    gw.total_left = total;
    gw_block_header();
}
static void gw_zstd(const void *p, unsigned n)
{
    while (n) {
        if (!gw.block_left)
            gw_block_header();
        unsigned k = n < gw.block_left ? n : gw.block_left;
        gw_out(p, k);
        p = (const char *)p + k, n -= k, gw.block_left -= k, gw.total_left -= k;
    }
}

static void gd_write(unsigned display)
{
    char path[200];
    SceIoStat st;
    int number = 1;
    for (; number < 10000; number++) {
        snprintf(path, sizeof path, "%sge_%04d.ppdmp", p4_game_dir, number);
        if (sceIoGetstat(path, &st) < 0)
            break;
    }
    gw.buf = malloc(65536);
    gw.fd = gw.buf ? sceIoOpen(path, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC, 0777) : -1;
    if (gw.fd < 0) {
        p4_log("GE dump: cannot write %s (%x)\n", path, gw.fd);
        free(gw.buf);
        return;
    }
    gw.used = gw.error = 0;
    char header[24] = "PPSSPPGE";
    unsigned version = 6;
    memcpy(header + 8, &version, 4);
    memcpy(header + 12, "SLUS21782", 9);
    gw_out(header, 24);
    gw_u32(gd.ncmds);
    gw_u32(gd.bufsz);
    gw_zstd_begin(gd.ncmds * 9);
    gw_zstd(gd.cmds, gd.ncmds * 9);
    gw_zstd_begin(gd.bufsz);
    static const unsigned char zeros[16];
    unsigned at = 0;
    for (unsigned i = 0; i < gd.npieces; i++) {
        const GdPiece *p = &gd.pieces[i];
        while (at < p->ptr) {
            unsigned k = p->ptr - at < 16 ? p->ptr - at : 16;
            gw_zstd(zeros, k);
            at += k;
        }
        gw_zstd(p->mem ? p->mem : gd.arena + p->arena, p->size);
        at += p->size;
    }
    if (gw.used && sceIoWrite(gw.fd, gw.buf, gw.used) != (int)gw.used)
        gw.error = 1;
    sceIoClose(gw.fd);
    free(gw.buf);
    p4_log("GE dump %s: %s, %u commands, %u KB data, %u prims, %u textures, display %x\n", path,
           gw.error || at != gd.bufsz ? "WRITE FAILED" : "ok", gd.ncmds, gd.bufsz >> 10, gd.prims, gd.textures, display);
}

static void gd_free(void)
{
    free(gd.cmds), free(gd.arena), free(gd.pieces), free(gd.regs), free(gd.seen);
    gd.cmds = gd.arena = 0, gd.pieces = 0, gd.regs = 0, gd.seen = 0;
    gd.ncmds = gd.cmds_cap = gd.arena_used = gd.arena_cap = gd.npieces = gd.pieces_cap = gd.nregs = gd.regs_cap = 0;
}

/* the frame's list from start to end has run and the GE is idle; display: the buffer it drew (VRAM offset) */
static void gd_record(unsigned start, unsigned end, unsigned display)
{
    unsigned t0 = sceKernelGetSystemTimeLow();
    gd.seen = calloc(GD_SEEN, sizeof *gd.seen);
    gd.failed = !gd.seen;
    gd.bufsz = gd.ndrawn = gd.ntargets = gd.prims = gd.textures = gd.bytes_mem = 0;
    gd.vaddr = gd.init[5], gd.iaddr = gd.init[6], gd.offset = 0;
    unsigned ptr = 0;
    gd_piece(ptr, 0, gd.init, sizeof gd.init);
    gd.bufsz = sizeof gd.init;
    gd_command(GD_INIT, sizeof gd.init, ptr);
    if (gd.clut_bytes) {
        ptr = gd.bufsz;
        gd_piece(ptr, 0, gd.clut, gd.clut_bytes);
        gd.bufsz += gd.clut_bytes;
        gd_command(GD_CLUT, gd.clut_bytes, ptr);
    }
    gd_walk(start & 0x0fffffff, end & 0x0fffffff);
    gd_flush_registers();
    unsigned disp[3] = {0x04000000 + display, STRIDE, 3 /* 8888 */};
    ptr = (gd.bufsz + 3) & ~3u;
    gd_piece(ptr, 0, disp, 12);
    gd.bufsz = ptr + 12;
    gd_command(GD_DISPLAY, 12, ptr);
    if (gd.failed)
        p4_log("GE dump: out of memory (%u commands, %u pieces)\n", gd.ncmds, gd.npieces);
    else
        gd_write(display);
    p4_log("GE dump took %u ms\n", (sceKernelGetSystemTimeLow() - t0) / 1000);
    gd_free();
}

/* each frame, after the pad was read: L + R + SQUARE starts a dump (once per press) */
static void gd_poll(unsigned buttons)
{
    static int held;
    const unsigned combo = PSP_CTRL_LTRIGGER | PSP_CTRL_RTRIGGER | PSP_CTRL_SQUARE;
    int now = (buttons & combo) == combo;
    if (now && !held && !gd.stage) {
        gd.saved_pipeline = p4_pipeline;
        p4_pipeline = 0; /* frames one after another, so the GE is idle between them */
        gd.stage = 1;
        p4_log("GE dump armed\n");
    }
    held = now;
}
