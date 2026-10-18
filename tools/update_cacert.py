#!/usr/bin/env python3

"""Refreshes third_party/cacert/cacert.pem from https://curl.se/docs/caextract.html

The bundle is embedded into Linux builds (see third_party/CMakeLists.txt).

Usage:
  python tools/update_cacert.py          Download and replace if changed.
  python tools/update_cacert.py --check  Exit 1 if the bundle is outdated.
  python tools/update_cacert.py --commit Replace and commit if changed.
"""

from argparse import ArgumentParser
from hashlib import sha256
from pathlib import Path
from re import search
from subprocess import run
from sys import exit, stderr
from urllib.request import Request, urlopen

CACERT_URL = "https://curl.se/ca/cacert.pem"
CACERT_SHA256_URL = "https://curl.se/ca/cacert.pem.sha256"
REPO_ROOT = Path(__file__).resolve().parent.parent
CACERT_PATH = REPO_ROOT / "third_party" / "cacert" / "cacert.pem"
MIN_CERTIFICATES = 100


def fetch(url):
    request = Request(url, headers={"User-Agent": "xenia-canary-update-cacert"})
    with urlopen(request, timeout=60) as response:
        return response.read()


def mozilla_date(pem):
    match = search(rb"Certificate data from Mozilla as of: ([^\r\n]+)", pem)
    return match.group(1).decode("ascii", "replace").strip() if match else "unknown"


def commit(date):
    # Pathspec limits the commit to the bundle, leaving anything else staged.
    pathspec = str(CACERT_PATH.relative_to(REPO_ROOT).as_posix())
    for command in (["git", "add", "--", pathspec],
                    ["git", "commit",
                     "-m", "[3PP] Update Mozilla CA bundle",
                     "-m", f"Certificate data from Mozilla as of: {date}",
                     "-m", "Source: https://curl.se/docs/caextract.html",
                     "--", pathspec]):
        if run(command, cwd=REPO_ROOT).returncode != 0:
            print(f"Failed to run: {' '.join(command)}", file=stderr)
            return False
    return True


def main():
    parser = ArgumentParser(description=__doc__.splitlines()[0])
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--check", action="store_true",
                      help="Only report whether the bundle is outdated.")
    mode.add_argument("--commit", action="store_true",
                      help="Commit the updated bundle to the current branch.")
    args = parser.parse_args()

    pem = fetch(CACERT_URL)
    expected = fetch(CACERT_SHA256_URL).split()[0].decode("ascii").lower()
    actual = sha256(pem).hexdigest()
    if actual != expected:
        print(f"SHA256 mismatch: expected {expected}, got {actual}", file=stderr)
        return 2

    certificates = pem.count(b"-----BEGIN CERTIFICATE-----")
    if certificates < MIN_CERTIFICATES:
        print(f"Only {certificates} certificates in bundle, refusing to use it",
              file=stderr)
        return 2

    # Compare ignoring line endings, the checkout may have converted them.
    current = CACERT_PATH.read_bytes() if CACERT_PATH.exists() else b""
    changed = current.replace(b"\r\n", b"\n") != pem.replace(b"\r\n", b"\n")
    date = mozilla_date(pem)

    if not changed:
        print(f"CA bundle is up to date (Mozilla data as of {date}).")
        return 0

    print(f"CA bundle is outdated: {mozilla_date(current)} -> {date} "
          f"({certificates} certificates).", flush=True)
    if args.check:
        return 1

    CACERT_PATH.parent.mkdir(parents=True, exist_ok=True)
    CACERT_PATH.write_bytes(pem)
    print(f"Updated {CACERT_PATH}", flush=True)

    if args.commit and not commit(date):
        return 2
    return 0


if __name__ == "__main__":
    exit(main())
