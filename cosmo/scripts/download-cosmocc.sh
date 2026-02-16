#!/bin/sh
# Download cosmocc toolchain if not already present.
# Usage: download-cosmocc.sh <output_dir> <version> <sha256>
# Adapted from cosmopolitan's build/download-cosmocc.sh

set -e

OUTPUT_DIR=${1:?usage: download-cosmocc.sh OUTPUT_DIR VERSION SHA256}
COSMOCC_VERSION=${2:?version required}
COSMOCC_SHA256=${3:?sha256 required}
URL1="https://github.com/jart/cosmopolitan/releases/download/${COSMOCC_VERSION}/cosmocc-${COSMOCC_VERSION}.zip"
URL2="https://cosmo.zip/pub/cosmocc/cosmocc-${COSMOCC_VERSION}.zip"

abort() {
  printf '%s\n' "download-cosmocc: $1" >&2
  exit 1
}

# Already downloaded?
OUTPUT_DIR=${OUTPUT_DIR%/}
if [ -d "${OUTPUT_DIR}" ]; then
  exit 0
fi

# Find unzip
command -v unzip >/dev/null 2>&1 || abort "unzip is required"

# Find sha256 checker
if command -v sha256sum >/dev/null 2>&1; then
  sha256check() { sha256sum -c "$1"; }
elif command -v shasum >/dev/null 2>&1; then
  sha256check() { shasum -a 256 -c "$1"; }
else
  abort "sha256sum or shasum is required"
fi

# Find downloader
if command -v wget >/dev/null 2>&1; then
  download() { wget -qO "$1" "$2"; }
elif command -v curl >/dev/null 2>&1; then
  download() { curl -fsSL -o "$1" "$2"; }
else
  abort "wget or curl is required"
fi

printf '%s\n' "downloading cosmocc ${COSMOCC_VERSION}..." >&2

# Atomic download: work in a temp directory, move when done
TMPDIR="${OUTPUT_DIR}.tmp.$$"
mkdir -p "${TMPDIR}"
cleanup() { rm -rf "${TMPDIR}"; }
trap cleanup EXIT

# Download with fallback URL
if ! download "${TMPDIR}/cosmocc.zip" "${URL1}" 2>/dev/null; then
  download "${TMPDIR}/cosmocc.zip" "${URL2}" || abort "download failed"
fi

# Verify checksum
printf '%s\n' "${COSMOCC_SHA256} *${TMPDIR}/cosmocc.zip" > "${TMPDIR}/cosmocc.zip.sha256"
sha256check "${TMPDIR}/cosmocc.zip.sha256" || abort "checksum mismatch"

# Extract and commit
unzip -q "${TMPDIR}/cosmocc.zip" -d "${TMPDIR}" || abort "unzip failed"
rm -f "${TMPDIR}/cosmocc.zip" "${TMPDIR}/cosmocc.zip.sha256"
trap - EXIT
mv "${TMPDIR}" "${OUTPUT_DIR}" || abort "mv failed"

# Symlink for convenience
ln -sfn "$(basename "${OUTPUT_DIR}")" "$(dirname "${OUTPUT_DIR}")/current"
printf '%s\n' "cosmocc ${COSMOCC_VERSION} installed to ${OUTPUT_DIR}" >&2
