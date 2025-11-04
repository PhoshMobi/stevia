#!/usr/bin/python3
#
# Copyright (C) 2024 The Phosh Developers
#
# Author: Guido Günther <agx@sigxcpu.org>
#
# Screenshot all layouts. This needs phoc, swaybg and montage (from
# Imagemagick) in $PATH.
#
# Screenshots are put into _build/screenshots.


import argparse
import glob
import os
import os
import pathlib
import re
import subprocess
import sys
import time


def spawn_phoc(phoc_ini, bg):
    compositor = 'phoc'
    try:
        p = subprocess.Popen(
            [compositor, "-C", phoc_ini],
            stdout=subprocess.PIPE,
            env=dict(os.environ, WLR_BACKENDS="headless"),
            close_fds=True,
        )
    except Exception as e:
        print(f"Failed to run {compositor}: {e}", file=sys.stderr)
        return None

    r = re.compile("Running [a-zA-Z ]+ '(wayland-[0-9]+)'")
    while True:
        line = p.stdout.readline().decode()

        m = r.match(line)
        if m:
            display = m.group(1)
            print(f"Found phoc. Setting `WAYLAND_DISPLAY` to '{display}'")
            os.environ["WAYLAND_DISPLAY"] = display
            break

        ret = p.poll()
        if ret:
            print(f"phoc exited with {ret}")
            return None

    # FIXME: don't hardcode
    subprocess.Popen(["swaybg", "-i", bg])

    return p


def screenshot_layouts(out):
    for json in glob.glob("src/layouts/*.json"):

        layout = os.path.basename(json).split(".")[0]
        png = out / f"{layout}.png"
        print(f"Screenshotting layout '{layout}' at '{png}'")

        # Not handles in p-o-s
        if layout == 'terminal':
            continue

        p_osk = subprocess.Popen(
            ["_build/run", "--allow-replacement", "--replace"],
            env=dict(
                os.environ,
                G_DEBUG="fatal-criticals",
                GSETTINGS_BACKEND="memory",
                POS_TEST_LAYOUT=layout,
                POS_DEBUG="force-show",
            ),
        )
        # FIXME: check for DBus name
        time.sleep(2)
        if p_osk.poll():
            print(f"Spawning OSK for {layout} failed", file=sys.stderr)
            return 1

        try:
            subprocess.run(["grim", png], check=True, close_fds=True)
        except Exception:
            print(f"Screenshot for '{layout}' failed", file=sys.stderr)
            return 1

        p_osk.terminate()

    subprocess.run(
        f"montage -mode concatenate {out}/*.png {out}/../overview.png",
        shell=True,
        check=True,
    )


def main(argv):
    parser = argparse.ArgumentParser(description="Screenshot OSK layouts")
    parser.add_argument(
        "-p", "--phoc-ini", action="store", default="/usr/share/phosh/phoc.ini"
    )
    parser.add_argument(
        "-i",
        "--background-image",
        action="store",
        default="/usr/share/phosh/backgrounds/byzantium-abstract-720x1440.jpg",
    )
    parser.add_argument(
        "-o", "--output-dir", action="store", default="_build/screenshots/360x720@1"
    )
    parser.add_argument(
        "-b", "--binary", action="store", default="_build/src/phosh-osk-stevia"
    )
    args = parser.parse_args(argv[1:])

    p_compositor = spawn_phoc(args.phoc_ini, args.background_image)
    if p_compositor is None:
        return 1

    try:
        out = pathlib.Path(args.output_dir)
        out.mkdir(parents=True, exist_ok=True)
        if screenshot_layouts(out):
            return 1
    except KeyboardInterrupt:
        print("Exiting…")

    p_compositor.kill()
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
