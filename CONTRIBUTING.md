# Contributing

Thanks for improving PulseForge.

1. Open an issue for ABI or architecture changes before implementation.
2. Keep the kernel and Python algorithms bit-exact.
3. Add a deterministic test for every bug fix.
4. Run `make test`, `make userspace` and the relevant kernel build before submitting a pull request.
5. Do not commit generated traces, binaries or machine-specific benchmark claims.

Commit messages should be short and imperative. Public UAPI changes must update `docs/protocol.md` and explain compatibility impact.

