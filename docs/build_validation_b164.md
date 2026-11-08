# Clean source-build validation, 2026-11-08

The commands in [Building from source](building.md) were checked in a separate local directory using the current working-tree port sources and an archive of upstream commit `363eab8a966d60a9773b45f330f2344329ec4f0a`. No previous generated objects, assembly, linker scripts or rebase tables were reused. The only retail input was the local `SLUS_217.82` ELF. The existing development build and installed game were not replaced.

Environment: Windows x64, Python 3.14, the local Windows PSPDEV toolchain described in the build guide (PSP GCC 15.2.0, GNU binutils 2.44).

| Check | Result |
| --- | --- |
| Upstream C compilation | 1,416 files; zero failures across all five census groups |
| Pointer table generation | Completed from the retail input and pinned upstream metadata |
| Base linker and address audit | Passed; no proof errors or detected direct retail RAM references requiring failure |
| Final release link | Passed; 17 PSP import libraries validated; zero function pointers without a native target |
| EBOOT packaging | Passed; EBOOT size 4,655,478 bytes |
| Preparation, audio and video unit tests | 3 + 3 + 5 tests passed |
| New release hardware testing | Not performed |
| Complete clean resource conversion/install | Not repeated in this check |

The address audit still reports retained PS2 hardware-access candidates. Its documented limits apply: static checks do not establish that every routine is safe or every game scene works.

SHA256 hashes of this clean build:

```text
EBOOT.PBP  0a7a830263e3f5b37b6b859af588a032ab5289fa0d40f0c5ab6ebbf06f5527e6
p4.prx     e3ec1515304cde6ef446757f917ae6f43cd3cf3245075bdbc7a5673c7a7abf9b
p4data.bin ef9fc8a9a9b4f3a1e85cb3fac929d654b1d4ef04f675f347afd2f2a4d1627337
p4str.bin  4bc65004bf091811ee679342eed10efe1dea01ef0fd02c0e189da45adf6e7ee4
```

The new EBOOT, PRX and data image **do not match the previous local b164 release hashes**; the string table does match. This check establishes that the documented source pipeline produces a release, not that it recreates the previously hardware-tested executable byte for byte. The cause of the differences has not been isolated. Use this build's matching data files and validate it on hardware before replacing a working installation or describing it as equivalent to the older release.

Local verification artifacts are retained under `build/public_source_validation/`: compile, pointer, link and release logs plus the final manifest. These files and the generated game artifacts remain ignored. The hashes above identify this verification run, not a promise of identical output across toolchains or source changes.
