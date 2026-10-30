# macOS candidate renderer development

The macOS candidate renderer uses a SwiftUI view adapted from nagi. The
converter, IMKit input controller, renderer IPC, and package installer remain
MoGRU components.

## One-time setup on a Mac

1. Install GitHub CLI (`gh`) and authenticate it with `gh auth login`.
2. Enable GitHub Actions once in the `Harumaru169/MoGRU` fork's Actions page.
3. Clone the fork and check out the development branch:

   ```sh
   gh repo clone Harumaru169/MoGRU
   cd MoGRU
   git switch feature/swiftui-candidate-renderer
   ```

4. Run `./scripts/dev-install-macos.sh` once, then select MoGRU in macOS Input
   Sources. The first registration may require logging out and back in.

The application is built by GitHub Actions. No local Bazel, Qt, or full Xcode
installation is required for the update loop.

## Update loop

Push a commit from the Ubuntu development environment. Then run this in the
Mac clone:

```sh
./scripts/dev-install-macos.sh
```

The script fetches the current branch's remote tip without changing the Mac
checkout, waits for the matching commit's arm64 or Intel macOS build job,
downloads its package, installs it, and stops the old renderer process. The
new renderer starts with the next candidate update. Pass a branch name as the
only argument if the Mac checkout is on a different branch.

The script refuses an unexpected Git remote, a different commit's CI run, a
failed build, or an artifact without `MoGRU.pkg`. It uses `sudo installer` for
the system installation. It does not call `install_macos.sh`, which also
updates model and user configuration files.

After an update, test candidate movement and selection in TextEdit. A change
to the input source's bundle identifier or LaunchAgent registration may
require a fresh login; normal candidate view changes keep those identifiers.

For candidate positioning changes, check the first appearance and reopening
after moving the caret elsewhere. Also test changes in candidate count,
text/emoji pages, cascading candidates, and placement near screen edges.
The entire panel should appear at its final position without a brief jump,
and typing focus should stay in the target application.
