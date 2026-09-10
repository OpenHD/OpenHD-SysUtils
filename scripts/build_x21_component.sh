#!/usr/bin/env bash
set -euo pipefail

usage="Usage: build_x21_component.sh <sdk-dir> <output-dir>"
repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
sdk_dir="$(realpath "${1:?${usage}}")"
output_dir="$(realpath -m "${2:?${usage}}")"

test -x "${sdk_dir}/relocate-sdk.sh"
test -f "${sdk_dir}/environment-setup"
test -f "${sdk_dir}/share/buildroot/toolchainfile.cmake"
"${sdk_dir}/relocate-sdk.sh"
# shellcheck disable=SC1091
source "${sdk_dir}/environment-setup"
test "${ARCH}" = "arm64"
test "${CROSS_COMPILE}" = "aarch64-buildroot-linux-gnu-"

work_dir="$(mktemp -d)"
trap 'rm -rf "${work_dir}"' EXIT
build_dir="${work_dir}/build"
stage_dir="${work_dir}/component"
toolchain_file="${sdk_dir}/share/buildroot/toolchainfile.cmake"

export HOST_CXX="${HOST_CXX:-/usr/bin/g++}"
export OPENHD_SYSROOT="${STAGING_DIR}"
export OPENHD_CROSS_TRIPLET="aarch64-buildroot-linux-gnu"
export OPENHD_CMAKE_TOOLCHAIN_FILE="${toolchain_file}"

cmake -S "${repo_root}" -B "${build_dir}" \
  -DCMAKE_TOOLCHAIN_FILE="${toolchain_file}" -DCMAKE_BUILD_TYPE=Release
cmake --build "${build_dir}" --parallel "$(nproc)" --target openhd_sys_utils

mkdir -p "${stage_dir}/usr/bin"
install -m 0755 "${build_dir}/openhd_sys_utils" \
  "${stage_dir}/usr/bin/openhd_sys_utils"
"${STRIP}" "${stage_dir}/usr/bin/openhd_sys_utils"

component_version="$(sed -nE 's/^project\(openhd_sys_utils VERSION ([^ ]+).*/\1/p' "${repo_root}/CMakeLists.txt")"
test -n "${component_version}"
source_commit="$(git -C "${repo_root}" rev-parse HEAD)"
package_version="${component_version}-${source_commit:0:12}"
package_name="openhd-sys-utils-x21b-${package_version}.tar.gz"

cat >"${stage_dir}/component-manifest.json" <<EOF
{
  "schema": 1,
  "component": "openhd-sys-utils",
  "component_version": "${component_version}",
  "package_version": "${package_version}",
  "platform": "x21b",
  "architecture": "aarch64",
  "source_commit": "${source_commit}",
  "sdk_sha256": "${X21_SDK_SHA256:-unknown}",
  "sdk_buildroot_commit": "${X21_SDK_BUILDROOT_COMMIT:-unknown}",
  "generated_at": "$(date -u +%Y-%m-%dT%H:%M:%SZ)"
}
EOF
(
  cd "${stage_dir}"
  find . -type f ! -name sha256sums -print0 | sort -z | xargs -0 sha256sum >sha256sums
)

mkdir -p "${output_dir}"
tar --numeric-owner --owner=0 --group=0 -C "${stage_dir}" \
  -czf "${output_dir}/${package_name}" .
cp "${stage_dir}/component-manifest.json" \
  "${output_dir}/${package_name}.manifest.json"
(
  cd "${output_dir}"
  sha256sum "${package_name}" >"${package_name}.sha256"
  cp "${package_name}" openhd-sys-utils-x21b-latest.tar.gz
  sha256sum openhd-sys-utils-x21b-latest.tar.gz \
    >openhd-sys-utils-x21b-latest.tar.gz.sha256
  cp "${package_name}.manifest.json" \
    openhd-sys-utils-x21b-latest.tar.gz.manifest.json
)
"${READELF}" -h "${stage_dir}/usr/bin/openhd_sys_utils" | grep -q 'Machine:.*AArch64'
"${READELF}" -d "${stage_dir}/usr/bin/openhd_sys_utils" | grep NEEDED
echo "Created ${output_dir}/${package_name}"
