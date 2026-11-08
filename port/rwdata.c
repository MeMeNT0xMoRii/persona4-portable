/* RenderWare file statics that are in retail data but whose address the decomp does not know.
   Only C uses them, so a private copy is enough. Sizes come from the retail table at 0x763c48 where it names them. */

typedef struct { int size, orig, max, staticAlloc; void *first, *last; } Registry;
typedef struct { int globalsOffset, numInstances; } Module;
typedef struct { unsigned w[9]; } FreeList;

void *RwEngineInstance = (void *)(0x8872e0 + P4_K);

int _rwFrameFreeListBlockSize = 0x32, _rwFrameFreeListPreallocBlocks = 1;
int _rwStreamFreeListBlockSize = 0x40, _rwStreamFreeListPreallocBlocks = 1;
int _rwPluginRegFreeListBlockSize = 0x10, _rwPluginRegListPreallocBlocks = 1;
int _rwImageFormatFreeListBlockSize = 0x10;

/* In-place RwFreeList structures, not pointers. */
FreeList _rwStreamFreeList;
FreeList toolkitRegEntriesSpace;
