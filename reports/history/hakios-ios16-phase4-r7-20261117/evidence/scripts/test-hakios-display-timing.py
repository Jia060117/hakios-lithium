"""Validate conclusions against malformed, interleaved and tiny inverted spans."""
import json
from pathlib import Path
from hakios_display_timing import parse, summarize


def record(**replace):
    data = dict(swap=1,band=1,y0=0,y1=2040,wall_us=3497790,cpu_us=2713141,
        noncpu_us=784649,monotonic=1,cpu_le_wall=1)
    data.update(replace)
    return 'LITHIUM DISPLAY CPU '+' '.join(str(k)+'='+str(v) for k,v in data.items())


def main():
    valid = parse(record())[0]
    assert valid['accepted'] and abs(valid['cpu_fraction']-.775673) < .000001
    adjacent = summarize(record()+record(swap=2,wall_us=5438626,cpu_us=3469201,noncpu_us=1969425))
    assert adjacent['draw_total_count']==2 and adjacent['wall_us_range']==[3497790,5438626]
    tiny = summarize(record(band=0,wall_us=412,cpu_us=413,noncpu_us=0,cpu_le_wall=0)+record())
    assert tiny['draw_total_count']==1 and tiny['rejected_span_count']==1
    assert not parse(record(monotonic=0))[0]['accepted']
    assert not parse(record(noncpu_us=123))[0]['accepted']
    assert parse(record(noncpu_us=784648))[0]['accepted']
    assert not parse(record(y1=0))[0]['accepted']
    assert summarize('unrelated output')['draw_total_count']==0
    root=Path(__file__).resolve().parents[1]
    (root/'reports/hakios-ios16-display-timing-checks.json').write_text(json.dumps(dict(passed=True,
        cases=['valid measured draw','adjacent records remain separate','tiny setup inversion rejected',
            'nonmonotonic clock rejected','arithmetic mismatch rejected','microsecond rounding permitted',
            'empty row interval rejected','no records'],device_commands_sent=False),indent=2)+'\n',encoding='utf-8')
    print('PASS 8 display timing interpretation checks')


if __name__=='__main__':
    main()
