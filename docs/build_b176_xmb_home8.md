# b176-xmb-home8

Update for the existing US installation. The tested release PBP has SHA-256
`cfe1683e02dce874c0cfa3b7e7068f19da19266b6a449253c75278a722c06d54`.

| Report | Change and PPSSPP result |
| --- | --- |
| P4-007, nested TV screens | The colour layers use a common opening and retain their coloured edges. Oversized fans are clipped before submission to the GE. On 04/17 the entry showed the concentric grey screens in a phase that previously filled with green. The return transition needs a replay. |
| P4-011, rectangles in battle effects | The matching framebuffer blend uses stencil coverage from the effect texture in four opacity bands. Replaying the same 1F encounter with the mask on and off showed the red/orange rectangles at entry and defeat with the old path. The new path showed shaped effects in those phases. Shadow Chie needs a separate replay. |
| P4-012, obscured level-up portrait | The colour pass uses the silhouette's texture alpha even when applying a constant tint. An early home8 build showed the hero portrait, text and stats at a natural level-up. The release uses the same tint code and displayed the regular results screen correctly. |
| P4-018, umbrella holes | The translucent world bucket uses the original alpha reference. Toggling the change on the 04/15 school route showed the broken canopy before and complete translucent panels after. The Flood Plain and detective scene need their own replay. |
| P4-026, All-Out Attack | Box culling uses the camera's real near plane when the GE clips the near edge. Yosuke's prompt portrait and both attack cut-ins appeared with box culling enabled in the castle replay. The rushing models and impact brightness need review. |
| P4-031 and scripted motions | The port repairs invalid animation frames according to their compressed or uncompressed layout, handles coincident key times and checks the head controller. Matrix products also support an aliased destination. Heads were visible in the tested release encounter. An intermittent failure at Shadow Chie and the reported scripted motions still need scene-specific replays. |

Release builds leave VU0 work counters off. The instrumented castle run
counted up to 6,845 calls per rendered frame to the counter in one battle
window. That is a work count. Frame times at 333 MHz need a PSP test.

## Build validation

- Linux PSP GCC 15.2.0 compiled 1,416 translation units. The release linked
  825 staged inputs, validated 19 PSP import libraries and found zero
  unresolved native function pointers.
- The data audit checked 5,857 rebased entries and 168 switch targets.
  Both host clipping checks and the PPSSPP depth-mask smoke test passed.
- The host suite recorded 59 passes, two platform skips and the preceding
  build's three errors in the Windows VAG encoder and renderer mocks.
- The scene replay used PPSSPP 1.20.4 software rendering and private saves.
  Hardware exit, long-session movies and full audio still need testing.

## Installation

Back up the existing P4 directory, including `saves`. Copy the update's
`EBOOT.PBP`, `p4data.bin` and `p4str.bin` together into the existing game
directory (for example `PSP/GAME/CAT_Ports/P4`). The log starts with
`b176-xmb-home8`. [Slot 8](replay_saves.md) resumes the regular playthrough.
Slot 7 provides the 04/17 route to the castle and slot 4 the 04/15 school route.
