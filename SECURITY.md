# Security policy

HNSWhy is a local, single-user demo application, not a multi-tenant service. It
ships without user authentication and must not be exposed directly to the public
internet. Run it on loopback or put it behind an access-controlled proxy.

The server does include transport guards for the local threat model:

- a `Host` allowlist to block DNS rebinding;
- rejection of cross-site state-changing requests via `Origin` and
  `Sec-Fetch-Site`;
- owner-only file creation for the database, WAL/SHM, lock, and temporary PDFs;
- bounded PDF extraction and a bounded benchmark-report read.

These guards are documented in [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) and
are not a substitute for authentication.

## Reporting a vulnerability

Please report suspected vulnerabilities privately through GitHub Security
Advisories on this repository rather than opening a public issue. Include the
affected endpoint or component, a minimal reproduction, and the impact you
observed. You can expect an acknowledgement within a few days.
