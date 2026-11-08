# Publishing the project

The source repository is [dreikk91/persona4-psp](https://github.com/dreikk91/persona4-psp). Original port source, build tools and documentation are published from a separate source snapshot with fresh Git history. Local game artifacts and external checkouts are excluded from that snapshot. The optional b164 Windows preparation utility is a separate release asset.

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

`tools/psp_prepare_release.py` bundles an EBOOT containing game-derived code. Reconstructing data from a user's ISO does not establish executable redistribution rights. The MIT grant does not cover that game code. Preserve the bundled tools' notices, version/source information and any required corresponding source when distributing the preparation package. The Windows package uses the earlier prepared b164 executable, not the independently checked clean-build binary; both sets of hashes are documented separately.

The tested Windows compiler was a locally built PSPDEV toolchain. Its recorded version identifiers are in [the build guide](building.md); a verified public download of that exact toolchain and bit-for-bit release reproducibility are not established here. Do not advertise a fully self-contained or one-click source build until a clean public setup has been verified.
