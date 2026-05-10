#!/usr/bin/env python3
"""
Convert Vector BLF files to LogSlayer-friendly NDJSON.

Install:
    python -m pip install vblf

Recommended for faster JSON output:
    python -m pip install orjson

Optional DBC signal decoding:
    python -m pip install cantools

Notes about the design contract:
    - Same public NDJSON schema as the original script: logslayer.blf.v1
    - Same progress prefix on stderr: LOGSLAYER_PROGRESS blf_import <percent>
    - Same CLI options as the original script, plus --compat-vblf for troubleshooting
    - Same import_error records for user-facing failures
    - Fast CAN-only parser is used by default when --include-non-can and
      --include-unknown are not requested. The vblf compatibility path is used
      whenever non-CAN/unknown object emission is requested.
"""

from __future__ import annotations

import argparse
import calendar
import datetime as dt
import hashlib
import json
import os
import struct
import sys
import zlib
from dataclasses import fields, is_dataclass
from enum import Enum, IntFlag
from functools import lru_cache
from pathlib import Path
from typing import Any, BinaryIO, Iterable, Optional

try:  # Optional but strongly recommended for large BLF files.
    import orjson  # type: ignore[import-not-found]
except ImportError:  # pragma: no cover - depends on runtime environment.
    orjson = None  # type: ignore[assignment]


CAN_EXTENDED_ID_FLAG = 0x80000000
CAN_ID_MASK = 0x1FFFFFFF

CAN_DIR_TX_FLAG = 0x01
CAN_REMOTE_FLAG = 0x80

CANFD64_REMOTE_FLAG = 0x0010
CANFD64_BRS_FLAG = 0x2000
CANFD64_ESI_FLAG = 0x4000

SCHEMA = "logslayer.blf.v1"
PROGRESS_PREFIX = "LOGSLAYER_PROGRESS blf_import "

# BLF/vblf binary constants used by the fast CAN-only path.
FILE_SIGNATURE = b"LOGG"
OBJ_SIGNATURE = b"LOBJ"
OBJ_SIGNATURE_SIZE = len(OBJ_SIGNATURE)

OBJ_TYPE_UNKNOWN = 0
OBJ_TYPE_CAN_MESSAGE = 1
OBJ_TYPE_LOG_CONTAINER = 10
OBJ_TYPE_CAN_MESSAGE2 = 86
OBJ_TYPE_CAN_FD_MESSAGE = 100
OBJ_TYPE_CAN_FD_MESSAGE_64 = 101

OBJ_TYPE_NAMES: dict[int, str] = {
    OBJ_TYPE_UNKNOWN: "UNKNOWN",
    OBJ_TYPE_CAN_MESSAGE: "CAN_MESSAGE",
    OBJ_TYPE_LOG_CONTAINER: "LOG_CONTAINER",
    OBJ_TYPE_CAN_MESSAGE2: "CAN_MESSAGE2",
    OBJ_TYPE_CAN_FD_MESSAGE: "CAN_FD_MESSAGE",
    OBJ_TYPE_CAN_FD_MESSAGE_64: "CAN_FD_MESSAGE_64",
}

TIME_TEN_MICS = 0x1
TIME_ONE_NANS = 0x2

# vblf uses native struct strings, but BLF is little-endian and these layouts have
# the same sizes on normal platforms. Explicit little-endian avoids surprises.
FILE_STATISTICS_STRUCT = struct.Struct("<4sIIBBBBQQII32xQ64s")
SYSTEM_TIME_STRUCT = struct.Struct("<HHHHHHHH")
OBJECT_HEADER_BASE_STRUCT = struct.Struct("<4sHHII")
OBJECT_HEADER_STRUCT = struct.Struct("<IHHQ")
OBJECT_HEADER_SIZE = OBJECT_HEADER_BASE_STRUCT.size + OBJECT_HEADER_STRUCT.size
CAN_MESSAGE_STRUCT = struct.Struct("<HBBI8s")
CAN_MESSAGE2_STRUCT = struct.Struct("<HBBI8sIBBH")
CAN_FD_MESSAGE_STRUCT = struct.Struct("<HBBIIBBBBI64sI")
CAN_FD_MESSAGE64_STRUCT = struct.Struct("<BBBBIIIIIIIHBBI")

WRITER_FLUSH_BYTES = 1024 * 1024
DBC_DECODE_CACHE_SIZE = 65_536


class ImportFailure(Exception):
    """User-facing import failure that should be written as an import_error."""


class IsoUtcCache:
    """Fast ISO-UTC formatter for many timestamps clustered in the same second."""

    __slots__ = ("_cache",)

    def __init__(self) -> None:
        self._cache: dict[int, str] = {}

    def format_ns(self, epoch_ns: Optional[int]) -> Optional[str]:
        if epoch_ns is None:
            return None

        seconds, nanoseconds = divmod(int(epoch_ns), 1_000_000_000)
        prefix = self._cache.get(seconds)
        if prefix is None:
            prefix = dt.datetime.fromtimestamp(seconds, tz=dt.timezone.utc).strftime(
                "%Y-%m-%dT%H:%M:%S."
            )
            self._cache[seconds] = prefix

        return f"{prefix}{nanoseconds // 1000:06d}Z"


class CanIdCache:
    """Cache formatted CAN IDs; BLFs usually repeat a small set of IDs."""

    __slots__ = ("_cache",)

    def __init__(self) -> None:
        self._cache: dict[int, tuple[str, int, bool]] = {}

    def get(self, raw_id: int) -> tuple[str, int, bool]:
        raw_id = int(raw_id)
        cached = self._cache.get(raw_id)
        if cached is not None:
            return cached

        is_extended = bool(raw_id & CAN_EXTENDED_ID_FLAG)
        can_id = raw_id & CAN_ID_MASK
        text = f"0x{can_id:08X}" if is_extended else f"0x{can_id:03X}"
        cached = (text, can_id, is_extended)
        self._cache[raw_id] = cached
        return cached


class ObjectTypeNameCache:
    """Cache enum/name conversion for vblf object types."""

    __slots__ = ("_cache",)

    def __init__(self) -> None:
        self._cache: dict[int, str] = {}

    def from_enum_or_value(self, value: Any) -> str | int:
        try:
            int_value = int(value)
        except Exception:
            return enum_or_value(value)

        cached = self._cache.get(int_value)
        if cached is not None:
            return cached

        if isinstance(value, Enum):
            name = value.name
        else:
            name = OBJ_TYPE_NAMES.get(int_value, str(int_value))

        self._cache[int_value] = name
        return name

    def from_raw_int(self, object_type: int) -> str:
        cached = self._cache.get(object_type)
        if cached is not None:
            return cached
        name = OBJ_TYPE_NAMES.get(object_type, "UNKNOWN")
        self._cache[object_type] = name
        return name


class NdjsonWriter:
    """Buffered NDJSON writer with optional orjson acceleration."""

    def __init__(self, out_path: Optional[Path]) -> None:
        self._use_orjson = orjson is not None
        self._owns_fh = out_path is not None
        self._buf = bytearray()

        if self._use_orjson:
            self._fh: Any = sys.stdout.buffer if out_path is None else out_path.open("wb")
        else:
            self._fh = (
                sys.stdout
                if out_path is None
                else out_path.open("w", encoding="utf-8", newline="\n", buffering=WRITER_FLUSH_BYTES)
            )

    def write_event(self, event: dict[str, Any]) -> None:
        if self._use_orjson:
            try:
                encoded = orjson.dumps(event)  # type: ignore[union-attr]
            except TypeError:
                # Defensive fallback for unusual payloads from recognized non-CAN
                # objects or custom DBC values.
                encoded = json.dumps(
                    jsonable(event),
                    ensure_ascii=False,
                    separators=(",", ":"),
                    allow_nan=True,
                ).encode("utf-8")

            self._buf.extend(encoded)
            self._buf.append(0x0A)
            if len(self._buf) >= WRITER_FLUSH_BYTES:
                self.flush()
            return

        self._fh.write(
            json.dumps(
                event,
                ensure_ascii=False,
                separators=(",", ":"),
                allow_nan=True,
            )
        )
        self._fh.write("\n")

    def flush(self) -> None:
        if self._use_orjson and self._buf:
            self._fh.write(self._buf)
            self._buf.clear()
        self._fh.flush()

    def close(self) -> None:
        self.flush()
        if self._owns_fh:
            self._fh.close()


class ProgressReporter:
    __slots__ = ("total_count", "last_percent")

    def __init__(self, total_count: int) -> None:
        self.total_count = max(1, int(total_count or 0))
        self.last_percent = -1

    def report(self, percent: int) -> None:
        percent = max(0, min(100, int(percent)))
        if percent == self.last_percent:
            return
        print(f"{PROGRESS_PREFIX}{percent}", file=sys.stderr, flush=True)
        self.last_percent = percent

    def report_completed(self, completed_count: int) -> None:
        percent = rounded_progress_percent(completed_count, self.total_count)
        self.report(percent)


class DbcDecoder:
    """Small wrapper around cantools with per-frame-ID and payload decode caches."""

    def __init__(self, dbc_db: Any, cache_size: int = DBC_DECODE_CACHE_SIZE) -> None:
        self._db = dbc_db
        self._messages: dict[int, Any | None] = {}

        @lru_cache(maxsize=cache_size)
        def decode_cached(can_id: int, data: bytes) -> dict[str, Any]:
            msg = self._message_for_id(can_id)
            if msg is None:
                raise KeyError(can_id)
            return msg.decode(data, decode_choices=False, scaling=True)

        self._decode_cached = decode_cached

    def _message_for_id(self, can_id: int) -> Any | None:
        cached = self._messages.get(can_id, ...)
        if cached is not ...:
            return cached

        try:
            msg = self._db.get_message_by_frame_id(can_id)
        except KeyError:
            msg = None

        self._messages[can_id] = msg
        return msg

    def apply(self, event: dict[str, Any], can_id: int, data: bytes) -> None:
        msg = self._message_for_id(can_id)
        if msg is None:
            return

        event["dbc_message"] = msg.name
        try:
            event["signals"] = self._decode_cached(int(can_id), bytes(data))
        except Exception as exc:
            event["decode_error"] = str(exc)


def write_event(out_fh: Any, event: dict[str, Any]) -> None:
    """Kept for compatibility with older call-sites/tests."""
    json.dump(event, out_fh, ensure_ascii=False, separators=(",", ":"), allow_nan=True)
    out_fh.write("\n")


def report_progress(percent: int, last_percent: int) -> int:
    """Kept for compatibility with older call-sites/tests."""
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


def system_time_tuple_to_epoch_ns(values: tuple[int, int, int, int, int, int, int, int]) -> Optional[int]:
    try:
        timestamp = dt.datetime(
            values[0],
            values[1],
            values[3],
            values[4],
            values[5],
            values[6],
            values[7] * 1000,
            tzinfo=dt.timezone.utc,
        )
    except ValueError:
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


def timestamp_factor_ns_from_flags(flags: int) -> int:
    if flags & TIME_TEN_MICS:
        return 10_000
    if flags & TIME_ONE_NANS:
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


def clamped_data(raw_data: bytes, valid_len: Any) -> bytes:
    try:
        n = int(valid_len)
    except Exception:
        n = len(raw_data)
    n = max(0, min(n, len(raw_data)))
    return raw_data[:n]


def base_event(
    obj: Any,
    seq: int,
    start_epoch_ns: Optional[int],
    obj_flags_type: Any,
    iso_cache: Optional[IsoUtcCache] = None,
    object_type_cache: Optional[ObjectTypeNameCache] = None,
) -> dict[str, Any]:
    header = obj.header
    base = header.base

    raw_ts = getattr(header, "object_time_stamp", None)
    object_flags = getattr(header, "object_flags", 0)

    ts_rel_ns = None
    ts_epoch_ns = None
    if raw_ts is not None:
        raw_ts = int(raw_ts)
        ts_rel_ns = raw_ts * object_timestamp_factor_ns(object_flags, obj_flags_type)
        if start_epoch_ns is not None:
            ts_epoch_ns = start_epoch_ns + ts_rel_ns

    object_type = (
        object_type_cache.from_enum_or_value(base.object_type)
        if object_type_cache is not None
        else enum_or_value(base.object_type)
    )

    return {
        "schema": SCHEMA,
        "seq": seq,
        "kind": "blf_object",
        "object_type": object_type,
        "object_type_value": int(base.object_type),
        "object_size": int(base.object_size),
        "header_version": int(base.header_version),
        "ts": iso_cache.format_ns(ts_epoch_ns) if iso_cache is not None else epoch_ns_to_iso_utc(ts_epoch_ns),
        "ts_raw": raw_ts,
        "ts_rel_ns": ts_rel_ns,
        "ts_epoch_ns": ts_epoch_ns,
    }


def raw_base_event(
    *,
    seq: int,
    object_type: int,
    object_size: int,
    header_version: int,
    object_flags: int,
    raw_ts: int,
    start_epoch_ns: Optional[int],
    iso_cache: IsoUtcCache,
    object_type_cache: ObjectTypeNameCache,
) -> dict[str, Any]:
    ts_rel_ns = int(raw_ts) * timestamp_factor_ns_from_flags(int(object_flags))
    ts_epoch_ns = None if start_epoch_ns is None else start_epoch_ns + ts_rel_ns

    return {
        "schema": SCHEMA,
        "seq": seq,
        "kind": "blf_object",
        "object_type": object_type_cache.from_raw_int(object_type),
        "object_type_value": int(object_type) if object_type in OBJ_TYPE_NAMES else OBJ_TYPE_UNKNOWN,
        "object_size": int(object_size),
        "header_version": int(header_version),
        "ts": iso_cache.format_ns(ts_epoch_ns),
        "ts_raw": int(raw_ts),
        "ts_rel_ns": ts_rel_ns,
        "ts_epoch_ns": ts_epoch_ns,
    }


def fill_can_fields(
    event: dict[str, Any],
    *,
    raw_id: int,
    channel: int,
    flags: int,
    dlc: int,
    data: bytes,
    is_fd: bool,
    canfd_flags: Optional[int],
    is_tx: bool,
    is_remote: bool,
    bitrate_switch: Optional[bool],
    error_state_indicator: Optional[bool],
    can_id_cache: CanIdCache,
    dbc_decoder: Optional[DbcDecoder],
) -> dict[str, Any]:
    id_text, can_id, is_extended = can_id_cache.get(raw_id)
    channel = int(channel)
    flags = int(flags)
    dlc = int(dlc)

    event.update(
        {
            "channel": channel,
            "channel_zero_based": channel - 1 if channel > 0 else None,
            "id": id_text,
            "id_dec": can_id,
            "id_raw": int(raw_id),
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
        event["canfd_flags"] = int(canfd_flags or 0)
        event["bitrate_switch"] = bitrate_switch
        event["error_state_indicator"] = error_state_indicator

    if dbc_decoder is not None and not is_remote:
        dbc_decoder.apply(event, can_id, data)

    return event


def can_event(
    obj: Any,
    seq: int,
    start_epoch_ns: Optional[int],
    dbc_decoder: Optional[DbcDecoder],
    can_types: tuple[type, ...],
    obj_flags_type: Any,
    iso_cache: IsoUtcCache,
    can_id_cache: CanIdCache,
    object_type_cache: ObjectTypeNameCache,
) -> dict[str, Any]:
    event = base_event(obj, seq, start_epoch_ns, obj_flags_type, iso_cache, object_type_cache)
    event["kind"] = "can_frame"

    CanMessage, CanMessage2, CanFdMessage, CanFdMessage64 = can_types

    if isinstance(obj, CanFdMessage64):
        raw_id = int(obj.frame_id)
        raw_data = bytes(obj.data)
        data = clamped_data(raw_data, obj.valid_data_bytes)
        flags = int(obj.flags)
        canfd_flags = int(getattr(obj, "canfd_flags", flags))
        return fill_can_fields(
            event,
            raw_id=raw_id,
            channel=int(obj.channel),
            flags=flags,
            dlc=int(obj.dlc),
            data=data,
            is_fd=True,
            canfd_flags=canfd_flags,
            is_tx=bool(int(obj.dir)),
            is_remote=bool(canfd_flags & CANFD64_REMOTE_FLAG),
            bitrate_switch=bool(canfd_flags & CANFD64_BRS_FLAG),
            error_state_indicator=bool(canfd_flags & CANFD64_ESI_FLAG),
            can_id_cache=can_id_cache,
            dbc_decoder=dbc_decoder,
        )

    if isinstance(obj, CanFdMessage):
        raw_id = int(obj.frame_id)
        raw_data = bytes(obj.data)
        data = clamped_data(raw_data, obj.valid_data_bytes)
        flags = int(obj.flags)
        canfd_flags = int(obj.canfd_flags)
        return fill_can_fields(
            event,
            raw_id=raw_id,
            channel=int(obj.channel),
            flags=flags,
            dlc=int(obj.dlc),
            data=data,
            is_fd=True,
            canfd_flags=canfd_flags,
            # Preserve the original v1 behavior: CanFdMessage BRS/ESI fields are
            # emitted as null. Only CanFdMessage64 populated these booleans.
            is_tx=bool(flags & CAN_DIR_TX_FLAG),
            is_remote=bool(flags & CAN_REMOTE_FLAG),
            bitrate_switch=None,
            error_state_indicator=None,
            can_id_cache=can_id_cache,
            dbc_decoder=dbc_decoder,
        )

    # Classic CAN: CanMessage or CanMessage2.
    raw_id = int(obj.frame_id)
    raw_data = bytes(obj.data)
    dlc = int(obj.dlc)
    data = clamped_data(raw_data, dlc)
    flags = int(obj.flags)
    return fill_can_fields(
        event,
        raw_id=raw_id,
        channel=int(obj.channel),
        flags=flags,
        dlc=dlc,
        data=data,
        is_fd=False,
        canfd_flags=None,
        is_tx=bool(flags & CAN_DIR_TX_FLAG),
        is_remote=bool(flags & CAN_REMOTE_FLAG),
        bitrate_switch=None,
        error_state_indicator=None,
        can_id_cache=can_id_cache,
        dbc_decoder=dbc_decoder,
    )


def unknown_event(
    obj: Any,
    seq: int,
    start_epoch_ns: Optional[int],
    include_raw_hex: bool,
    obj_flags_type: Any,
    iso_cache: IsoUtcCache,
    object_type_cache: ObjectTypeNameCache,
) -> dict[str, Any]:
    event = base_event(obj, seq, start_epoch_ns, obj_flags_type, iso_cache, object_type_cache)
    event["kind"] = "blf_object_unimplemented"

    raw = bytes(obj.buffer)
    event["raw_len"] = len(raw)
    event["raw_sha256"] = hashlib.sha256(raw).hexdigest()
    if include_raw_hex:
        event["raw_hex"] = raw.hex().upper()

    return event


def generic_event(
    obj: Any,
    seq: int,
    start_epoch_ns: Optional[int],
    obj_flags_type: Any,
    iso_cache: IsoUtcCache,
    object_type_cache: ObjectTypeNameCache,
) -> dict[str, Any]:
    event = base_event(obj, seq, start_epoch_ns, obj_flags_type, iso_cache, object_type_cache)
    event["payload"] = jsonable(obj)
    return event


def raw_can_event_from_object_buffer(
    obj_buf: bytes,
    *,
    seq: int,
    object_type: int,
    object_size: int,
    header_version: int,
    object_flags: int,
    raw_ts: int,
    start_epoch_ns: Optional[int],
    dbc_decoder: Optional[DbcDecoder],
    iso_cache: IsoUtcCache,
    can_id_cache: CanIdCache,
    object_type_cache: ObjectTypeNameCache,
) -> Optional[dict[str, Any]]:
    event = raw_base_event(
        seq=seq,
        object_type=object_type,
        object_size=object_size,
        header_version=header_version,
        object_flags=object_flags,
        raw_ts=raw_ts,
        start_epoch_ns=start_epoch_ns,
        iso_cache=iso_cache,
        object_type_cache=object_type_cache,
    )
    event["kind"] = "can_frame"

    pos = OBJECT_HEADER_SIZE

    if object_type == OBJ_TYPE_CAN_MESSAGE:
        if len(obj_buf) < pos + CAN_MESSAGE_STRUCT.size:
            return None
        channel, flags, dlc, raw_id, raw_data = CAN_MESSAGE_STRUCT.unpack_from(obj_buf, pos)
        data = clamped_data(raw_data, dlc)
        return fill_can_fields(
            event,
            raw_id=raw_id,
            channel=channel,
            flags=flags,
            dlc=dlc,
            data=data,
            is_fd=False,
            canfd_flags=None,
            is_tx=bool(flags & CAN_DIR_TX_FLAG),
            is_remote=bool(flags & CAN_REMOTE_FLAG),
            bitrate_switch=None,
            error_state_indicator=None,
            can_id_cache=can_id_cache,
            dbc_decoder=dbc_decoder,
        )

    if object_type == OBJ_TYPE_CAN_MESSAGE2:
        if len(obj_buf) < pos + CAN_MESSAGE2_STRUCT.size:
            return None
        channel, flags, dlc, raw_id, raw_data, _frame_length, _bit_count, _reserved1, _reserved2 = (
            CAN_MESSAGE2_STRUCT.unpack_from(obj_buf, pos)
        )
        data = clamped_data(raw_data, dlc)
        return fill_can_fields(
            event,
            raw_id=raw_id,
            channel=channel,
            flags=flags,
            dlc=dlc,
            data=data,
            is_fd=False,
            canfd_flags=None,
            is_tx=bool(flags & CAN_DIR_TX_FLAG),
            is_remote=bool(flags & CAN_REMOTE_FLAG),
            bitrate_switch=None,
            error_state_indicator=None,
            can_id_cache=can_id_cache,
            dbc_decoder=dbc_decoder,
        )

    if object_type == OBJ_TYPE_CAN_FD_MESSAGE:
        if len(obj_buf) < pos + CAN_FD_MESSAGE_STRUCT.size:
            return None
        (
            channel,
            flags,
            dlc,
            raw_id,
            _frame_length,
            _arb_bit_count,
            canfd_flags,
            valid_data_bytes,
            _reserved1,
            _reserved2,
            raw_data,
            _reserved3,
        ) = CAN_FD_MESSAGE_STRUCT.unpack_from(obj_buf, pos)
        data = clamped_data(raw_data, valid_data_bytes)
        return fill_can_fields(
            event,
            raw_id=raw_id,
            channel=channel,
            flags=flags,
            dlc=dlc,
            data=data,
            is_fd=True,
            canfd_flags=canfd_flags,
            # Preserve original v1 behavior for CanFdMessage.
            is_tx=bool(flags & CAN_DIR_TX_FLAG),
            is_remote=bool(flags & CAN_REMOTE_FLAG),
            bitrate_switch=None,
            error_state_indicator=None,
            can_id_cache=can_id_cache,
            dbc_decoder=dbc_decoder,
        )

    if object_type == OBJ_TYPE_CAN_FD_MESSAGE_64:
        if len(obj_buf) < pos + CAN_FD_MESSAGE64_STRUCT.size:
            return None
        (
            channel,
            dlc,
            valid_data_bytes,
            _tx_count,
            raw_id,
            _frame_length,
            flags,
            _btr_cfg_arb,
            _btr_cfg_data,
            _time_offset_brs_ns,
            _time_offset_crc_del_ns,
            _bit_count,
            direction,
            ext_data_offset,
            _crc,
        ) = CAN_FD_MESSAGE64_STRUCT.unpack_from(obj_buf, pos)
        data_offset = OBJECT_HEADER_SIZE + CAN_FD_MESSAGE64_STRUCT.size
        data_end = (ext_data_offset or object_size)
        raw_data = obj_buf[data_offset:data_end]
        data = clamped_data(raw_data, valid_data_bytes)
        return fill_can_fields(
            event,
            raw_id=raw_id,
            channel=channel,
            flags=int(flags),
            dlc=dlc,
            data=data,
            is_fd=True,
            canfd_flags=int(flags),
            is_tx=bool(direction),
            is_remote=bool(flags & CANFD64_REMOTE_FLAG),
            bitrate_switch=bool(flags & CANFD64_BRS_FLAG),
            error_state_indicator=bool(flags & CANFD64_ESI_FLAG),
            can_id_cache=can_id_cache,
            dbc_decoder=dbc_decoder,
        )

    return None


def parse_file_statistics_header(fh: BinaryIO) -> tuple[Optional[int], int, int]:
    header = fh.read(FILE_STATISTICS_STRUCT.size)
    if len(header) < FILE_STATISTICS_STRUCT.size or not header.startswith(FILE_SIGNATURE):
        raise ImportFailure("Unexpected file format")

    unpacked = FILE_STATISTICS_STRUCT.unpack(header)
    compression_level = int(unpacked[4])
    object_count = int(unpacked[9])
    start_system_time = SYSTEM_TIME_STRUCT.unpack_from(header, 40)
    start_epoch_ns = system_time_tuple_to_epoch_ns(start_system_time)
    return start_epoch_ns, object_count, compression_level


def iter_top_level_objects(fh: BinaryIO) -> Iterable[bytes]:
    """Yield complete top-level object buffers, scanning over padding bytes."""
    while True:
        signature = fh.read(OBJ_SIGNATURE_SIZE)
        if len(signature) != OBJ_SIGNATURE_SIZE:
            return
        if signature != OBJ_SIGNATURE:
            fh.seek(1 - OBJ_SIGNATURE_SIZE, os.SEEK_CUR)
            continue

        rest = fh.read(OBJECT_HEADER_BASE_STRUCT.size - OBJ_SIGNATURE_SIZE)
        if len(rest) < OBJECT_HEADER_BASE_STRUCT.size - OBJ_SIGNATURE_SIZE:
            return

        header_base = signature + rest
        _signature, _header_size, _header_version, object_size, _object_type = (
            OBJECT_HEADER_BASE_STRUCT.unpack(header_base)
        )
        if object_size < OBJECT_HEADER_BASE_STRUCT.size:
            raise ImportFailure(f"Invalid BLF object size: {object_size}")

        payload = fh.read(object_size - OBJECT_HEADER_BASE_STRUCT.size)
        if len(payload) < object_size - OBJECT_HEADER_BASE_STRUCT.size:
            return

        yield header_base + payload


def parse_inner_objects_buffer(
    data: bytes,
    *,
    seq: int,
    start_epoch_ns: Optional[int],
    dbc_decoder: Optional[DbcDecoder],
    writer: NdjsonWriter,
    progress: ProgressReporter,
    iso_cache: IsoUtcCache,
    can_id_cache: CanIdCache,
    object_type_cache: ObjectTypeNameCache,
    compression_level: int,
) -> tuple[bytes, int, int]:
    """
    Parse decompressed LogContainer data.

    Returns (tail, next_seq, emitted_count). seq is incremented for every non-container
    object, even when the object is not emitted, matching vblf enumerate(reader).
    """
    pos = 0
    max_pos = len(data)
    emitted = 0

    while pos < max_pos:
        next_sig = data.find(OBJ_SIGNATURE, pos)
        if next_sig < 0:
            return data[pos:], seq, emitted
        pos = next_sig

        if max_pos - pos < OBJECT_HEADER_BASE_STRUCT.size:
            return data[pos:], seq, emitted

        signature, header_size, header_version, object_size, object_type = OBJECT_HEADER_BASE_STRUCT.unpack_from(data, pos)
        if signature != OBJ_SIGNATURE:
            pos += 1
            continue
        if object_size < OBJECT_HEADER_SIZE:
            raise ImportFailure(f"Invalid BLF object size: {object_size}")

        end = pos + object_size
        if end > max_pos:
            return data[pos:], seq, emitted

        if max_pos - pos < OBJECT_HEADER_SIZE:
            return data[pos:], seq, emitted

        object_flags, _client_index, _object_version, raw_ts = OBJECT_HEADER_STRUCT.unpack_from(
            data, pos + OBJECT_HEADER_BASE_STRUCT.size
        )
        obj_buf = data[pos:end]

        if object_type == OBJ_TYPE_LOG_CONTAINER:
            container_payload = obj_buf[OBJECT_HEADER_SIZE:]
            nested_data = zlib.decompress(container_payload) if compression_level > 0 else container_payload
            nested_tail, seq, nested_emitted = parse_inner_objects_buffer(
                nested_data,
                seq=seq,
                start_epoch_ns=start_epoch_ns,
                dbc_decoder=dbc_decoder,
                writer=writer,
                progress=progress,
                iso_cache=iso_cache,
                can_id_cache=can_id_cache,
                object_type_cache=object_type_cache,
                compression_level=compression_level,
            )
            # Nested containers are unexpected in normal BLF files. If one leaves
            # a tail, keep it local rather than poisoning the outer stream.
            if nested_tail.strip(b"\x00"):
                raise ImportFailure("Incomplete nested LogContainer data")
            emitted += nested_emitted
        else:
            if object_type in (
                OBJ_TYPE_CAN_MESSAGE,
                OBJ_TYPE_CAN_MESSAGE2,
                OBJ_TYPE_CAN_FD_MESSAGE,
                OBJ_TYPE_CAN_FD_MESSAGE_64,
            ):
                event = raw_can_event_from_object_buffer(
                    obj_buf,
                    seq=seq,
                    object_type=object_type,
                    object_size=object_size,
                    header_version=header_version,
                    object_flags=object_flags,
                    raw_ts=raw_ts,
                    start_epoch_ns=start_epoch_ns,
                    dbc_decoder=dbc_decoder,
                    iso_cache=iso_cache,
                    can_id_cache=can_id_cache,
                    object_type_cache=object_type_cache,
                )
                if event is not None:
                    writer.write_event(event)
                    emitted += 1

            seq += 1
            progress.report_completed(seq)

        pos = end

    return b"", seq, emitted


def main_fast_can_only(
    args: argparse.Namespace,
    writer: NdjsonWriter,
    dbc_decoder: Optional[DbcDecoder],
) -> int:
    iso_cache = IsoUtcCache()
    can_id_cache = CanIdCache()
    object_type_cache = ObjectTypeNameCache()
    emitted_count = 0
    seq = 0
    tail = b""

    with args.input.open("rb") as fh:
        start_epoch_ns, total_objects, compression_level = parse_file_statistics_header(fh)
        progress = ProgressReporter(total_objects)
        progress.report(0)

        for obj_buf in iter_top_level_objects(fh):
            if len(obj_buf) < OBJECT_HEADER_SIZE:
                continue

            signature, header_size, header_version, object_size, object_type = OBJECT_HEADER_BASE_STRUCT.unpack_from(obj_buf, 0)
            object_flags, _client_index, _object_version, raw_ts = OBJECT_HEADER_STRUCT.unpack_from(
                obj_buf, OBJECT_HEADER_BASE_STRUCT.size
            )

            if object_type == OBJ_TYPE_LOG_CONTAINER:
                container_payload = obj_buf[OBJECT_HEADER_SIZE:]
                uncompressed = zlib.decompress(container_payload) if compression_level > 0 else container_payload
                tail, seq, new_emitted = parse_inner_objects_buffer(
                    tail + uncompressed,
                    seq=seq,
                    start_epoch_ns=start_epoch_ns,
                    dbc_decoder=dbc_decoder,
                    writer=writer,
                    progress=progress,
                    iso_cache=iso_cache,
                    can_id_cache=can_id_cache,
                    object_type_cache=object_type_cache,
                    compression_level=compression_level,
                )
                emitted_count += new_emitted
                continue

            # Top-level non-container object. Rare, but vblf would yield it.
            if object_type in (
                OBJ_TYPE_CAN_MESSAGE,
                OBJ_TYPE_CAN_MESSAGE2,
                OBJ_TYPE_CAN_FD_MESSAGE,
                OBJ_TYPE_CAN_FD_MESSAGE_64,
            ):
                event = raw_can_event_from_object_buffer(
                    obj_buf,
                    seq=seq,
                    object_type=object_type,
                    object_size=object_size,
                    header_version=header_version,
                    object_flags=object_flags,
                    raw_ts=raw_ts,
                    start_epoch_ns=start_epoch_ns,
                    dbc_decoder=dbc_decoder,
                    iso_cache=iso_cache,
                    can_id_cache=can_id_cache,
                    object_type_cache=object_type_cache,
                )
                if event is not None:
                    writer.write_event(event)
                    emitted_count += 1

            seq += 1
            progress.report_completed(seq)

        progress.report(100)

    print(f"wrote {emitted_count} events", file=sys.stderr)
    return 0


def main_vblf_compat(
    args: argparse.Namespace,
    writer: NdjsonWriter,
    dbc_decoder: Optional[DbcDecoder],
) -> int:
    try:
        from vblf.can import CanFdMessage, CanFdMessage64, CanMessage, CanMessage2
        from vblf.constants import ObjFlags
        from vblf.general import NotImplementedObject
        from vblf.reader import BlfReader
    except ImportError:
        writer.write_event(
            import_error(
                "Python package vblf is required. Install it with: python -m pip install vblf",
                args.input,
            )
        )
        return 1

    can_types = (CanMessage, CanMessage2, CanFdMessage, CanFdMessage64)
    iso_cache = IsoUtcCache()
    can_id_cache = CanIdCache()
    object_type_cache = ObjectTypeNameCache()
    count = 0

    try:
        with BlfReader(args.input) as reader:
            start_epoch_ns = system_time_to_epoch_ns(reader.file_statistics.measurement_start_time)
            total_objects = max(1, int(getattr(reader.file_statistics, "object_count", 0) or 0))
            progress = ProgressReporter(total_objects)
            progress.report(0)

            for seq, obj in enumerate(reader):
                progress.report_completed(seq + 1)

                if isinstance(obj, can_types):
                    event = can_event(
                        obj,
                        seq,
                        start_epoch_ns,
                        dbc_decoder,
                        can_types,
                        ObjFlags,
                        iso_cache,
                        can_id_cache,
                        object_type_cache,
                    )
                elif isinstance(obj, NotImplementedObject):
                    if not args.include_unknown:
                        continue
                    event = unknown_event(
                        obj,
                        seq,
                        start_epoch_ns,
                        args.raw_unknown,
                        ObjFlags,
                        iso_cache,
                        object_type_cache,
                    )
                else:
                    if not args.include_non_can:
                        continue
                    event = generic_event(
                        obj,
                        seq,
                        start_epoch_ns,
                        ObjFlags,
                        iso_cache,
                        object_type_cache,
                    )

                writer.write_event(event)
                count += 1

            progress.report(100)
    except Exception as exc:
        writer.write_event(import_error(f"Failed to import BLF: {exc}", args.input))
        return 1

    print(f"wrote {count} events", file=sys.stderr)
    return 0


def load_dbc_decoder(args: argparse.Namespace, writer: NdjsonWriter) -> Optional[DbcDecoder]:
    if args.dbc is None:
        return None

    try:
        import cantools  # type: ignore[import-not-found]
    except ImportError:
        writer.write_event(
            import_error(
                "Python package cantools is required for --dbc. Install it with: python -m pip install cantools",
                args.input,
            )
        )
        raise SystemExit(1)

    try:
        dbc_db = cantools.database.load_file(str(args.dbc))
    except Exception as exc:
        writer.write_event(import_error(f"Failed to load DBC file {args.dbc}: {exc}", args.input))
        raise SystemExit(1)

    return DbcDecoder(dbc_db)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Convert Vector BLF to LogSlayer-friendly NDJSON.")
    parser.add_argument("input", type=Path, help="Input .blf file")
    parser.add_argument("-o", "--out", type=Path, default=None, help="Output .jsonl file. Defaults to stdout.")
    parser.add_argument("--dbc", type=Path, default=None, help="Optional DBC file for signal decoding.")
    parser.add_argument("--include-non-can", action="store_true", help="Also emit recognized non-CAN BLF objects.")
    parser.add_argument("--include-unknown", action="store_true", help="Emit unsupported BLF objects as metadata records.")
    parser.add_argument("--raw-unknown", action="store_true", help="Include full raw hex for unsupported objects. Can make output huge.")
    parser.add_argument(
        "--compat-vblf",
        action="store_true",
        help="Force the vblf compatibility reader instead of the fast CAN-only parser.",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    writer = NdjsonWriter(args.out)

    try:
        dbc_decoder = load_dbc_decoder(args, writer)

        use_fast_can_only = not (
            args.compat_vblf
            or args.include_non_can
            or args.include_unknown
        )

        if use_fast_can_only:
            try:
                return main_fast_can_only(args, writer, dbc_decoder)
            except ImportFailure as exc:
                writer.write_event(import_error(f"Failed to import BLF: {exc}", args.input))
                return 1
            except Exception as exc:
                writer.write_event(import_error(f"Failed to import BLF: {exc}", args.input))
                return 1

        return main_vblf_compat(args, writer, dbc_decoder)

    finally:
        writer.close()


if __name__ == "__main__":
    raise SystemExit(main())
