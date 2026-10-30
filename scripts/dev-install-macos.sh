#!/usr/bin/env bash
# Install the latest successful macOS CI package for the requested branch.
set -euo pipefail

if [[ "$(uname -s)" != "Darwin" ]]; then
  echo "This installer must run on a Mac." >&2
  exit 1
fi

cd "$(dirname "${BASH_SOURCE[0]}")/.."

ci_repo="Harumaru169/MoGRU"
remote_url="$(git remote get-url origin)"
case "$remote_url" in
  git@github.com:Harumaru169/MoGRU|git@github.com:Harumaru169/MoGRU.git|\
  https://github.com/Harumaru169/MoGRU|https://github.com/Harumaru169/MoGRU.git) ;;
  *) echo "Unexpected origin: $remote_url" >&2; exit 1 ;;
esac

branch="${1:-$(git branch --show-current)}"
if [[ -z "$branch" || "$branch" == -* ]]; then
  echo "Usage: $0 [remote-branch]" >&2
  exit 1
fi

case "$(uname -m)" in
  arm64) artifact_name=MoGRU-macOS-arm64 ;;
  x86_64) artifact_name=MoGRU-macOS-intel64 ;;
  *) echo "Unsupported Mac architecture: $(uname -m)" >&2; exit 1 ;;
esac

# Ignore queued/running builds and use the latest successful run immediately.
run_id="$(gh run list -R "$ci_repo" -w macos.yaml -b "$branch" \
  --status success --limit 1 --json databaseId --jq '.[0].databaseId // empty')"
if [[ -z "$run_id" ]]; then
  echo "No successful macOS CI run found for branch $branch." >&2
  exit 1
fi

commit="$(gh run view "$run_id" -R "$ci_repo" \
  --json headSha --jq '.headSha')"
echo "Installing MoGRU $commit from CI run $run_id ($artifact_name)"

download_dir="$(mktemp -d)"
trap 'rm -rf "$download_dir"' EXIT
gh run download "$run_id" -R "$ci_repo" -n "$artifact_name" -D "$download_dir"
package="$download_dir/MoGRU.pkg"
if [[ ! -f "$package" ]]; then
  echo "CI artifact did not contain MoGRU.pkg." >&2
  exit 1
fi

# Avoid leaving the old renderer process mapped while the package is updated.
killall MoGRURenderer >/dev/null 2>&1 || true
sudo installer -pkg "$package" -target /
killall MoGRURenderer >/dev/null 2>&1 || true

installed="/Library/Input Methods/MoGRU.app/Contents/Resources/MoGRURenderer.app/Contents/MacOS/MoGRURenderer"
if [[ ! -x "$installed" ]]; then
  echo "Installation finished, but the renderer executable is missing: $installed" >&2
  exit 1
fi
echo "Installed $commit. The new renderer starts on the next candidate update."
