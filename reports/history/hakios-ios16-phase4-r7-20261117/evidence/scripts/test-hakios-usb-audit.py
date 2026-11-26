"""Reject false completeness from loss, overlap, truncated tails and disconnects."""
from pathlib import Path
import hashlib
import json
import struct
from hakios_usb_audit import audit


def main():
    root = Path(__file__).resolve().parents[1]
    receiver = root/'scripts/capture-hakios-usb.py'
    # Use the same production framing decoder; no USB imports happen at module load.
    assert receiver.exists()
    header = struct.Struct('<8sQQII')
    def frame(payload, offset=0, lost=0):
        return header.pack(b'LITHLOG1', offset, lost, len(payload), 0)+payload
    body = (b'Darwin Kernel Version 22.5.0: test\n'
        b'--- loader_return_after expired ---\n'
        b'LITHIUM USB: normal timer return boundary; draining before quiesce\n'
        b'LITHIUM USB: timer boundary drain=1 target=32\n')
    cases = []
    def check(name, raw, console, transport, boundary):
        result = audit(raw, console, receiver)
        assert result['transport_verified'] is transport, (name, result)
        assert result['emitted_kernel_stream_through_timer_verified'] is boundary, (name, result)
        assert not result['all_firmware_daemon_and_original_printf_logs_complete']
        cases.append(name)
        return result
    check('ordered multi-frame normal timer and successful drain', frame(body[:90])+frame(body[90:],90), body, True, True)
    check('nonzero device loss despite contiguous offsets', frame(body,lost=1), body, False, False)
    check('missing initial bytes', frame(body,offset=5), body, False, False)
    check('duplicate complete frame', frame(body)+frame(body), body, False, False)
    check('incomplete trailing header', frame(body)+b'LITH', body, False, False)
    check('incomplete trailing payload', frame(body)+frame(b'abc',len(body))[:-1], body, False, False)
    check('saved console differs from raw replay', frame(body), body+b'extra', False, False)
    failure = body.replace(b'drain=1',b'drain=0')
    check('failed drain', frame(failure), failure, True, False)
    failure = body.replace(b'target=32',b'target=999999')
    check('drain target beyond captured boundary', frame(failure), failure, True, False)
    failure = b'Darwin Kernel Version 22.5.0: test\n'
    check('disconnect without timer and drain', frame(failure), failure, True, False)
    check('empty capture', b'', b'', False, False)
    failure = body.splitlines(keepends=True)
    failure = b''.join(failure[:1]+failure[2:]+failure[1:2])
    check('timer marker after drain', frame(failure), failure, True, False)
    for raw in [b'BADMAGIC'+frame(body)[8:], header.pack(b'LITHLOG1',0,0,0,0),
            header.pack(b'LITHLOG1',0,0,1,1)+b'a']:
        try:
            audit(raw,body,receiver)
        except ValueError:
            pass
        else:
            raise AssertionError('Invalid header accepted')
    cases.append('invalid magic count and flags rejected')
    result = dict(passed=True,cases=cases,device_commands_sent=False,
        module_sha256=hashlib.sha256((root/'scripts/hakios_usb_audit.py').read_bytes()).hexdigest(),
        test_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest())
    (root/'reports/hakios-ios16-usb-audit-checks.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    print('PASS '+str(len(cases))+' saved USB stream audit checks')


if __name__ == '__main__':
    main()
