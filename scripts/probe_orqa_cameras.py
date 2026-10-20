#!/usr/bin/env python3
"""Measure ORQA DTK signal profiles; run with OpenHD stopped.

Dummy CSI sensors advertise arbitrary dimensions. Validate captured image buffers
and measure frame arrival rate instead. This does not read a camera model ID or
enumerate modes a camera could be reprogrammed to produce.
"""
import argparse
import json
import pathlib
import re
import subprocess
import tempfile


def run(*args):
    return subprocess.run(args, capture_output=True, text=True, timeout=12,
                          check=True).stdout


def configure(node, pad, width, height, code="0x2006"):
    run("v4l2-ctl", "-d", node,
        f"--set-subdev-fmt=pad={pad},width={width},height={height},code={code}")


def probe(index):
    # Same CSI/ISI mapping as OpenHD's native ORQA backend on this SDK.
    capture = f"/dev/video{2 + index}"
    sensor = f"/dev/v4l-subdev{3 - index}"
    csi = f"/dev/v4l-subdev{index}"
    saved = []
    capture_format = run("v4l2-ctl", "-d", capture, "--get-fmt-video")
    size = re.search(r"Width/Height\s*:\s*(\d+)/(\d+)", capture_format)
    pixel = re.search(r"Pixel Format\s*:\s*'([^']+)'", capture_format)
    if not size or not pixel:
        raise RuntimeError(f"Cannot save capture format on {capture}")
    result = {"csi": index, "capture": capture, "profile_match": None,
              "identity_source": "measured signal, no model ID"}
    try:
        for node, pad in ((sensor, 0), (csi, 0), (csi, 4)):
            fmt = run("v4l2-ctl", "-d", node, f"--get-subdev-fmt=pad={pad}")
            wh = re.search(r"Width/Height\s*:\s*(\d+)/(\d+)", fmt)
            code = re.search(r"Mediabus Code\s*:\s*(0x[0-9a-fA-F]+)", fmt)
            if not wh or not code:
                raise RuntimeError(f"Cannot save {node} pad {pad}")
            saved.append((node, pad, *wh.groups(), code.group(1)))
        with tempfile.TemporaryDirectory(prefix="orqa-camera-") as temporary:
            for width, height in ((1920, 1080), (960, 720)):
                for node, pad, *_ in saved:
                    configure(node, pad, width, height)
                raw = pathlib.Path(temporary) / "frames.yuyv"
                command = ["v4l2-ctl", "-d", capture,
                           f"--set-fmt-video=width={width},height={height},pixelformat=YUYV",
                           "--stream-mmap=3", "--stream-count=3", f"--stream-to={raw}"]
                try:
                    subprocess.run(command, capture_output=True, timeout=4, check=True)
                except (subprocess.TimeoutExpired, subprocess.CalledProcessError):
                    continue
                frame_size = width * height * 2
                data = raw.read_bytes()
                # Oversized dummy-sensor formats can dequeue successfully with
                # a repeated partial image and zero-filled trailing rows.
                if len(data) != 3 * frame_size or any(
                    not any(data[end - width * 2:end])
                    for end in (frame_size, 2 * frame_size, 3 * frame_size)
                ):
                    continue
                rate = subprocess.run(
                    ["v4l2-ctl", "-d", capture, "--stream-mmap=3",
                     "--stream-count=240", "--stream-to=/dev/null"],
                    capture_output=True, text=True, timeout=8, check=True)
                rates = re.findall(r"([0-9.]+) fps", rate.stdout + rate.stderr)
                if not rates:
                    raise RuntimeError(f"No measured frame rate on {capture}")
                fps = float(rates[-1])
                result.update(width=width, height=height, measured_fps=fps)
                if width == 1920 and abs(fps - 60) < 2:
                    result.update(profile_match="ORQA v1", camera_type=126)
                elif width == 960 and abs(fps - 120) < 2:
                    result.update(profile_match="RunCam Wasp", camera_type=122)
                break
    finally:
        for entry in saved:
            configure(*entry)
        run("v4l2-ctl", "-d", capture,
            f"--set-fmt-video=width={size[1]},height={size[2]},pixelformat={pixel[1]}")
    return result


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sysutils-config", action="store_true",
                        help="emit flat provisioning fields for SysUtils")
    args = parser.parse_args()
    model = pathlib.Path("/proc/device-tree/model").read_text()
    if "orqa" not in model.lower():
        raise SystemExit("This probe requires an ORQA DTK media graph.")
    if subprocess.run(["systemctl", "is-active", "--quiet", "openhd.service"]).returncode == 0:
        raise SystemExit("Stop openhd.service before probing; restart it afterwards.")
    cameras = [probe(0), probe(1)]
    if args.sysutils_config:
        config = {}
        for index, camera in enumerate(cameras):
            if camera["profile_match"] is None:
                continue
            prefix = "camera" if index == 0 else "camera2"
            config[prefix + "_type"] = camera["camera_type"]
            config[prefix + "_resolution_fps"] = (
                f'{camera["width"]}x{camera["height"]}@{round(camera["measured_fps"])}')
        print(json.dumps(config))
    else:
        print(json.dumps({"cameras": cameras}, indent=2))
