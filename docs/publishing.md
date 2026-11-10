# Publishing the project

The source repository is [dreikk91/persona4-psp](https://github.com/dreikk91/persona4-psp). Original port source, build tools and documentation are published from a separate source snapshot with fresh Git history. Local game artifacts and external checkouts are excluded from that snapshot. The older Windows preparation release asset was withdrawn; its source-build replacement is packaged locally.

For updates, clone the existing public repository with `gh repo clone dreikk91/persona4-psp`, copy only the reviewed source/documentation changes into that clone, and commit and push from there. Preserve the public history; do not push the development checkout's history. Uploading a Windows utility to Releases is a separate operation from pushing source changes.

## Source publication

1. Read [the license scope](../THIRD_PARTY.md). Resolve provenance and applicable permissions for any reconstructed or adapted code you intend to distribute. The external P4 decompilation has no repository-wide license in the inspected revision; attribution is required context, not a substitute for permission.
2. Include the complete current `port/`, `tools/`, documentation, root README, LICENSE and THIRD_PARTY.md. A number of b164 audio/video/preparation files may still be untracked in the local working tree: a push includes committed files only.
3. Exclude the disc image, retail ELF, external checkout, toolchains, generated assembly and objects, EBOOT, PRX, game data, converted media, memory cards and saves. `.gitignore` covers the local locations and common formats; it does not remove anything already committed.
4. Inspect both the files being committed and all history that will be pushed. **The local Git history contains `card0/` save data and save icons**, even though the current checkout does not track them. Adding ignore rules does not remove those historical objects. For an initial public release, use a new directory containing an explicitly reviewed source snapshot and initialize fresh Git history there. Keep the existing workspace and its history intact; do not push its existing branches or tags without separately cleaning and auditing their history.
5. Follow [the build guide](building.md) from that snapshot with an empty build directory, using the pinned upstream commit and your own ISO. Record the toolchain and dependency versions, build logs, manifest hashes and hardware results. A successful build alone does not establish full-game compatibility or bit-identical reproduction of an older binary.
6. Create the GitHub repository under the intended account, commit the reviewed files, add the remote and push. Use a release tag only after reviewing the committed snapshot and verification results.

Useful local review commands (PowerShell, from the repository root):

```powershell
git status --short
git diff --check
git diff --cached --stat
git diff --cached
git ls-files
git rev-list --objects --all
```

Review filenames and file contents, including historical versions, for game data, personal paths, credentials and third-party material. Do not use `git add -f` on ignored generated files. Publication should not depend on untracked sources that only exist on the maintainer's machine.

## Release artifacts

Do not upload `build/release_b164/UPDATE`, the complete installation folder or any converted media. They are local installation products.

The withdrawn b164 package contained an EBOOT with game-derived code. The packager now defaults to a source-build package that compiles EBOOT and matching data locally; it does not include a prebuilt game executable, game objects or a numerical data recipe. The utility downloads the compiler from the separate [toolchain release](https://github.com/dreikk91/persona4-psp/releases/tag/toolchain-gcc15.2), whose archive also carries the corresponding source of GCC, binutils, newlib, PSPSDK and the statically linked GMP/MPFR/MPC. The pinned upstream source is obtained separately. Legacy EBOOT packaging requires `--legacy-prebuilt` explicitly.

Local compilation does not establish permissions for the sources or tools. The MIT grant covers only our original contributions. Preserve bundled tools' notices, version/source information and any required corresponding source. Creating the source-build package does not upload it or update the existing release.

The tested Windows compiler is published as the [toolchain release](https://github.com/dreikk91/persona4-psp/releases/tag/toolchain-gcc15.2); its fingerprint is `recommended_toolchain.sha256` in `tools/build_reference.json`. Bit-for-bit release reproducibility on other machines is not established here. Do not advertise a fully self-contained or one-click source build until a clean public setup has been verified.
