#!/usr/bin/env bash
# Build the standalone USB_ModeSwitch executable using the active toolchain.
# For ORQA, source environment-setup-armv8a-poky-linux before invoking this.
set -euo pipefail

if [[ $# -ne 1 ]]; then
  echo "Usage: $0 <output-file> (CC/CFLAGS/LDFLAGS from target SDK)" >&2
  exit 2
fi

output="$(realpath -m "$1")"
work="$(mktemp -d)"
trap 'rm -rf "${work}"' EXIT
version=2.6.2
archive="${work}/usb-modeswitch.tar.bz2"
curl --fail --location --retry 5 \
  "https://www.draisberghof.de/usb_modeswitch/usb-modeswitch-${version}.tar.bz2" \
  -o "${archive}"
echo "f7abd337784a9d1bd39cb8a587518aff6f2a43d916145eafd80b1b8b7146db66  ${archive}" | sha256sum --check
tar -xjf "${archive}" -C "${work}"
read -r -a compiler <<< "${CC:-cc}"
read -r -a cflags <<< "${CFLAGS:--O2}"
read -r -a ldflags <<< "${LDFLAGS:-}"
read -r -a pkgconfig <<< "${PKG_CONFIG:-pkg-config}"
read -r -a usb_cflags <<< "$("${pkgconfig[@]}" --cflags libusb-1.0)"
read -r -a usb_libs <<< "$("${pkgconfig[@]}" --libs libusb-1.0)"
"${compiler[@]}" "${cflags[@]}" "${usb_cflags[@]}" \
  "${work}/usb-modeswitch-${version}/usb_modeswitch.c" \
  "${ldflags[@]}" "${usb_libs[@]}" -o "${work}/usb_modeswitch"
if [[ -n "${READELF:-}" ]]; then
  "${READELF}" -h "${work}/usb_modeswitch" | grep -q 'Machine:.*AArch64'
fi
"${STRIP:-strip}" --strip-unneeded "${work}/usb_modeswitch"
mkdir -p "$(dirname "${output}")"
install -m 0755 "${work}/usb_modeswitch" "${output}"
# Preserve upstream licensing alongside the binary in deploy archives.
install -m 0644 "${work}/usb-modeswitch-${version}/COPYING" "${output}.COPYING"
echo "USB_ModeSwitch built at ${output}"
