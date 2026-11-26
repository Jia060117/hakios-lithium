#!/usr/bin/env python3
"""Keep exact UI injection scope and latest existing child abort in the retained screen."""
import difflib
import hakios
from hakios_bgr24 import replace_once

def main():
    hakios.resource_check();spec=hakios.read(hakios.MANIFEST);work=hakios.verify_workspace(spec)
    pieces=[]
    for name in ['xnu/files/osfmk/arm64/loader_return.c','xnuloader/loader.c']:
        before=(work/name).read_text();after=before
        if name.endswith('loader_return.c'):
            after=replace_once(after,'layers_copied=%u bb_hook=%u','layers_copied=%u bb_inject=%u')
        else:
            after=replace_once(after,'"LITHIUM UI SVC ", "LITHIUM UI SURF ",',
                '"LITHIUM UI SVC ", "LITHIUM UI SURF ", "LITHIUM UI READY ", "LITHIUM UI FINISH ",\n'
                '            "userabort: backboardd[", "userabort: SpringBoard[", "usertrap: ",')
            after=replace_once(after,'        const char *ui_panic =',
                '        lithium_show_launchd_diagnostics(log, bytes);\n        const char *ui_panic =')
        pieces.append(''.join(difflib.unified_diff(before.splitlines(True),after.splitlines(True),fromfile='a/'+name,tofile='b/'+name)))
    path=hakios.ROOT/spec['port_path']/'patches/0019-lithium-ui-diagnostic-scope.patch'
    assert not path.exists();path.write_text(''.join(pieces))
    print('Staged truthful injection metric and existing child abort summary')

if __name__=='__main__':main()
