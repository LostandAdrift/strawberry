#!/usr/bin/env python3
"""Prepare a Git checkout with upstream's published release build configuration."""
import argparse
import hashlib
from pathlib import Path
import tarfile

SHA256 = "cffc0cd453f3126a930b9e67f547e54d9fde0c8754fdead8185d2b2f5c68c037"
MEMBER = "strawberry-1.2.31/src/apicredentials.h"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("archive", type=Path)
    args = parser.parse_args()
    if hashlib.sha256(args.archive.read_bytes()).hexdigest() != SHA256:
        parser.error("Archive does not match the pinned official Strawberry 1.2.31 release")
    with tarfile.open(args.archive, "r:xz") as archive:
        member = archive.getmember(MEMBER)
        if not member.isfile() or member.size > 65536:
            parser.error("Unexpected release configuration member")
        data = archive.extractfile(member).read()
    destination = Path(__file__).resolve().parents[2] / "src" / "apicredentials.h"
    if destination.exists():
        if destination.read_bytes() != data:
            parser.error("Existing application configuration differs; preserve it or move it aside explicitly")
    else:
        with destination.open("xb") as output:
            output.write(data)
    print("Official release application configuration ready; no account settings changed.")


if __name__ == "__main__":
    main()
