# ORQA builds

ORQA SysUtils is cross-compiled by the `build-orqa` job in
[OpenHD's final_ci workflow](https://github.com/OpenHD/OpenHD/actions/workflows/final_ci.yml).
That repository owns the SDK download credentials and installs the SDK once for
OpenHD and SysUtils. SysUtils does not need a separate SDK download workflow or
copies of those credentials.

The builder checks out SysUtils `dev-release`. After publishing a SysUtils-only
change to that branch, manually run `final_ci` on OpenHD's `openhd-3.0` branch.
SysUtils pushes alone do not trigger that workflow.

The `openhd-sysutils-aarch64` artifact contains the ARM64 Debian package, the
SysUtils binary, the ORQA camera probe, and `usb_modeswitch` with its license.
The existing `openhd-orqa` deploy archive also includes SysUtils and the utility
for the ORQA Yocto image. The Debian package requires Debian package management
and is not the installer for the Yocto image.
