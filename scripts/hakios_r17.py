"""R17 restores the native D22AP render canvas and status-bar profile."""
from hakios_boot_observer import once

PORT_DELTA = {'diagnostics/r17.json', 'diagnostics/display-userspace-r17.inc',
    'patches/0017-lithium-native-display-profile.patch'}


def userspace_source(text):
    text = once(text,
        "/* Measurements only, except for retaining UIKit's native 2x/1334 status bar. */",
        '/* Measurements only; keep the D22AP native 3x/2436 status-bar profile. */')
    return once(text,
        'bool native = chosen && product && scale == 2 && subtype == 1334;',
        'bool native = chosen && product && scale == 3 && subtype == 2436;')


def springboard_source(text):
    old = '#include "../../devices/lithium/diagnostics/display-userspace.inc"'
    new = '#include "../../devices/lithium/diagnostics/display-userspace-r17.inc"'
    if text.count(old) == 1:
        return text.replace(old, new, 1)
    if text.count(new) == 1 and old not in text:
        return text
    raise RuntimeError('R17 requires one exact SpringBoard display include')


def command_line(r16):
    old = ('fbwidth=750', 'fbheight=1334')
    new = ('fbwidth=1125', 'fbheight=2436')
    words = r16.split()
    if any(words.count(value) != 1 for value in old):
        raise RuntimeError('R17 requires the exact frozen R16 render arguments')
    updated = r16
    for before, after in zip(old, new):
        updated = updated.replace(before, after, 1)
    before, after = r16.split(), updated.split()
    differences = [(a, b) for a, b in zip(before, after) if a != b]
    expected = [(old[0], new[0]), (old[1], new[1])]
    if len(before) != len(after) or differences != expected:
        raise RuntimeError('R17 may only change fbwidth/fbheight')
    return updated
