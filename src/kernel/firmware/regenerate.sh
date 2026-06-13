#!/bin/bash
#
# Rebuild the kernel-ready firmware blobs (.hex sources + .fw outputs) from
# the primary artefacts under doc/firmware/. Maintainer-only; not invoked by
# build.sh. Run after changing any of the primary firmware sources.
#
# Requires the host tool `ihex2fw` from the Linux kernel's tools/firmware/
# directory. Easiest way to get it:
#
#   wget -O /tmp/ihex2fw.c https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git/plain/tools/firmware/ihex2fw.c
#   cc -O2 -o ~/.local/bin/ihex2fw /tmp/ihex2fw.c
#
# Alternatively: `sudo apt install linux-source-*` then unpack the tarball
# under /usr/src/ and run `make tools/firmware` (or just compile the one
# file above) inside it. The host tool is kernel-version-independent.
#
# Usage:
#   ./regenerate.sh               # finds ihex2fw on $PATH (or $KERNEL_SOURCE_DIR/scripts/)
#   ./regenerate.sh -k <dir>      # use a specific kernel source tree

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PRIMARY_DIR="$SCRIPT_DIR/../../../doc/firmware"

# parse -k
KERNEL_SOURCE_DIR=
while getopts ":k:" opt; do
	case $opt in
		k) KERNEL_SOURCE_DIR=$OPTARG ;;
		\?) echo "Invalid option: -$OPTARG" >&2; exit 1 ;;
	esac
done

# Locate ihex2fw
IHEX2FW=$(command -v ihex2fw || true)
if [ -z "$IHEX2FW" ]; then
	if [ -z "$KERNEL_SOURCE_DIR" ]; then
		# Fall back to the project's autodetect helper if available
		if [ -f "$SCRIPT_DIR/../get_kernel_source_dir.sh" ]; then
			# shellcheck disable=SC1091
			source "$SCRIPT_DIR/../get_kernel_source_dir.sh"
		fi
	fi
	if [ -n "$KERNEL_SOURCE_DIR" ] && [ -x "$KERNEL_SOURCE_DIR/scripts/ihex2fw" ]; then
		IHEX2FW="$KERNEL_SOURCE_DIR/scripts/ihex2fw"
	fi
fi

if [ -z "$IHEX2FW" ]; then
	cat >&2 <<EOF
ihex2fw not found.

Easiest fix:
  wget -O /tmp/ihex2fw.c \\
    https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git/plain/scripts/ihex2fw.c
  cc -O2 -o ~/.local/bin/ihex2fw /tmp/ihex2fw.c

Alternative: install linux-source-* and run \`make scripts_basic\` inside
the unpacked tree, then pass that tree with: $0 -k <dir>
EOF
	exit 2
fi

echo "Using ihex2fw: $IHEX2FW"
echo "Primary sources: $PRIMARY_DIR"
echo

if [ ! -d "$PRIMARY_DIR" ]; then
	echo "Primary firmware dir not found: $PRIMARY_DIR" >&2
	exit 2
fi

# Strip the Intel HEX EOF record from a file (used when concatenating).
strip_eof() {
	grep -v '^:00000001FF$' "$1"
}

# --- midex8 (r1): copy the post-stage-2 combined image as-is ----------------
cp -v "$PRIMARY_DIR/midex_firmware_combined.ihx" "$SCRIPT_DIR/midex8.hex"

# --- midex8r2 (r2): mac_loader stage-1 + r2 stage-2 -------------------------
{
	strip_eof "$PRIMARY_DIR/midex_mac_loader.ihx"
	cat "$PRIMARY_DIR/midex8r2_combined.ihx"
} > "$SCRIPT_DIR/midex8r2.hex"
echo "wrote $SCRIPT_DIR/midex8r2.hex (mac_loader + midex8r2_combined)"

# --- midex3 (r3): mac_loader stage-1 + r3 stage-2 ---------------------------
{
	strip_eof "$PRIMARY_DIR/midex_mac_loader.ihx"
	cat "$PRIMARY_DIR/midex3_combined.ihx"
} > "$SCRIPT_DIR/midex3.hex"
echo "wrote $SCRIPT_DIR/midex3.hex (mac_loader + midex3_combined)"

echo

# --- Convert each .hex to ihex2fw's packed .fw format -----------------------
for name in midex8 midex8r2 midex3; do
	"$IHEX2FW" "$SCRIPT_DIR/$name.hex" "$SCRIPT_DIR/$name.fw"
	echo "wrote $SCRIPT_DIR/$name.fw"
done

echo
echo "Done. Commit the .hex and .fw files in $SCRIPT_DIR/."
