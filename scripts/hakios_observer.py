"""Read the bounded observer budgets directly from the compiled C source."""
import re
import hakios


def contract():
    path = hakios.ROOT / 'ports/hakios-lithium/tests/ios14_launchd.h'
    source = path.read_text()
    def macro(name):
        values = re.findall(r'^#define LITHIUM_OBSERVER_' + name + r' (\d+)$', source, re.M)
        hakios.require(len(values) == 1, 'Observer budget missing/ambiguous: ' + name)
        return int(values[0])
    attempts, ps, retry, kernel = [macro(n) for n in ('ATTEMPTS', 'PS_SECONDS', 'RETRY_US', 'KERNEL_SECONDS')]
    hakios.require(attempts == 2 and ps == 30 and retry == 5000000 and kernel == 180,
        'Observer budget changed; review host tests and whole-test margin')
    return dict(attempts=attempts, ps_seconds=ps, retry_us=retry, kernel_seconds=kernel,
        initial_wait_seconds=0, child_cleanup_max_seconds=2,
        child_and_retry_max_seconds=attempts * (ps + 2) + (attempts - 1) * retry // 1000000,
        diagnostics_fd=2, capture_fd=1, source_sha256=hakios.sha(path))
