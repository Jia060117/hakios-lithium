"""Read emitted display spans without mistaking wall time for CPU time."""
import re

_SPAN = re.compile(r'LITHIUM DISPLAY CPU swap=(\d+) band=(\d+) y0=(-?\d+) y1=(-?\d+) '
    r'wall_us=(\d+) cpu_us=(\d+) noncpu_us=(\d+) monotonic=(\d+) cpu_le_wall=(\d+)')


def parse(text):
    spans = []
    for match in _SPAN.finditer(text):
        names = ('swap', 'band', 'y0', 'y1', 'wall_us', 'cpu_us', 'noncpu_us', 'monotonic', 'cpu_le_wall')
        span = dict(zip(names, map(int, match.groups())))
        expected = max(span['wall_us']-span['cpu_us'], 0)
        span.update(stream_start=match.start(), line=text.count('\n', 0, match.start())+1,
            cpu_exceeds_wall_us=max(span['cpu_us']-span['wall_us'], 0),
            arithmetic_consistent=abs(span['noncpu_us']-expected) <= 1,
            accepted=bool(span['monotonic'] == 1 and span['cpu_le_wall'] == 1
                and span['cpu_us'] <= span['wall_us'] and span['wall_us'] > 0
                and abs(span['noncpu_us']-expected) <= 1 and span['y1'] > span['y0']))
        if span['accepted']:
            span['cpu_fraction'] = span['cpu_us']/span['wall_us']
        spans.append(span)
    return spans


def summarize(text):
    spans = parse(text)
    totals = [span for span in spans if span['band'] == 1 and span['accepted']]
    return dict(spans=spans, accepted_draw_totals=totals,
        draw_total_count=len(totals), rejected_span_count=sum(not x['accepted'] for x in spans),
        wall_us_range=[min(x['wall_us'] for x in totals), max(x['wall_us'] for x in totals)] if totals else None,
        cpu_us_range=[min(x['cpu_us'] for x in totals), max(x['cpu_us'] for x in totals)] if totals else None,
        noncpu_us_range=[min(x['noncpu_us'] for x in totals), max(x['noncpu_us'] for x in totals)] if totals else None,
        cpu_fraction_range=[min(x['cpu_fraction'] for x in totals), max(x['cpu_fraction'] for x in totals)] if totals else None,
        noncpu_scope='Wall minus current-thread CPU; includes scheduling, blocking, interrupts and measurement overhead. Not a direct scheduler-only or memory-bandwidth measurement.')
