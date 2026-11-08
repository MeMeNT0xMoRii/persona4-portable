# Development history before the public repository

The initial public commit imports the current b164 source snapshot. Earlier development remains in the maintainer's local repository; its Git objects were not published because that history also contains saves and icons. The entries below preserve its commit subjects and local abbreviated IDs. These IDs are references to the local archive, not commits available from this public repository.

## Major stages

- Boot/data relocation, full decompilation compilation and EBOOT linking.
- PSP services, disc access, input, fonts, textures, animated 3D and saves.
- GE rendering, lighting/skinning, caching, guard clipping and VFPU optimization.
- Hardware profiling, reproducible experiments, crash repairs and frame pacing.
- AVC movies, SAS audio and the Windows ISO preparation utility (later working-tree work through b164).

## Earlier local commits

```text
258b071 Add PSP boot check with rebased data image and pointer proof
7382287 Repair per-TU prototype conflicts in the compile census
146b590 Keep 128-bit layouts and finish non-VU compile repairs
eab369b Compile all 1,416 decomp files with psp-gcc
0513cd9 Add link census over all compiled objects
23734b5 Link the full game into an EBOOT and enter kwlnBoot in PPSSPP
be665a2 Add platform overrides and reach the game's own debug output
8fe3366 Reassemble retail functions without C, tie C RenderWare to retail addresses
4005e92 Reassembler: struct copies, widening shifts and the second multiplier
1977fa2 GE driver skeleton: open, raster create, camera, render state
1c1123d Read game files from the .CVM containers through a sorted index; keep Sony libraries as stubs
c9b3a92 Reassembler: hardware only when dereferenced; stubs return zero; silence CRI start-up
f4fee3c Log call chains for missing functions; skip sound banks
533291b Raster lock, palette lock and TMX copy; subsystems initialise, main loop reached
4d1cfa8 Main loop runs in PPSSPP: game frames tick, CRI out of the link until audio has its own mixer
0a519e9 Im2D on the GE, raster alpha and palette fixes, PPSSPP debugger client
44dac4f Reassembler keeps lui live through delay slots; boot sequence reaches init data
b280034 VU0 matrix and vector transforms in plain C
97bc60c EE threads and semaphores on the PSP kernel: the game's file worker runs
0f1f6b2 Tie the world plugin attach to C, display raster
19c19c5 First pixels: the game's loading icon drawn through the GE driver
07e459d Retail world module only: all boot assets load, models prepare
fdbe24c Reassembler: 128-bit copies through shadow words
1dcf0c6 Skip skinned attach and material colours until 3D; boot loads 38 assets
52beaa7 Link retail RenderWare only; string table and powf in C
dad4e0d Reassembler: 32-bit shift idioms; RenderWare core runs from retail
b61162d Split containers into 1 GB parts: PPSSPP cannot seek past 2 GB. Boot reaches sequence 27 and the logo
b68a1c2 Boot reaches titleVisualDraw: movies end at once, model setup skipped for now
c5fb8fc Rebase unproven aligned words unless they read as text
3101a16 Title draw runs: first full-screen frame from the title code
7209c98 Pad on sceCtrl: title menu and the load screen respond to buttons
582c3d6 Pad records initialised, cursor moves in the title menu
6954248 Translate VU0 and MMI inline assembly to C: 85 of about 856 statements stay as traps
d87f51e Translated asm reads argument registers, quad setters are void: New Game reaches its first menu
43bd8d9 Linear glyph upload: game text is readable, New Game reaches the difficulty choice
94e47c5 Move literal retail addresses with the image: the story's first event starts loading
19bd4fd Read PS2 native textures, keep the data image inside the ELF: the story reaches the Velvet Room
614647d Models set up with the retail RenderWare: the HAnim stubs are gone
6c10daf Draw atomics on the GE, narrow s64 parameters: the first town scene renders in 3D
0dd5d89 Skin atomics on the CPU, translate the HAnim interpolators: characters animate
ba6f528 Double vertex colours, RwFrameGetLTM, game blend modes, soft-double math on floats: the Velvet Room and headings are right
656f83f Memory card on PSP files, screen feedback rects, depth/stencil state, hardware profile build: town scene runs at build 44
cdfa021 Draw RpPTank particles as camera-facing quads, translate func_003a2950, working PPSSPP screenshots
c30d8e5 Light lit meshes on the GE with s8 normals, fix black screen from reused GE list ids, pipeline off (HUD flicker), replay pad routes by the PSP clock
dd39aa2 GE lighting for guard-clipped meshes: cut corners carry interpolated s8 normals, CPU lighting 1686 -> 0 vertices a frame on the Heaven save
4682520 Count VU0 work per call site: every translated block adds its instruction count, top 8 sites logged every 256 frames
dab1af4 Mesh cache: a miss may take its mesh's entry drawn last frame when the pipeline is off
67d57ce Log why mesh cache entries change (churn by input), record held buttons and the stick for routes (tools/psp_route.py)
cf04e56 Swizzle native textures for the GE texture cache, clear in 32-pixel stripes
283de50 Skin characters on the GE: 8-bone batches built once per geometry, per-joint spheres keep the guard band
53c032c Skip the TEST_1 second pass when it cannot change a pixel: Heaven 151 -> 118 ms a frame on the PSP
f55336d GE state cache for 3D meshes, batched stall updates, GE timing experiments; cache source markers for the data build
ff32c3c Pipeline on by default, cheap guard band, chunk boxes; PSPLink tooling (b98-b110)
7721d16 Fix the mesh cache key: hash_words read floats through unsigned * (strict aliasing)
b24cac3 Reassembler: rebase a lui in a branch delay slot that is used at the branch target
a6e1a87 Log to the memory stick before PSPLink stdout, so a stuck USB cable does not stop the file log (b112)
5edfa5f Cached GE list, event-driven flip thread, task profiler (b113-b128)
9bdca8e Skinned batches carry alpha bounds: one TEST_1 pass when all pixels pass (b129)
a6b234d Fix the dungeon corridor freeze: objects[] table, not objects[1] (b130-b132)
c763ead Draw RwIm3D: Shuffle Time cards (b133-b136)
9534b36 UV shift cached per array; draw-queue profiler (b137)
848cc4a Profile the game's draw scene and animation; C bone-matrix update, off (b138-b142)
53f1912 Guard band by chunk, chunks of 8 triangles (b143)
bb08e2f Classify guard chunk boxes on the VFPU (b144)
9262410 UV memo with 1024 slots; Dojima's room at 30 FPS (b145)
d885167 2D draw census for the title screen analysis (b146)
ea8f266 Frameskip: keep the game at full speed below 30 FPS (b147)
e43cf46 Save screen at full speed; frameskip targets 30 Hz; no bone update on skipped frames (b148-b150)
```

## Work imported after the last local commit

The local commit list ends at b148-b150. Subsequent audio/video/preparation work was present in the working tree and is included in the public snapshot; it must not be described as a separate historical commit series. See the dated development notes, audio/video documentation and clean-build record for the actual measurements and their limits.
