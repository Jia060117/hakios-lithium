"""R18: serialize the coherent retained 750x1334 profile in the loader."""
import hashlib
from pathlib import Path
import subprocess
import tempfile
from hakios_boot_observer import once

PORT_DELTA = {"patches/0018-lithium-early-display-profile.patch", "diagnostics/r18.json",
    "diagnostics/display-loader.inc", "diagnostics/display-userspace-r18.inc"}

def loader_source(text):
    text = once(text, "static void\ndt_identity(struct dt_writer",
        '#if defined(HAKIOS_LITHIUM)\n#include "../devices/lithium/diagnostics/display-loader.inc"\n#endif\n\nstatic void\ndt_identity(struct dt_writer')
    text = once(text, '\t\tif (str_eq(props[i].name, "artwork-display-gamut") && !gamut_p3) {',
        '#if defined(HAKIOS_LITHIUM)\n\t\tif (lithium_display_loader_identity(w, props, &props[i])) continue;\n#endif\n'
        '\t\tif (str_eq(props[i].name, "artwork-display-gamut") && !gamut_p3) {')
    text = once(text, "\t/* --- device tree ---------------------------------------------------- */",
        "#if defined(HAKIOS_LITHIUM)\n\tlithium_display_loader_configure(pi.cmdline);\n#endif\n\n"
        "\t/* --- device tree ---------------------------------------------------- */")
    return once(text, '\tdt_prop_u32(&w, "display-scale", 3);',
        '#if defined(HAKIOS_LITHIUM)\n\tdt_prop_u32(&w, "display-scale", lithium_loader_display.automatic ? lithium_loader_display.scale : 3);\n#else\n\tdt_prop_u32(&w, "display-scale", 3);\n#endif')

def springboard_source(text):
    return once(text, '#include "../../devices/lithium/diagnostics/display-userspace-r17.inc"',
        '#include "../../devices/lithium/diagnostics/display-userspace-r18.inc"')

def command_line(prior):
    from hakios_r16 import command_line as preserve
    return preserve(prior)

def historical_text(work, name, text):
    """Reconstruct R17 only when both exact before/after hashes prove the chain."""
    import hakios
    contract = work/"devices/lithium/diagnostics/r18.json"
    if not contract.exists(): return text
    c = hakios.read(contract)
    if name not in c["after_sha256"]: return text
    if hashlib.sha256(text.encode()).hexdigest() == c["before_sha256"][name]: return text
    hakios.require(hashlib.sha256(text.encode()).hexdigest() == c["after_sha256"][name], "R18 source differs: "+name)
    with tempfile.TemporaryDirectory(prefix="r18-history-") as folder:
        base = Path(folder)
        # Supply all targets because the patch covers loader and injection.
        for n in c["after_sha256"]:
            target = base/n
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(text.encode() if n == name else (work/n).read_bytes())
        patch = work/"devices/lithium"/c["patch"]
        subprocess.run(["git", "apply", "--reverse", str(patch)], cwd=base, check=True)
        result = (base/name).read_text()
        hakios.require(hashlib.sha256(result.encode()).hexdigest() == c["before_sha256"][name], "R18 history mismatch: "+name)
        return result
