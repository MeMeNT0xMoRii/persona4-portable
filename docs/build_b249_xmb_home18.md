# b249-xmb-home18 Persona fusion update

Home18 starts from the verified [home17 source](build_b249_xmb_home17.md),
tree `0e9b56f34e07d86031d0f1fba44ee6249b9fa462`. Its three runtime files
are an update for the prepared US `SLUS_217.82` installation. The PSP Go
tester confirmed the home17 Social Stats layout and day-change display.

## P4-057 · Persona fusion

The latest PSP Go log reaches `confirm-rows` in Normal Spread state `2a`
at 111889 ms. Its final entry is `ingredients` at 111934 ms with Persona
IDs 1 and 26 (Izanagi and Ukobach). The next expected checkpoint is
`preview-begin` for Apsaras (ID 14).

The second confirmation retracts the Persona rows and their level digits.
In `nmMesWindowShowNumber`, both outgoing digit branches reload four color
bytes at animation offset `0x75`. GCC ignored `packed` on the typedef and
generated word-aligned `lw` instructions. At the expected home17 PRX load
address, they are at `0x08AF5068` and `0x08AF50F0`. The US PS2 executable
uses `lwr` and `lwl` at both sites. The PSP source mirror now applies
`packed` to the struct definition and checks its byte alignment at compile
time. Both retrace loads in home18 use `lwl` and `lwr`.

## Verification

The reproduced home17 `EBOOT.PBP`, `p4data.bin` and `p4str.bin` match the
previous package byte for byte. An alignment-checking MIPS CPU test
relocated the PRX from each packaged EBOOT and ran the real Normal Spread
confirmation and sprite tween code with synthetic menu data. Home17
raised an address error at both loads when each digit was active alone,
after `ingredients`. Home18 passed six cases with roster capacities 6,
8, 10 and 12. The digit fades preserved all four color bytes and the
menu reached `preview-queued` with the fixture's Apsaras record. This
test stubs platform I/O and the Persona model callbacks. Full game and
PSP Go replay of home18 are pending.

The PSP build compiled 1,416 units, checked 19 import libraries and
audited 5,857 rebased entries and 168 switch targets with zero proof
errors. Full host discovery reported 74 passes, two skips and the three
baseline harness errors (missing Windows VAG encoder compiler and two
renderer harness compilation failures).

## PSP Go replay

1. Replace the three runtime files together and launch from the XMB at
   333 MHz with sound. Check for `b249-xmb-home18` in `p4.log`.
2. In Normal Spread from slot 10, choose Izanagi and Ukobach. Confirm the
   Apsaras preview, watch the fusion finish and check Apsaras in the roster.
   Save to a fresh slot and test it after restarting the game.
3. If the PSP crashes, copy `p4.log` before relaunching. The `FUSION`
   entries identify how far the menu reached. The three runtime files
   from the home17 package can be restored together for rollback.
