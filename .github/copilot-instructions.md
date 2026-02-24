# Project Guidelines

## Code Style
- Language is C (C89+); follow existing K&R style and tab indentation in core files like [packet.c](../packet.c) and [auth.c](../auth.c).
- Prefer existing naming patterns: `snake_case` for functions/variables, `UPPER_CASE` for macros.
- Use project logging/fatal wrappers from [log.h](../log.h) and [fatal.c](../fatal.c); do not introduce ad-hoc logging APIs.
- Keep fail-closed behavior: memory wrappers in [xmalloc.c](../xmalloc.c) and packet paths often terminate on invariant breaks.
- For protocol parsing/building, use `sshbuf`/`sshpkt` helpers (see [dispatch.c](../dispatch.c), [monitor.c](../monitor.c)).

## Architecture
- Build outputs are declared in [Makefile.in](../Makefile.in): `ssh`, `sshd`, `sshd-session`, `sshd-auth`, `scp`, `sftp`, helper binaries.
- Shared protocol/crypto logic is centralized in `libssh.a` object sets in [Makefile.in](../Makefile.in); reuse existing modules before adding new files.
- Server-side privilege separation is mandatory: monitor + unprivileged child model in [monitor.c](../monitor.c) and runtime behavior in [README.privsep](../README.privsep).
- Client/server configuration flow is split (`readconf.c` for client, `servconf.c` for server); keep option ownership consistent.

## Build and Test
- Release-style build: `./configure && make`.
- From git checkout, regenerate autotools first: `autoreconf && ./configure && make`.
- Run tests with `make tests` (aggregates regress and unit suites).
- Focused unit run: `make unit`.
- Regenerate dependencies only when changing headers/large refactors: `make depend`.
- If testing auth/server changes, ensure privsep prerequisites from [README.privsep](../README.privsep) are met.

## Project Conventions
- Follow dispatch-table control flow patterns in [dispatch.c](../dispatch.c); add packet handlers via existing registration flow.
- Preserve `r`-accumulation + `fatal_fr`/`sshpkt_fatal` error style in parsing and IPC paths.
- Keep platform-specific code in `platform-*` and `openbsd-compat/`; avoid scattering `#ifdef` logic into unrelated files.
- Avoid broad API reshaping: this codebase favors incremental, compatibility-preserving changes.

## Integration Points
- Crypto backend and feature flags are configured in [configure.ac](../configure.ac) (OpenSSL/LibreSSL, zlib, PAM, Kerberos/GSSAPI, libfido2).
- Link integration points through existing vars in [Makefile.in](../Makefile.in) (`SSHDLIBS`, `GSSLIBS`, `K5LIBS`, `LIBFIDO2`).
- Authentication/audit backends are modular (`auth-*.c`, `audit-*.c`); extend existing modules instead of creating parallel frameworks.

## Security
- Do not weaken privilege separation invariants (privsep user, chroot path, monitor request gating).
- Do not bypass packet boundary checks or `sshbuf` end-of-message validation.
- Do not replace fatal-on-corruption paths with soft-fail behavior in security-critical flows.
- Preserve sensitive-memory cleanup patterns (`freezero`, `explicit_bzero`) in auth/key handling paths.
