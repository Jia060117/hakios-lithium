"""Parse bounded upstream ustacks records, including adjacent console records.

Record boundaries take precedence over physical newlines: the device console
can omit the end of a long printf, leaving the next thread on the same line.
Never borrow a later thread's register or lock-owner fields for an earlier one.
"""
import bisect
import re

_START = re.compile(r'LITHIUM USTACKS (?:begin|end)\b|ustacks:')
_FIELD = re.compile(r'\b(state|x0|x1|x2|x3|x16|ulock_op|word|copy|matches|owner_port|owner_tid|owner_same_task|lookup)=([^\s]+)')
_HEADER = re.compile(r'^ustacks: ([^\[\r\n]+)\[(\d+)\] (\d+) threads, shared cache slide (0x[0-9a-f]+), uptime ([\d.]+), at (\d+)')
_THREAD = re.compile(r'^ustacks:  thread (0x[0-9a-f]+)(.*?): (.*)$')


def parse(text):
    starts = list(_START.finditer(text))
    newlines = [hit.start() for hit in re.finditer('\n', text)]
    samples = []
    sample = process = None
    for index, start in enumerate(starts):
        next_start = starts[index+1].start() if index+1 < len(starts) else len(text)
        line_end = text.find('\n', start.start())
        end = min(next_start, line_end if line_end >= 0 else len(text))
        raw = text[start.start():end].rstrip('\r')
        location = dict(line=bisect.bisect_left(newlines, start.start())+1,
            stream_start=start.start(), stream_end=end)
        begin = re.fullmatch(r'LITHIUM USTACKS begin sequence=(\d+)', raw)
        if begin:
            sample = dict(sequence=int(begin[1]), begin_line=location['line'],
                begin_offset=start.start(), processes=[], ended=False)
            samples.append(sample)
            process = None
            continue
        finish = re.fullmatch(r'LITHIUM USTACKS end sequence=(\d+) processes=(\d+) wall_us=(\d+)', raw)
        if finish and sample and sample['sequence'] == int(finish[1]):
            sample.update(ended=True, end_line=location['line'], end_offset=end,
                processes_reported=int(finish[2]), wall_us=int(finish[3]))
            process = None
            continue
        head = _HEADER.match(raw)
        if head and sample and not sample['ended']:
            process = dict(name=head[1], pid=int(head[2]), reported_threads=int(head[3]),
                slide=head[4], uptime=head[5], mach_absolute_us=int(head[6]), threads=[], **location)
            sample['processes'].append(process)
            continue
        thread = _THREAD.fullmatch(raw)
        if not (thread and process and sample and not sample['ended']):
            continue
        fields = dict(_FIELD.findall(raw))
        context = thread[3].split(' state=', 1)
        pc_lr = re.findall(r'0x[0-9a-f]+', context[0])
        tail = re.search(r'\b(?:lookup|x16)=[^\s]+(.*)$', context[1]) if len(context) == 2 else None
        if tail and ' lookup=' in raw:
            tail = re.search(r'\blookup=[^\s]+(.*)$', raw)
        frames = re.findall(r'0x[0-9a-f]+', tail[1]) if tail else []
        # An unterminated tail may contain a partial hex token. Its last
        # candidate must not be symbolized as a complete address.
        complete_line = line_end == end
        partial_tail = not complete_line and tail is not None and bool(frames)
        if partial_tail:
            frames = frames[:-1]
        process['threads'].append(dict(tid=thread[1], label=thread[2], fields=fields, raw=raw,
            pc_lr=pc_lr, frames=frames, unterminated_record=not complete_line,
            final_frame_excluded=partial_tail,
            register_scope='Last saved user kernel-entry context; TH_RUN is not a live user PC', **location))
    for sample in samples:
        for process in sample['processes']:
            process['parsed_threads'] = len(process['threads'])
            process['thread_count_matches'] = process['parsed_threads'] == process['reported_threads']
    return samples


def owner_links(samples):
    links = []
    for sample in samples:
        for process in sample['processes']:
            threads = {thread['tid']: thread for thread in process['threads']}
            for thread in process['threads']:
                fields = thread['fields']
                owner = fields.get('owner_tid', '0x0')
                if owner == '0x0':
                    continue
                target = threads.get(owner) if fields.get('owner_same_task') == '1' else None
                links.append(dict(sequence=sample['sequence'], process=process['name'], pid=process['pid'],
                    waiter_tid=thread['tid'], owner_tid=owner, matches=fields.get('matches'), lookup=fields.get('lookup'),
                    waiter_line=thread['line'], waiter_offset=thread['stream_start'],
                    owner_line=target['line'] if target else None, owner_offset=target['stream_start'] if target else None,
                    waiter_stack=thread['raw'], owner_stack=target['raw'] if target else None,
                    historical_owner_or_deadlock_proven=False))
    return links
