#!/usr/bin/env bash
# Install the macOS CI package for exactly the current branch's remote commit.
set -euo pipefail

if [[ "$(uname -s)" != "Darwin" ]]; then
  echo "This installer must run on a Mac." >&2
  exit 1
fi

for command in git gh sudo installer; do
  if ! command -v "$command" >/dev/null 2>&1; then
    echo "Missing command: $command" >&2
    exit 1
  fi
done

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$repo_root"

remote_url="$(git remote get-url origin)"
case "$remote_url" in
  git@github.com:lambda-haru/MoGRU.git|https://github.com/lambda-haru/MoGRU.git) ;;
  *) echo "Unexpected origin: $remote_url" >&2; exit 1 ;;
esac

branch="${1:-$(git branch --show-current)}"
if [[ -z "$branch" || "$branch" == -* ]]; then
  echo "Usage: $0 [remote-branch]" >&2
  exit 1
fi

case "$(uname -m)" in
  arm64) job_name=build_arm64; artifact_name=MoGRU-macOS-arm64 ;;
  x86_64) job_name=build_intel64; artifact_name=MoGRU-macOS-intel64 ;;
  *) echo "Unsupported Mac architecture: $(uname -m)" >&2; exit 1 ;;
esac

# Fetch only this repository's requested branch. The Mac checkout is not
# changed; its remote tip selects the exact CI build to install.
git fetch origin "$branch"
commit="$(git rev-parse FETCH_HEAD)"
echo "Installing MoGRU $commit ($artifact_name)"

run_id=""
for ((attempt=0; attempt<60; attempt++)); do
  run_id="$(gh run list -R lambda-haru/MoGRU -w macos.yaml -c "$commit" \
    --json databaseId --jq '.[0].databaseId // empty')"
  [[ -n "$run_id" ]] && break
  sleep 10
done
if [[ -z "$run_id" ]]; then
  echo "No macOS CI run appeared for $commit." >&2
  exit 1
fi

actual_commit="$(gh run view "$run_id" -R lambda-haru/MoGRU \
  --json headSha --jq '.headSha')"
if [[ "$actual_commit" != "$commit" ]]; then
  echo "CI commit mismatch: expected $commit, got $actual_commit" >&2
  exit 1
fi

echo "Waiting for CI run $run_id, job $job_name ..."
for ((attempt=0; attempt<540; attempt++)); do
  job_state="$(gh run view "$run_id" -R lambda-haru/MoGRU --json jobs \
    --jq ".jobs[] | select(.name == \"$job_name\") | [.status, .conclusion] | @tsv")"
  case "$job_state" in
    $'completed\tsuccess') break ;;
    completed*) echo "CI job failed: $job_state" >&2; exit 1 ;;
  esac
  sleep 10
done
if [[ "$job_state" != $'completed\tsuccess' ]]; then
  echo "Timed out waiting for $job_name." >&2
  exit 1
fi

download_dir="$(mktemp -d)"
trap 'rm -rf "$download_dir"' EXIT
gh run download "$run_id" -R lambda-haru/MoGRU -n "$artifact_name" -D "$download_dir"
package="$(find "$download_dir" -type d -name 'MoGRU.pkg' -print -quit)"
if [[ -z "$package" ]]; then
  package="$(find "$download_dir" -type f -name 'MoGRU.pkg' -print -quit)"
fi
if [[ -z "$package" ]]; then
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
