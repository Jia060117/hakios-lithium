"""Reject cross-thread owner attribution in truncated/adjacent ustacks output."""
from pathlib import Path
import hashlib
import json
from hakios_ustacks_log import parse, owner_links


def main():
    start = 'LITHIUM USTACKS begin sequence=2\n'
    head = 'ustacks: service[7] 3 threads, shared cache slide 0x4000, uptime 10.001, at 10001000\n'
    first = 'ustacks:  thread 0xfd (running): 0x100000004 0x100000008 state=0x4 x0=0x0 x1=0x0 x2=0x0 x3=0x0 x16=0x204 0x10000000c'
    waiter = 'ustacks:  thread 0xfe: 0x100000010 0x100000014 state=0x1 x0=0x2 x1=0x100001000 x2=0x3002 x3=0x0 x16=0x203 ulock_op=2 word=0x3002 copy=0 matches=1 owner_port=0x3003 owner_tid=0xff owner_same_task=1 lookup=0 0x100000018 0x1f5bc'
    owner = 'ustacks:  thread 0xff (running): 0x10000001c 0x100000020 state=0x4 x0=0x100001000 x1=0x0 x2=0x3003 x3=0x0 x16=0x1f0000000 0x100000024\n'
    finish = 'LITHIUM USTACKS end sequence=2 processes=1 wall_us=42\n'
    cases = []
    for name, middle in [('adjacent', first+waiter+owner), ('separate', first+'\n'+waiter+'\n'+owner)]:
        samples = parse(start+head+middle+finish)
        process = samples[0]['processes'][0]
        assert samples[0]['ended'] and process['thread_count_matches']
        a, b, c = process['threads']
        assert [t['tid'] for t in process['threads']] == ['0xfd', '0xfe', '0xff']
        assert 'owner_tid' not in a['fields'] and 'owner_tid' not in c['fields']
        assert b['fields']['owner_tid'] == '0xff' and b['fields']['x16'] == '0x203'
        links = owner_links(samples)
        assert len(links) == 1 and links[0]['waiter_tid'] == '0xfe' and links[0]['owner_tid'] == '0xff'
        assert links[0]['owner_offset'] == c['stream_start']
        assert not links[0]['historical_owner_or_deadlock_proven']
        if name == 'adjacent':
            assert a['final_frame_excluded'] and b['final_frame_excluded']
            assert b['frames'] == ['0x100000018']
        cases.append(name)
    second = 'LITHIUM USTACKS begin sequence=3\n'+head+owner+finish.replace('sequence=2', 'sequence=3')
    samples = parse(start+head+first+waiter+owner+finish+'unrelated output\n'+second)
    assert len(samples) == 2 and len(owner_links(samples)) == 1
    assert not samples[1]['processes'][0]['thread_count_matches']
    assert len(parse(start+head+first)[0]['processes'][0]['threads']) == 1
    assert parse(start+head+first)[0]['ended'] is False
    assert not parse('no diagnostics\n')
    cases.extend(['sample isolation', 'incomplete sample', 'no records'])
    root = Path(__file__).resolve().parents[1]
    prior = root/'reports/local/20261117-032135-817008-ios16-r7/snapshot-033134-741555/console.log'
    if prior.exists():
        actual = parse(prior.read_text(encoding='utf-8'))
        p = {p['name']: p for p in actual[1]['processes']}
        assert len(p['SpringBoard']['threads']) == 5
        assert len(p['runningboardd']['threads']) == 12
        assert all(x['thread_count_matches'] for x in p.values())
        links = owner_links(actual)
        assert len(links) == 1 and links[0]['waiter_tid'] == '0xfe' and links[0]['owner_tid'] == '0xff'
        assert links[0]['owner_stack'].startswith('ustacks:  thread 0xff ')
        cases.append('actual frozen R7: all 17 threads and exact owner pair; no borrowed fields')
    result = dict(passed=True, cases=cases,
        parser_sha256=hashlib.sha256((root/'scripts/hakios_ustacks_log.py').read_bytes()).hexdigest(),
        test_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(), device_commands_sent=False,
        historical_owner_or_deadlock_proven=False)
    (root/'reports/hakios-ios16-ustacks-parser-checks.json').write_text(json.dumps(result, indent=2)+'\n', encoding='utf-8')
    print('PASS '+str(len(cases))+' parser boundary and exact attribution checks')


if __name__ == '__main__':
    main()
