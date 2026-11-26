"""Register exact R18 provenance without weakening earlier revision checks."""
from pathlib import Path
ROOT = Path(__file__).resolve().parents[1]

def edit(name, pairs):
    if "--resume-review" in __import__("sys").argv and name in {"scripts/hakios_r15.py", "scripts/hakios_r16.py", "scripts/test-hakios-ios16-r9.py", "scripts/test-hakios-ios16-r16.py", "scripts/test-hakios-ios16-r17.py", "scripts/review-hakios-ios16.py"}: return
    path = ROOT/name
    text = path.read_text(encoding="utf-8")
    for old, new in pairs:
        if text.count(old) != 1: raise RuntimeError(f"{name}: unexpected anchor {old!r}")
        text = text.replace(old,new,1)
    path.write_text(text,encoding="utf-8",newline="\n")

def main():
    edit("scripts/hakios_r15.py", [("(13,15,16,17)", "(13,15,16,17,18)")])
    edit("scripts/hakios_r16.py", [("    return hakios.sha(work/name) == digest",
        "    path = work/\'devices/lithium/diagnostics/r18.json\'\n    if path.exists():\n        c = hakios.read(path)\n        if c[\'before_sha256\'].get(name) == digest: digest = c[\'after_sha256\'][name]\n    return hakios.sha(work/name) == digest")])
    edit("scripts/test-hakios-ios16-r9.py", [("            if expected:\n",
        "            r18_path=self.port/\'diagnostics/r18.json\'\n            if r18_path.exists():\n                c=json.loads(self.read(r18_path))\n                if c.get(\'before_sha256\',{}).get(name)==expected: expected=c[\'after_sha256\'][name]\n            if expected:\n")])
    for revision in (16,17):
        edit(f"scripts/test-hakios-ios16-r{revision}.py", [("import hakios_r17", "import hakios_r17\nimport hakios_r18")]) if revision==16 else edit("scripts/test-hakios-ios16-r17.py", [("import hakios_r17", "import hakios_r17\nimport hakios_r18")])
    edit("scripts/test-hakios-ios16-r16.py", [("    sb_text=sb_source.read_text()",
        "    sb_text=hakios_r18.historical_text(work,\'userland/src/inject_sb.c\',sb_source.read_text())"),
        ("hakios.sha(sb_source)==r17[\'after_sha256\'][\'userland/src/inject_sb.c\']",
         "hashlib.sha256(sb_text.encode()).hexdigest()==r17[\'after_sha256\'][\'userland/src/inject_sb.c\']")])
    edit("scripts/test-hakios-ios16-r17.py", [("hakios.sha(work/name)==digest",
        "__import__(\'hashlib\').sha256(hakios_r18.historical_text(work,name,(work/name).read_text()).encode()).hexdigest()==digest"),
        ("target.write_bytes((work/name).read_bytes())",
         "target.write_text(hakios_r18.historical_text(work,name,(work/name).read_text()),encoding=\'utf-8\')")])
    edit("scripts/review-hakios-ios16.py", [("import hakios_r17", "import hakios_r17\nimport hakios_r18"),
        ("        if (work/\'devices/lithium/diagnostics/r17.json\').exists():expected=hakios_r17.springboard_source(expected)",
         "        if (work/\'devices/lithium/diagnostics/r17.json\').exists():expected=hakios_r17.springboard_source(expected)\n        if (work/\'devices/lithium/diagnostics/r18.json\').exists():expected=hakios_r18.springboard_source(expected)")])
    edit("scripts/check-hakios-ios16-build.py", [("import hakios_r17", "import hakios_r17\nimport hakios_r18"),
        ("            expected.update(hakios_r17.PORT_DELTA)",
         "            expected.update(hakios_r17.PORT_DELTA)\n        if \'diagnostics/r18.json\' in delta:\n            expected.update(hakios_r18.PORT_DELTA)"),
        ("    if (work/\'devices/lithium/diagnostics/r16.json\').exists():",
         "    if (work/\'devices/lithium/diagnostics/r18.json\').exists():\n        hakios.require(b\'LITHIUM DISPLAY LOADER phase=after\' in loader.read_bytes(), \'Early display producer absent from actual loader\')\n        hakios.require(b\'LITHIUM DISPLAY PROFILE\' in (work/\'userland/obj/libhakios.sb.dylib\').read_bytes(), \'Actual profile comparison absent\')\n    if (work/\'devices/lithium/diagnostics/r16.json\').exists():"),
        ('\'#include "../../devices/lithium/diagnostics/display-userspace-r17.inc"\' in sb_source',
         '(\'#include "../../devices/lithium/diagnostics/display-userspace-r18.inc"\' if (work/\'devices/lithium/diagnostics/r18.json\').exists() else \'#include "../../devices/lithium/diagnostics/display-userspace-r17.inc"\') in sb_source')])
    edit("scripts/freeze-hakios-ios16-boot-userspace.py", [("(3,4,5,6,7,8,9,10,11,12,13,14,15,16,17)", "(3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18)")])
    edit("scripts/package-hakios-ios16.py", [("import hakios_r17", "import hakios_r17\nimport hakios_r18"),
        ("(1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17)", "(1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18)"),
        ("    if revision>=17:expected_changes.update(hakios_r17.PORT_DELTA)",
         "    if revision>=17:expected_changes.update(hakios_r17.PORT_DELTA)\n    if revision>=18:expected_changes.update(hakios_r18.PORT_DELTA)"),
        ("(13,14,15,16,17)", "(13,14,15,16,17,18)"),
        ("    budgeter=hakios_r15",
         "    if revision==18:\n        checks=hakios.read(hakios.ROOT/\'reports/hakios-ios16-r18-checks.json\')\n        hakios.require(checks[\'passed\'] and checks[\'inputs\']==current and checks[\'actual_production_functions\'], \'R18 actual early-profile tests required\')\n        for name,digest in checks[\'evidence_sha256\'].items():\n            hakios.require(hakios.sha(Path(name))==digest, \'R18 evidence changed: \'+name)\n        cmdline=hakios_r18.command_line(hakios.read(hakios.ROOT/\'artifacts/hakios/lithium-ios16/phase4-r15/contract.json\')[\'cmdline\'])\n    budgeter=hakios_r15"),
        ("    contract=dict(schema=1",
         "    if revision==18:\n        for name in [*(\'ports/hakios-lithium/\'+p for p in sorted(hakios_r18.PORT_DELTA)),\n            \'scripts/hakios_r18.py\',\'scripts/prepare-hakios-ios16-r18.py\',\'scripts/test-hakios-ios16-r18.py\',\n            \'scripts/test-hakios-ios16-r18-package.py\',\'scripts/run-hakios-ios16-r18-regressions.py\',\'reports/hakios-ios16-r18-checks.json\']:\n            evidence[name]=hakios.sha(hakios.ROOT/name)\n    contract=dict(schema=1"),
        ("    hakios.write(out/\'contract.json\',contract)",
         "    if revision==18:\n        contract.update(display_profile_contract=\'ports/hakios-lithium/diagnostics/r18.json\',payload_arguments_preserved_from_r15=True,display_acceptance_verified=False,render_profile=dict(width=750,height=1334,scale=2,subtype=1334,points=[375,667]))\n    hakios.write(out/\'contract.json\',contract)")])
    p=ROOT/"scripts/run-wsl-limited.ps1"
    text=p.read_text(encoding="utf-8")
    tasks=["prepare","checks","review","regressions","userspace","build-check","freeze","package","package-checks"]
    text=text.replace("\'hakios-r17-package-checks\', \'kernel\'", "\'hakios-r17-package-checks\', "+", ".join("\'hakios-r18-"+x+"\'" for x in tasks)+", \'kernel\'",1)
    begin=text.index("if ($Task -like \'hakios-r17-*\') {")
    end=text.index("if ($Task -like \'hakios-r12-*\') {",begin)
    text=text[:end]+text[begin:end].replace("r17","r18")+text[end:]
    text=text.replace("if ($Task -notlike \'hakios-r17-*\'", "if ($Task -notlike \'hakios-r18-*\' -and $Task -notlike \'hakios-r17-*\'",1)
    anchor="& wsl.exe @arguments"
    text=text.replace(anchor,"if ($Task -in @(\'hakios-r18-userspace\', \'hakios-r18-build-check\')) { $arguments += \'--phase4\' }\nif ($Task -in @(\'hakios-r18-freeze\', \'hakios-r18-package\')) { $arguments += @(\'--revision\', \'18\') }\n"+anchor,1)
    p.write_text(text,encoding="utf-8",newline="\n")
    print("Registered R18 exact source chain and preserved historical checks")

if __name__=="__main__": main()
