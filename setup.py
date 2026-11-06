#!/usr/bin/env python3
"""Prepare a local Muse Gadget SDK checkout and apply the ESP-VoCat board patch.

This repository is an overlay, not a fork. It ships only the new board files
and an anchor-based patch (apply.py). This script obtains the upstream SDK at
a pinned commit into vendor/ (gitignored), then applies the patch to that copy.

Usage:
    python setup.py                       # clone pinned upstream + apply patch
    python setup.py --sdk-dir PATH        # use an existing SDK checkout instead
    python setup.py --check-only          # only verify that anchors still match
    python setup.py --mirror URL          # custom clone URL (e.g. a GitHub mirror)

Console output is intentionally ASCII-only so it stays safe when redirected on
Chinese Windows (GBK code page).
"""
from __future__ import annotations

import argparse
import shutil
import subprocess
import sys
from pathlib import Path

sys.stdout.reconfigure(encoding="utf-8", errors="replace")
sys.stderr.reconfigure(encoding="utf-8", errors="replace")

ROOT = Path(__file__).resolve().parent
UPSTREAM_URL = "https://github.com/facebookincubator/muse-gadget-sdk.git"
MIRROR_FALLBACK = ("https://gh-proxy.com/https://github.com/"
                   "facebookincubator/muse-gadget-sdk.git")
PINNED_COMMIT = "463bd2a05380fea4211b0099ad0f70933c108b20"
DEFAULT_VENDOR = ROOT / "vendor" / "muse-gadget-sdk"


def run(cmd, *, capture=False):
    print("+ " + " ".join(str(c) for c in cmd), flush=True)
    argv = [str(c) for c in cmd]
    if capture:
        # Capture bytes only; never use text=True (PowerShell / Windows output
        # may not be UTF-8 and can blow up in a reader thread).
        return subprocess.run(argv, check=True, stdout=subprocess.PIPE).stdout
    subprocess.run(argv, check=True)
    return b""


def git_output(args):
    return run(["git", *args], capture=True).decode("utf-8", "replace").strip()


def looks_like_sdk(root: Path) -> bool:
    return (
        (root / "esp32" / "components" / "muse" / "Kconfig").is_file()
        or (root / "components" / "muse" / "Kconfig").is_file()
    )


def cleanup_vendor(dest: Path) -> None:
    """Remove a failed clone. Only ever touches <repo>/vendor/<name>."""
    if dest.parent == ROOT / "vendor" and dest.exists():
        shutil.rmtree(dest)
        print(f"Cleaned up incomplete checkout: {dest}")


def clone_with_fallback(dest: Path, url: str, fallback) -> None:
    candidates = [url] + ([fallback] if fallback and fallback != url else [])
    for candidate in candidates:
        try:
            # HTTP/1.1 avoids the schannel close_notify / early EOF failure that
            # some Windows proxies trigger on HTTP/2 git smart transfers.
            run(["git", "-c", "http.version=HTTP/1.1", "clone", candidate, str(dest)])
            return
        except subprocess.CalledProcessError:
            cleanup_vendor(dest)
            if candidate != candidates[-1]:
                print("Clone failed; retrying through the GitHub mirror...\n")
    print("ERROR: could not clone the upstream SDK.\n"
          "Check your git proxy (http.proxy=http://127.0.0.1:12334) or pass "
          "--mirror <reachable-url>.", file=sys.stderr)
    raise SystemExit(3)


def prepare_vendor(dest: Path, url: str, pin: str, fallback=None) -> Path:
    if dest.exists() and not (dest / ".git").exists():
        if any(dest.iterdir()):
            print(f"ERROR: {dest} exists but is not a git checkout. Remove it and retry.",
                  file=sys.stderr)
            raise SystemExit(2)
        cleanup_vendor(dest)  # empty leftover directory from an earlier failure

    if (dest / ".git").exists():
        head = git_output(["-C", str(dest), "rev-parse", "HEAD"])
        print(f"Existing checkout: {dest} @ {head[:12]}")
        if head != pin:
            try:
                git_output(["-C", str(dest), "cat-file", "-e", pin + "^{commit}"])
            except subprocess.CalledProcessError:
                run(["git", "-C", str(dest), "fetch", "origin", pin])
            run(["git", "-C", str(dest), "checkout", "--detach", pin])
        return dest

    dest.parent.mkdir(parents=True, exist_ok=True)
    clone_with_fallback(dest, url, fallback)
    run(["git", "-C", str(dest), "checkout", "--detach", pin])
    return dest


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--sdk-dir", type=Path,
                    help="existing muse-gadget-sdk checkout (repo root or its esp32/ dir)")
    ap.add_argument("--check-only", action="store_true",
                    help="verify patch anchors only, do not modify the checkout")
    ap.add_argument("--mirror", default=UPSTREAM_URL,
                    help="clone URL (default: official GitHub repo)")
    args = ap.parse_args()

    if args.sdk_dir:
        root = args.sdk_dir.resolve()
        if not looks_like_sdk(root):
            print(f"ERROR: {root} does not look like a Muse Gadget SDK checkout "
                  "(missing esp32/components/muse/Kconfig).", file=sys.stderr)
            return 2
        print(f"Using existing SDK checkout: {root}")
    else:
        fallback = MIRROR_FALLBACK if args.mirror == UPSTREAM_URL else None
        root = prepare_vendor(DEFAULT_VENDOR, args.mirror, PINNED_COMMIT, fallback)

    print(f"\nPinned upstream commit: {PINNED_COMMIT}")
    print("--- step 1/2: verify patch anchors ---")
    run([sys.executable, str(ROOT / "apply.py"), "--sdk", str(root), "--check"])

    if args.check_only:
        print("\nAnchors OK. Nothing was applied.")
        return 0

    print("--- step 2/2: apply patch (idempotent) ---")
    run([sys.executable, str(ROOT / "apply.py"), "--sdk", str(root)])

    esp32 = root / "esp32" if (root / "esp32").is_dir() else root
    print(f"""
Done. Patched SDK checkout: {root}

Next (ESP-IDF v6.0.3 is required):
    cd {esp32}
    tools/muse/board.sh build vocat
    tools/muse/board.sh flash vocat COM27

vendor/ is gitignored. Never push changes to the upstream repository.
""")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
