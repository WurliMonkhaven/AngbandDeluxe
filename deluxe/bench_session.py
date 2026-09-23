"""Transport/movement benchmarks on disposable town and first-floor saves."""
import argparse
from pathlib import Path
import subprocess
import tempfile
from types import SimpleNamespace
import test_backend as protocol


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--backend", type=Path, required=True)
    parser.add_argument("--transport", type=Path, required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    protocol.ARGS = SimpleNamespace(backend=args.backend.resolve(), data=root/"lib", verbose=False)
    test_root = args.backend.resolve().parent/"test-runs"
    test_root.mkdir(exist_ok=True)
    for depth in (0, 1):
        with tempfile.TemporaryDirectory(prefix="deluxe-bench-", dir=test_root) as folder:
            engine = protocol.Engine(folder)
            try:
                engine.hello(); engine.birth()
                if depth:
                    engine.key(ord('>'))
                    for _ in range(20):
                        if engine.state['readiness'] == 'ready': break
                        engine.key('enter')
                    assert engine.state['player']['depth'] == depth
                engine.call('session.close')
                assert engine.process.wait(timeout=10) == 0
            finally:
                engine.stop()
            subprocess.run([str(args.transport.resolve()), str(args.backend.resolve()),
                            str(root/'lib'), folder], check=True, timeout=60)


if __name__ == '__main__':
    main()
