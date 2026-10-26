#!/bin/sh
# Builds the Pangolin CLI bundled by rpm/harbour-pangolin-ui.spec:
# fosrl/cli at %define pangolin_version, with olm patched by
# pangolin-cli/olm-systemd238.patch, for aarch64 only.
# Output: rpm/pangolin-cli_linux_arm64 (mb2 uses rpm/ as %{_sourcedir}).
#
# Usage: sh pangolin-cli/build.sh      (needs Docker, or Go when run with --in-container)
set -eu

HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(dirname "$HERE")
CLI_VERSION=$(sed -n 's/^%define pangolin_version //p' "$ROOT/rpm/harbour-pangolin-ui.spec")
GO_IMAGE=${GO_IMAGE:-golang:1.26}

if [ "${1:-}" != "--in-container" ]; then
    # Git Bash on Windows: native path for the mount, and no rewriting of
    # /src into a Windows path (MSYS_NO_PATHCONV is ignored elsewhere).
    MOUNT=$(cd "$ROOT" && (pwd -W 2>/dev/null || pwd))
    MSYS_NO_PATHCONV=1 exec docker run --rm -e CLI_VERSION="$CLI_VERSION" -v "$MOUNT:/src" -w /src \
        "$GO_IMAGE" sh pangolin-cli/build.sh --in-container
fi

WORK=$(mktemp -d)
git clone -q --depth 1 --branch "$CLI_VERSION" https://github.com/fosrl/cli "$WORK/cli"
cd "$WORK/cli"

# Patch the exact olm version this CLI release uses.
OLM_VERSION=$(go list -m -f '{{.Version}}' github.com/fosrl/olm)
go mod download github.com/fosrl/olm@"$OLM_VERSION"
cp -r "$(go list -m -f '{{.Dir}}' github.com/fosrl/olm)" "$WORK/olm"
chmod -R u+w "$WORK/olm"
(cd "$WORK/olm" && git apply "$HERE/olm-systemd238.patch")
go mod edit -replace github.com/fosrl/olm="$WORK/olm"

# Same ldflags as the upstream Makefile.
NEWT_VERSION=$(go list -m -f '{{.Version}}' github.com/fosrl/newt | sed 's/^v//')
LDFLAGS="-s -w -X github.com/fosrl/cli/internal/version.Version=$CLI_VERSION \
 -X github.com/fosrl/cli/internal/version.NewtVersionOverride=$NEWT_VERSION"

go vet github.com/fosrl/olm/dns/platform
CGO_ENABLED=0 GOOS=linux GOARCH=arm64 go build -trimpath -ldflags "$LDFLAGS" \
    -o "$ROOT/rpm/pangolin-cli_linux_arm64" .
echo "Built rpm/pangolin-cli_linux_arm64: fosrl/cli $CLI_VERSION, olm $OLM_VERSION + olm-systemd238.patch"
