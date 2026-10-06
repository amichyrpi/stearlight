#!/usr/bin/env python3
"""Decode the OS's original startup assets for the OpenXR loading layer."""
import argparse
import json
from pathlib import Path
import subprocess


def prepare(root, assets):
    output = assets / "boot"
    output.mkdir(parents=True, exist_ok=True)
    clips = {}
    for name, relative in [
        ("startup", "steamui/movies/steamframe_startup_450x450.webm"),
        ("loop", "steamui/images/steam_spinner.png"),
    ]:
        source = root / "opt/steam" / relative
        frames = json.loads(subprocess.check_output([
            "ffprobe", "-v", "error", "-select_streams", "v:0", "-show_frames",
            "-show_entries", "frame=best_effort_timestamp_time,pkt_duration_time",
            "-of", "json", str(source),
        ]))["frames"]
        if not frames:
            raise RuntimeError(f"No animation frames in {source}")
        starts = [float(frame["best_effort_timestamp_time"]) for frame in frames]
        origin = starts[0]
        duration = float(frames[-1].get("pkt_duration_time", "0.016667"))
        subprocess.run([
            "ffmpeg", "-v", "error", "-y", "-i", str(source), "-fps_mode", "passthrough",
            str(output / f"{name}-%04d.png"),
        ], check=True)
        clips[name] = {"starts_ms": [round((t - origin) * 1000) for t in starts],
                       "duration_ms": round((starts[-1] - origin + duration) * 1000)}
        print(f"Quest {name}: {len(frames)} frames, {clips[name]['duration_ms']} ms")
    (output / "clips.json").write_text(json.dumps(clips) + "\n")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--assets", type=Path, required=True)
    args = parser.parse_args()
    prepare(args.root, args.assets)
