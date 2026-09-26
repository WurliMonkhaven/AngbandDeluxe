# Frontend implementation

See the [project README](../README.md) for building and running this standalone
frontend and the [engine contract](../protocol/README.md) for integration.

`build.py` builds only the frontend and its tests. `readiness.py` checks UI,
audio/GPU and packaging without an engine; `--engine` adds the package handshake
smoke check. `package_windows.py` produces an engine-free Windows ZIP. The separate
angband repository owns engine sources, backend tests, data and its own build.

`--engines-dir PATH` selects the installation folder. `--backend PATH` is a test
convenience requiring a matching adjacent manifest; `--data-dir PATH` can override
that package's data for isolated tests. `--user-dir PATH` selects the frontend
profile root, with engine saves isolated underneath it. `--check-assets` validates
frontend assets without requiring an engine. Runtime assets have no engine-source
fallback.
