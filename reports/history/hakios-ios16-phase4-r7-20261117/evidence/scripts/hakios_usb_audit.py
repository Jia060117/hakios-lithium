"""Independently audit saved USB frames; never open or command a device.

Transport continuity cannot recover printf text dropped before USB framing.
The frozen receiver is also replayed to compare its exact decoded output.
"""
import hashlib
import importlib.util
import io
import re
import struct

_HEADER = struct.Struct('<8sQQII')


def audit(raw, console, receiver_path):
    spec = importlib.util.spec_from_file_location('frozen_usb_receiver_audit', receiver_path)
    receiver = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(receiver)
    sink = io.BytesIO()
    decoder = receiver.Decoder(sink)
    decoder.feed(raw)

    cursor = expected = gaps = duplicates = frames = lost_max = lost_frames = 0
    first_offset = None
    while len(raw)-cursor >= _HEADER.size:
        magic, offset, lost, count, flags = _HEADER.unpack_from(raw, cursor)
        if magic != b'LITHLOG1' or not 1 <= count <= 4063 or flags != 0:
            raise ValueError('Invalid saved USB frame header')
        if len(raw)-cursor < _HEADER.size+count:
            break
        if first_offset is None:
            first_offset = offset
        gaps += max(offset-expected, 0)
        duplicates += min(max(expected-offset, 0), count)
        lost_max = max(lost_max, lost)
        lost_frames += bool(lost)
        expected = max(expected, offset+count)
        frames += 1
        cursor += _HEADER.size+count
    incomplete = len(raw)-cursor
    agrees = (frames == decoder.frames and gaps == decoder.gap_bytes
        and duplicates == decoder.duplicate_bytes and incomplete == len(decoder.buffer)
        and expected == decoder.next_offset)
    exact = sink.getvalue() == console
    transport_ok = bool(frames and first_offset == 0 and not gaps and not duplicates
        and not incomplete and not lost_max and agrees and exact)
    kernel = console.find(b'Darwin Kernel Version 22.5.0:')
    timer = console.find(b'--- loader_return_after expired ---')
    normal = console.find(b'LITHIUM USB: normal timer return boundary; draining before quiesce')
    drains = list(re.finditer(rb'LITHIUM USB: timer boundary drain=(\d+) target=(\d+)\r?\n', console))
    drain_ok = bool(drains and int(drains[-1][1]) == 1
        and 0 < int(drains[-1][2]) <= expected
        and 0 <= kernel < timer < normal < drains[-1].start())
    return dict(raw_bytes=len(raw), console_bytes=len(console), frames=frames,
        raw_sha256=hashlib.sha256(raw).hexdigest(), console_sha256=hashlib.sha256(console).hexdigest(),
        first_offset=first_offset, final_offset=expected, missing_bytes=gaps,
        duplicate_bytes=duplicates, incomplete_frame_bytes=incomplete,
        device_lost_max=lost_max, frames_reporting_device_loss=lost_frames,
        independent_parser_agrees_with_frozen_receiver=agrees,
        frozen_redecode_equals_complete_console=exact, transport_verified=transport_ok,
        kernel_banner_offset=kernel, timer_offset=timer, normal_return_offset=normal,
        drain_success=drain_ok, drain_records=[dict(offset=m.start(), drained=int(m[1]),
            target=int(m[2])) for m in drains],
        emitted_kernel_stream_through_timer_verified=transport_ok and drain_ok,
        all_firmware_daemon_and_original_printf_logs_complete=False,
        scope='Saved USB byte stream only. Pre-framing printf truncation and uncollected daemon or firmware output remain outside this audit.')
