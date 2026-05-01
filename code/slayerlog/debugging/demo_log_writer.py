from __future__ import annotations

import os
import signal
import sys
import time
from datetime import datetime
from pathlib import Path

INITIAL_LINE_COUNT = 20


def build_output_paths(primary_output_path: Path) -> list[Path]:
    secondary_name = f"{primary_output_path.stem}_secondary{primary_output_path.suffix}"
    return [
        primary_output_path,
        primary_output_path.with_name(secondary_name),
    ]


def write_demo_line(handle, output_path: Path, count: int) -> None:
    base = f"[{datetime.now().isoformat(timespec='seconds')}] {output_path.name} demo line {count}"
    line = (base + " ") * 3
    handle.write(line + "\n")


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: demo_log_writer.py <output-file>", file=sys.stderr)
        return 1

    primary_output_path = Path(sys.argv[1]).resolve()
    output_paths = build_output_paths(primary_output_path)
    for output_path in output_paths:
        output_path.parent.mkdir(parents=True, exist_ok=True)

    pid_path = primary_output_path.with_suffix(primary_output_path.suffix + ".pid")

    running = True

    def stop_handler(signum: int, frame: object) -> None:
        nonlocal running
        running = False

    signal.signal(signal.SIGINT, stop_handler)
    signal.signal(signal.SIGTERM, stop_handler)

    print("SLAYERLOG_WRITER_STARTING", flush=True)
    pid_path.write_text(str(os.getpid()), encoding="utf-8")

    with (
        output_paths[0].open("w", encoding="utf-8", newline="") as primary_handle,
        output_paths[1].open("w", encoding="utf-8", newline="") as secondary_handle,
    ):
        handles = [
            (primary_handle, output_paths[0]),
            (secondary_handle, output_paths[1]),
        ]

        for handle, output_path in handles:
            handle.write("slayerlog demo writer started\n")
            handle.write(f"monitoring file: {output_path}\n")

        count = 1
        for _ in range(INITIAL_LINE_COUNT):
            for handle, output_path in handles:
                write_demo_line(handle, output_path, count)
            count += 1

        for handle, _ in handles:
            handle.flush()

        print("SLAYERLOG_WRITER_READY", flush=True)
        print(f"primary log: {output_paths[0]}", flush=True)
        print(f"secondary log: {output_paths[1]}", flush=True)

        while running:
            for handle, output_path in handles:
                write_demo_line(handle, output_path, count)
                handle.flush()
                print(f"{output_path.name}: wrote demo line {count}", flush=True)

            count += 1
            time.sleep(1)

    try:
        pid_path.unlink()
    except FileNotFoundError:
        pass

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
