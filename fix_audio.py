"""
fix_audio.py - re-encode every .ogg under res/audio/ to canonical Vorbis-in-Ogg
so SDL_mixer's stb_vorbis decoder (used by the 3DS port) can read it.

Backs up originals to res/audio_backup/ the first time it runs, so you can
re-run it without losing data.

Run from the project root:
    python fix_audio.py

Flags:
    --dry-run     show what would be done without touching any files
    --bitrate N   target bitrate for the re-encoded files (default 128k)
    --force       re-encode even files that already look fine
"""

from __future__ import annotations

import argparse
import json
import shutil
import subprocess
import sys
from pathlib import Path

AUDIO_DIR    = Path("res/audio")
BACKUP_DIR   = Path("res/audio_backup")
DEFAULT_BR   = "128k"


def die(msg: str, code: int = 1) -> None:
    print(f"error: {msg}", file=sys.stderr)
    sys.exit(code)


def check_ffmpeg() -> None:
    try:
        subprocess.run(
            ["ffmpeg", "-version"],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            check=True,
        )
    except (FileNotFoundError, subprocess.CalledProcessError):
        die(
            "ffmpeg not found on PATH. Install it and try again.\n"
            "  Windows:  winget install Gyan.FFmpeg\n"
            "  Or:       https://www.gyan.dev/ffmpeg/builds/"
        )

    try:
        subprocess.run(
            ["ffprobe", "-version"],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            check=True,
        )
    except (FileNotFoundError, subprocess.CalledProcessError):
        die("ffprobe not found on PATH (usually installed alongside ffmpeg).")


def probe(path: Path) -> dict:
    """Return ffprobe's JSON output for a single file, or {} on failure."""
    try:
        out = subprocess.run(
            [
                "ffprobe", "-v", "error",
                "-show_format", "-show_streams",
                "-print_format", "json",
                str(path),
            ],
            capture_output=True,
            text=True,
            check=True,
        )
        return json.loads(out.stdout or "{}")
    except subprocess.CalledProcessError as e:
        print(f"  ffprobe failed: {e.stderr.strip()}")
        return {}
    except json.JSONDecodeError:
        return {}


def describe(probe_data: dict) -> str:
    """Human-readable summary of the first audio stream."""
    streams = probe_data.get("streams", [])
    if not streams:
        return "(no streams)"

    audio = next((s for s in streams if s.get("codec_type") == "audio"), None)
    if not audio:
        return "(no audio stream)"

    codec      = audio.get("codec_name", "?")
    rate       = audio.get("sample_rate", "?")
    channels   = audio.get("channels", "?")
    container  = probe_data.get("format", {}).get("format_name", "?")
    return f"codec={codec} container={container} rate={rate}Hz channels={channels}"


def is_already_good(probe_data: dict) -> bool:
    """True if the file is already Vorbis-in-Ogg with a sane layout."""
    fmt = probe_data.get("format", {}).get("format_name", "")
    if "ogg" not in fmt:
        return False
    audio = next(
        (s for s in probe_data.get("streams", []) if s.get("codec_type") == "audio"),
        None,
    )
    if not audio:
        return False
    return audio.get("codec_name") == "vorbis"


def reencode(src: Path, dst: Path, bitrate: str) -> bool:
    """
    Re-encode src into a canonical stereo Vorbis-in-Ogg file at dst.
    Returns True on success.
    """
    cmd = [
        "ffmpeg",
        "-hide_banner", "-loglevel", "error",
        "-y",                    # overwrite temp output if a previous run died
        "-i", str(src),
        "-vn",                   # drop any cover art / video stream
        "-map_metadata", "-1",   # drop tags; some decoders trip over them
        "-c:a", "libvorbis",
        "-b:a", bitrate,
        "-ar", "44100",
        "-ac", "2",
        "-f", "ogg",
        str(dst),
    ]
    try:
        subprocess.run(cmd, check=True)
        return True
    except subprocess.CalledProcessError as e:
        print(f"  ffmpeg failed ({e.returncode})")
        return False


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--dry-run", action="store_true",
                    help="show what would happen, don't modify anything")
    ap.add_argument("--bitrate", default=DEFAULT_BR,
                    help=f"Vorbis target bitrate (default {DEFAULT_BR})")
    ap.add_argument("--force", action="store_true",
                    help="re-encode even files that already look correct")
    args = ap.parse_args()

    if not AUDIO_DIR.is_dir():
        die(f"{AUDIO_DIR}/ not found. Run this from the project root.")

    check_ffmpeg()

    oggs = sorted(AUDIO_DIR.rglob("*.ogg"))
    if not oggs:
        print(f"no .ogg files found under {AUDIO_DIR}/")
        return 0

    print(f"found {len(oggs)} .ogg file(s) under {AUDIO_DIR}/\n")

    if not args.dry_run:
        BACKUP_DIR.mkdir(parents=True, exist_ok=True)

    converted = 0
    skipped   = 0
    failed    = []

    for src in oggs:
        rel = src.relative_to(AUDIO_DIR)
        print(f"== {rel}")

        before = probe(src)
        if not before:
            print("  -> unreadable; will still try to re-encode")
        else:
            print(f"  before: {describe(before)}")

        if before and is_already_good(before) and not args.force:
            print("  -> already canonical Vorbis/Ogg, skipping")
            skipped += 1
            continue

        if args.dry_run:
            print("  -> would re-encode")
            continue

        # Back up the original (once).
        backup = BACKUP_DIR / rel
        backup.parent.mkdir(parents=True, exist_ok=True)
        if not backup.exists():
            shutil.copy2(src, backup)
            print(f"  backed up to {backup}")
        else:
            print(f"  backup already exists at {backup}")

        # Re-encode to a temp file next to the original, then swap.
        tmp = src.with_suffix(src.suffix + ".tmp")
        if tmp.exists():
            tmp.unlink()

        if not reencode(src, tmp, args.bitrate):
            failed.append(str(rel))
            if tmp.exists():
                tmp.unlink()
            continue

        after = probe(tmp)
        if not after or not is_already_good(after):
            print(f"  -> re-encoded file is still not canonical: {describe(after)}")
            failed.append(str(rel))
            tmp.unlink()
            continue

        print(f"  after:  {describe(after)}")
        tmp.replace(src)          # atomic-ish swap on both Windows and POSIX
        print("  -> done")
        converted += 1

    print()
    print(f"converted: {converted}")
    print(f"skipped:   {skipped}")
    if failed:
        print(f"failed:    {len(failed)}")
        for f in failed:
            print(f"  - {f}")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())