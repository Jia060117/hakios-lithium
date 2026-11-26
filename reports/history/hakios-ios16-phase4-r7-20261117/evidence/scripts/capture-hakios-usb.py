#!/usr/bin/env python3
"""Capture the experimental lithium USB bulk log; never sends device commands.

Requires PyUSB and a libusb backend. Windows uses the device's WinUSB compatible
ID; do not replace the phone's Fastboot driver. This is a different USB identity.
"""
import argparse
from datetime import datetime
import json
from pathlib import Path
import struct
import time

MAGIC = b'LITHLOG1'
HEADER = struct.Struct('<8sQQII')

def identify(candidate, util):
    """Require the board serial; LANGID failure never authorizes a VID/PID match.

    en-US is declared by ud_string(0) in the compiled device source. Asking
    for that language explicitly is valid even if Windows cannot fetch the
    language table during enumeration. Keep both failures for diagnosis.
    """
    errors=[]
    candidate.default_timeout=1000
    for lang in (None, 0x0409):
        try:
            serial=util.get_string(candidate,candidate.iSerialNumber,langid=lang)
            return serial=='lithium-log-1', errors
        except Exception as error:
            errors.append(dict(stage='serial_descriptor',language=lang,
                error_type=type(error).__name__,message=str(error)))
    return False, errors

class Decoder:
    def __init__(self, sink):
        self.buffer = bytearray()
        self.next_offset = 0
        self.sink = sink
        self.frames = self.gap_bytes = self.duplicate_bytes = 0

    def feed(self, data):
        self.buffer.extend(data)
        while len(self.buffer) >= HEADER.size:
            if not self.buffer.startswith(MAGIC):
                raise ValueError('Invalid USB log framing; raw capture preserved')
            _, offset, lost, count, flags = HEADER.unpack_from(self.buffer)
            if not 1 <= count <= 4063 or flags != 0:
                raise ValueError('Unsupported/corrupt USB log frame')
            size = HEADER.size + count
            if len(self.buffer) < size:
                return
            payload = bytes(self.buffer[HEADER.size:size])
            del self.buffer[:size]
            if offset > self.next_offset:
                gap = offset - self.next_offset
                self.gap_bytes += gap
                self.sink.write(f'\n[USB LOG GAP {gap} bytes; device reported {lost}]\n'.encode())
            elif offset < self.next_offset:
                overlap = min(len(payload), self.next_offset - offset)
                self.duplicate_bytes += overlap
                payload = payload[overlap:]
            self.sink.write(payload)
            self.sink.flush()
            self.next_offset = max(self.next_offset, offset + count)
            self.frames += 1

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=Path('reports/local/usb/console.log'))
    parser.add_argument('--wait', type=int, default=180, help='wait for enumeration, seconds')
    parser.add_argument('--duration', type=int, default=0, help='capture duration after enumeration; zero until interrupted')
    parser.add_argument('--status', type=Path, help='write capture progress and final counters as JSON')
    parser.add_argument('--stop-file', type=Path, help='stop the host capture when this file exists')
    args = parser.parse_args()
    try:
        import usb.core
        import usb.util
    except ImportError:
        raise SystemExit('PyUSB is required for capture; offline decoder needs no dependency')
    backend = None
    try:
        import libusb_package
        import usb.backend.libusb1
        backend = usb.backend.libusb1.get_backend(find_library=libusb_package.find_library)
    except ImportError:
        pass
    report = dict(started_at=datetime.now().astimezone().isoformat(), enumerated=False,
                  frames=0, missing_bytes=0, duplicate_bytes=0, status='waiting',
                  output=str(args.output), device_commands_sent=False)
    def save():
        if args.status:
            args.status.parent.mkdir(parents=True, exist_ok=True)
            temp=args.status.with_name(args.status.name+'.tmp')
            temp.write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
            temp.replace(args.status)
    save()
    device=None;claimed=False;decoder=None
    report.update(candidate_seen=False,identity_verified=False,descriptor_errors=0,
                  descriptor_error_samples=[],configuration_verified=False)
    try:
        deadline = time.monotonic() + args.wait
        while time.monotonic() < deadline:
            if args.stop_file and args.stop_file.exists():
                report['status']='host_stopped_before_enumeration';return
            try:
                candidates=usb.core.find(find_all=True,idVendor=0x1209,idProduct=0x0001,backend=backend)
                for candidate in candidates:
                    report['candidate_seen']=True
                    matched,errors=identify(candidate,usb.util)
                    report['descriptor_errors']+=len(errors)
                    report['descriptor_error_samples']=(report['descriptor_error_samples']+errors)[-8:]
                    if matched:
                        device=candidate;break
                    usb.util.dispose_resources(candidate)
            except usb.core.USBError as error:
                report['enumeration_error']=str(error)
            save()
            if device is not None:break
            time.sleep(0.5)
        if device is None:
            report['status']='descriptor_verification_failed' if report['candidate_seen'] else 'not_enumerated'
            raise RuntimeError('MIX USB identity not verified before deadline; see capture status')
        report.update(enumerated=True,identity_verified=True,status='connecting');save()
        try:
            configuration=device.get_active_configuration()
        except usb.core.USBError:
            device.set_configuration(1)
            configuration=device.get_active_configuration()
        interface=configuration[(0,0)]
        endpoint=usb.util.find_descriptor(interface,bEndpointAddress=0x81)
        if endpoint is None or endpoint.bmAttributes & 3 != 2:
            raise RuntimeError('Verified MIX does not expose the expected bulk IN endpoint')
        usb.util.claim_interface(device,0);claimed=True
        report['configuration_verified']=True
        args.output.parent.mkdir(parents=True,exist_ok=True)
        # Exclusive creation preserves previous sessions, including partial logs.
        with args.output.open('xb') as log, args.output.with_suffix('.frames').open('xb') as raw:
            decoder = Decoder(log)
            print('Capturing MIX USB log. Ctrl+C stops the host only.',flush=True)
            report['status']='capturing';save()
            finish=time.monotonic()+args.duration if args.duration else float('inf')
            last_status=0
            while True:
                if time.monotonic()>=finish or (args.stop_file and args.stop_file.exists()):
                    report['status']='host_capture_complete';break
                try:
                    packet = bytes(device.read(0x81, 8192, timeout=1000))
                except usb.core.USBTimeoutError:
                    continue
                raw.write(packet); raw.flush()
                decoder.feed(packet)
                report.update(frames=decoder.frames,missing_bytes=decoder.gap_bytes,
                              duplicate_bytes=decoder.duplicate_bytes,console_bytes=log.tell())
                if time.monotonic()-last_status>=2:
                    save();last_status=time.monotonic()
    except KeyboardInterrupt:
        report['status']='host_interrupted'
        if decoder:
            print(f'Captured {decoder.frames} frames; missing {decoder.gap_bytes}, duplicates {decoder.duplicate_bytes} bytes')
    except Exception as error:
        report.update(failure_stage=report['status'],status='capture_failed',failure=str(error));raise
    finally:
        report['finished_at']=datetime.now().astimezone().isoformat();save()
        if device is not None:
            try:
                if claimed:usb.util.release_interface(device,0)
                usb.util.dispose_resources(device)
            except usb.core.USBError as error:
                report['cleanup_error']=str(error);save()

if __name__ == '__main__':
    main()
