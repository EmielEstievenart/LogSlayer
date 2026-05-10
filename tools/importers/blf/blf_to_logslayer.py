#!/usr/bin/env python3
"""
Convert Vector BLF files to LogSlayer-friendly NDJSON.

Install:
    python -m pip install vblf

Optional DBC signal decoding:
    python -m pip install cantools
"""

from __future__ import annotations

import argparse
import calendar
import datetime as dt
import hashlib
import json
import sys
from dataclasses import fields, is_dataclass
from enum import Enum, IntFlag
from pathlib import Path
from typing import Any, Optional


CAN_EXTENDED_ID_FLAG = 0x80000000
CAN_ID_MASK = 0x1FFFFFFF

CAN_DIR_TX_FLAG = 0x01
CAN_REMOTE_FLAG = 0x80

CANFD64_REMOTE_FLAG = 0x0010
CANFD64_BRS_FLAG = 0x2000
CANFD64_ESI_FLAG = 0x4000

SCHEMA = "logslayer.blf.v1"
PROGRESS_PREFIX = "LOGSLAYER_PROGRESS blf_import "


def write_event(out_fh: Any, event: dict[str, Any]) -> None:
    json.dump(event, out_fh, ensure_ascii=False, separators=(",", ":"))
    out_fh.write("\n")


def report_progress(percent: int, last_percent: int) -> int:
    percent = max(0, min(100, percent))
    if percent == last_percent:
        return last_percent

    print(f"{PROGRESS_PREFIX}{percent}", file=sys.stderr, flush=True)
    return percent


def rounded_progress_percent(completed_count: int, total_count: int) -> int:
    if total_count <= 0:
        return 100

    return ((completed_count * 100) + (total_count // 2)) // total_count


def import_error(message: str, source: Optional[Path] = None) -> dict[str, Any]:
    event: dict[str, Any] = {
        "schema": SCHEMA,
        "kind": "import_error",
        "ts": None,
        "message": message,
    }
    if source is not None:
        event["source"] = str(source)
    return event


def system_time_to_epoch_ns(value: Any) -> Optional[int]:
    try:
        timestamp = dt.datetime(
            value.year,
            value.month,
            value.day,
            value.hour,
            value.minute,
            value.second,
            value.milliseconds * 1000,
            tzinfo=dt.timezone.utc,
        )
    except (AttributeError, ValueError):
        return None

    return calendar.timegm(timestamp.utctimetuple()) * 1_000_000_000 + timestamp.microsecond * 1000


def epoch_ns_to_iso_utc(epoch_ns: Optional[int]) -> Optional[str]:
    if epoch_ns is None:
        return None

    seconds, nanoseconds = divmod(epoch_ns, 1_000_000_000)
    timestamp = dt.datetime.fromtimestamp(seconds, tz=dt.timezone.utc)
    timestamp = timestamp.replace(microsecond=nanoseconds // 1000)
    return timestamp.strftime("%Y-%m-%dT%H:%M:%S.%fZ")


def object_timestamp_factor_ns(object_flags: Any, obj_flags_type: Any) -> int:
    flags = int(object_flags)

    if flags & int(obj_flags_type.TIME_TEN_MICS):
        return 10_000

    if flags & int(obj_flags_type.TIME_ONE_NANS):
        return 1

    return 1


def enum_or_value(value: Any) -> Any:
    if isinstance(value, Enum):
        return value.name
    if isinstance(value, IntFlag):
        return int(value)
    return value


def jsonable(value: Any) -> Any:
    if isinstance(value, bytes):
        return value.hex().upper()

    if isinstance(value, Enum):
        return value.name

    if isinstance(value, IntFlag):
        return int(value)

    if is_dataclass(value):
        return {field.name: jsonable(getattr(value, field.name)) for field in fields(value)}

    if isinstance(value, (list, tuple)):
        return [jsonable(item) for item in value]

    if isinstance(value, dict):
        return {str(key): jsonable(item) for key, item in value.items()}

    return value


def get_first_attr(obj: Any, *names: str, default: Any = None) -> Any:
    for name in names:
        if hasattr(obj, name):
            return getattr(obj, name)
    return default


def base_event(obj: Any, seq: int, start_epoch_ns: Optional[int], obj_flags_type: Any) -> dict[str, Any]:
    header = obj.header
    base = header.base

    raw_ts = getattr(header, "object_time_stamp", None)
    object_flags = getattr(header, "object_flags", 0)

    ts_rel_ns = None
    ts_epoch_ns = None
    if raw_ts is not None:
        ts_rel_ns = int(raw_ts) * object_timestamp_factor_ns(object_flags, obj_flags_type)
        if start_epoch_ns is not None:
            ts_epoch_ns = start_epoch_ns + ts_rel_ns

    return {
        "schema": SCHEMA,
        "seq": seq,
        "kind": "blf_object",
        "object_type": enum_or_value(base.object_type),
        "object_type_value": int(base.object_type),
        "object_size": int(base.object_size),
        "header_version": int(base.header_version),
        "ts": epoch_ns_to_iso_utc(ts_epoch_ns),
        "ts_raw": raw_ts,
        "ts_rel_ns": ts_rel_ns,
        "ts_epoch_ns": ts_epoch_ns,
    }


def can_event(obj: Any, seq: int, start_epoch_ns: Optional[int], dbc_db: Any, can_types: tuple[type, ...], obj_flags_type: Any) -> dict[str, Any]:
    event = base_event(obj, seq, start_epoch_ns, obj_flags_type)
    event["kind"] = "can_frame"

    raw_id = int(get_first_attr(obj, "frame_id", "id", default=0))
    is_extended = bool(raw_id & CAN_EXTENDED_ID_FLAG)
    can_id = raw_id & CAN_ID_MASK

    raw_data = bytes(getattr(obj, "data", b""))
    dlc = int(getattr(obj, "dlc", len(raw_data)))

    valid_len = get_first_attr(obj, "valid_data_bytes", default=None)
    if valid_len is None:
        valid_len = dlc

    valid_len = max(0, min(int(valid_len), len(raw_data)))
    data = raw_data[:valid_len]

    channel = int(getattr(obj, "channel", 0))
    is_fd = isinstance(obj, can_types[2:])

    flags = int(getattr(obj, "flags", 0))
    canfd_flags = int(get_first_attr(obj, "canfd_flags", "flags", default=0))

    if isinstance(obj, can_types[3]):
        direction_raw = int(getattr(obj, "dir", 0))
        is_tx = bool(direction_raw)
        is_remote = bool(canfd_flags & CANFD64_REMOTE_FLAG)
        bitrate_switch = bool(canfd_flags & CANFD64_BRS_FLAG)
        error_state_indicator = bool(canfd_flags & CANFD64_ESI_FLAG)
    else:
        is_tx = bool(flags & CAN_DIR_TX_FLAG)
        is_remote = bool(flags & CAN_REMOTE_FLAG)
        bitrate_switch = None
        error_state_indicator = None

    event.update(
        {
            "channel": channel,
            "channel_zero_based": channel - 1 if channel > 0 else None,
            "id": f"0x{can_id:08X}" if is_extended else f"0x{can_id:03X}",
            "id_dec": can_id,
            "id_raw": raw_id,
            "is_extended_id": is_extended,
            "is_fd": is_fd,
            "is_remote_frame": is_remote,
            "direction": "tx" if is_tx else "rx",
            "dlc": dlc,
            "data_len": len(data),
            "data": data.hex().upper(),
            "flags": flags,
        }
    )

    if is_fd:
        event["canfd_flags"] = canfd_flags
        event["bitrate_switch"] = bitrate_switch
        event["error_state_indicator"] = error_state_indicator

    if dbc_db is not None and not is_remote:
        try:
            msg_def = dbc_db.get_message_by_frame_id(can_id)
            event["dbc_message"] = msg_def.name
            event["signals"] = dbc_db.decode_message(can_id, data, decode_choices=False, scaling=True)
        except KeyError:
            pass
        except Exception as exc:
            event["decode_error"] = str(exc)

    return event


def unknown_event(obj: Any, seq: int, start_epoch_ns: Optional[int], include_raw_hex: bool, obj_flags_type: Any) -> dict[str, Any]:
    event = base_event(obj, seq, start_epoch_ns, obj_flags_type)
    event["kind"] = "blf_object_unimplemented"

    raw = obj.buffer
    event["raw_len"] = len(raw)
    event["raw_sha256"] = hashlib.sha256(raw).hexdigest()
    if include_raw_hex:
        event["raw_hex"] = raw.hex().upper()

    return event


def generic_event(obj: Any, seq: int, start_epoch_ns: Optional[int], obj_flags_type: Any) -> dict[str, Any]:
    event = base_event(obj, seq, start_epoch_ns, obj_flags_type)
    event["payload"] = jsonable(obj)
    return event


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Convert Vector BLF to LogSlayer-friendly NDJSON.")
    parser.add_argument("input", type=Path, help="Input .blf file")
    parser.add_argument("-o", "--out", type=Path, default=None, help="Output .jsonl file. Defaults to stdout.")
    parser.add_argument("--dbc", type=Path, default=None, help="Optional DBC file for signal decoding.")
    parser.add_argument("--include-non-can", action="store_true", help="Also emit recognized non-CAN BLF objects.")
    parser.add_argument("--include-unknown", action="store_true", help="Emit unsupported BLF objects as metadata records.")
    parser.add_argument("--raw-unknown", action="store_true", help="Include full raw hex for unsupported objects. Can make output huge.")
    return parser.parse_args()


def main() -> int:
    args = parse_args()

    out_fh = args.out.open("w", encoding="utf-8", newline="\n") if args.out is not None else sys.stdout
    count = 0

    try:
        try:
            from vblf.can import CanFdMessage, CanFdMessage64, CanMessage, CanMessage2
            from vblf.constants import ObjFlags
            from vblf.general import NotImplementedObject
            from vblf.reader import BlfReader
        except ImportError:
            write_event(out_fh, import_error("Python package vblf is required. Install it with: python -m pip install vblf", args.input))
            return 1

        dbc_db = None
        if args.dbc is not None:
            try:
                import cantools
            except ImportError:
                write_event(out_fh, import_error("Python package cantools is required for --dbc. Install it with: python -m pip install cantools", args.input))
                return 1

            try:
                dbc_db = cantools.database.load_file(str(args.dbc))
            except Exception as exc:
                write_event(out_fh, import_error(f"Failed to load DBC file {args.dbc}: {exc}", args.input))
                return 1

        can_types = (CanMessage, CanMessage2, CanFdMessage, CanFdMessage64)

        try:
            with BlfReader(args.input) as reader:
                start_epoch_ns = system_time_to_epoch_ns(reader.file_statistics.measurement_start_time)
                total_objects = max(1, int(getattr(reader.file_statistics, "object_count", 0) or 0))
                last_progress = report_progress(0, -1)

                for seq, obj in enumerate(reader):
                    last_progress = report_progress(rounded_progress_percent(seq + 1, total_objects), last_progress)
                    if isinstance(obj, can_types):
                        event = can_event(obj, seq, start_epoch_ns, dbc_db, can_types, ObjFlags)
                    elif isinstance(obj, NotImplementedObject):
                        if not args.include_unknown:
                            continue
                        event = unknown_event(obj, seq, start_epoch_ns, args.raw_unknown, ObjFlags)
                    else:
                        if not args.include_non_can:
                            continue
                        event = generic_event(obj, seq, start_epoch_ns, ObjFlags)

                    write_event(out_fh, event)
                    count += 1
                report_progress(100, last_progress)
        except Exception as exc:
            write_event(out_fh, import_error(f"Failed to import BLF: {exc}", args.input))
            return 1

    finally:
        if out_fh is not sys.stdout:
            out_fh.close()

    print(f"wrote {count} events", file=sys.stderr)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
