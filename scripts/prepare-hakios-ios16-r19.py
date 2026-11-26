"""Check R19 source preservation; no runtime edit or device command."""
from pathlib import Path
import hakios
import hakios_r19


def main():
    spec = hakios.read(hakios.MANIFEST)
    hakios.policy(spec)
    hakios.resource_check()
    work = hakios.verify_workspace(spec)
    frozen = hakios.ROOT / "artifacts/hakios/lithium-ios16/phase4-r19/contract.json"
    hakios.require(not frozen.exists(), "Preserve the frozen R19 package")
    backup = hakios.ROOT / "artifacts/hakios/lithium-ios16/display-r19-before-host-refresh-20261118/backup.json"
    record = hakios.read(backup)
    hakios.require(hakios.sha(Path(record["archive_path"])) == record["archive_sha256"], "R19 rollback bytes changed")
    result = hakios_r19.source_checks(hakios.ROOT, work, hakios.inputs(spec))
    result.update(revision=19, inputs=hakios.inputs(spec), rollback_record_sha256=hakios.sha(backup),
                  source_script_sha256=hakios.sha(Path(__file__)),
                  purpose="Refresh normal build evidence; retain exact R18 display and R15 arguments")
    hakios.write(hakios.ROOT / "reports/hakios-ios16-r19-prepared-sources.json", result)
    print("PASS R19 source relationship and rollback; no phone or runtime source changes")


if __name__ == "__main__":
    main()
