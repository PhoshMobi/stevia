#!/usr/bin/python3
#
# Copyright (C) 2024 The Phosh Developers
#
# Author: Guido Günther <agx@sigxcpu.org>
#
# Add info about available completers

import argparse
import glob
import os
import sys
import json


def get_layouts_info(path, varnam, uim):
    layouts = []

    for file in sorted(glob.glob(os.path.join(path, "*.json"))):

        name = os.path.basename(file).split(".")[0]
        # Not handles in p-o-s
        if name == "terminal":
            continue

        j = json.load(open(file))
        layouts.append(
            {
                "type": "xkb",
                "layout-id": name,
                "name": j["name"],
            }
        )

    # TODO: need to provide this at runtime based on installed schemes:
    if varnam:
        layouts.append(
            {
                "type": "ibus",
                "layout-id": "varnam:ml",
                "name": "Malayalam (via varnam)",
            }
        )
    # TODO: need to provide this at runtime based on installed dependencies/fonts
    if uim:
        layouts.append(
            {
                "type": "ibus",
                "layout-id": "uim:jp",
                "name": "Japanese (anthy)",
            }
        )
        layouts.append(
            {
                "type": "ibus",
                "layout-id": "uim:cn",
                "name": "Chinese (pinyin)",
            }
        )
    return {"layouts": layouts}


def main(argv):
    parser = argparse.ArgumentParser(description="Write OSK layout info")
    parser.add_argument("--layouts", action="store", default="src/layouts")
    parser.add_argument(
        "--varnam", action=argparse.BooleanOptionalAction, default=False
    )
    parser.add_argument(
        "--uim", action=argparse.BooleanOptionalAction, default=False
    )
    parser.add_argument("--out", action="store", default="layouts.json")
    args = parser.parse_args(argv[1:])

    info = get_layouts_info(args.layouts, args.varnam, args.uim)
    with open(args.out, "w") as f:
        f.write(json.dumps(info, indent=2))

    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
