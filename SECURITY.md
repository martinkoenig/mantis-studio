# Security model of Skeleton v0.1

This milestone is intended for a local developer workstation. It is not a hardened service for untrusted networks, users, projects or plugins.

- The control listener binds `127.0.0.1`, requires a per-session token, limits messages to 4 MiB and sets connection timeouts on the POSIX backend.
- Clients with the token can operate the daemon, open local projects, export files and shut it down. Do not expose the listener through a proxy or share the token with an untrusted client.
- The daemon explicitly approves the three shipped first-party plugin IDs for in-process execution in its configured plugin directory. The directory and its binaries are part of the trusted installation. A manifest cannot by itself grant other plugin IDs in-process trust.
- Other processors/exporters use an isolated host. This contains accidental crashes; it does **not** restrict filesystem, network, process or device access. Permissions are declared in manifests but an OS permission sandbox is not implemented.
- Do not replace a trusted installation's plugin library with an untrusted binary. IDs are not signatures.
- Local object files are shared by read-only mapping. The project owner can still edit files outside the application; integrity checks detect accidental corruption, not malicious tampering. FNV-1a-64 is explicitly tagged and is not a cryptographic security hash.
- Export refuses an existing destination and lexically rejects destinations inside the project store. It is designed for trusted local paths, not adversarial symlink races.
- Cancellation of native in-process plugin code is cooperative at node boundaries. An uncooperative trusted plugin can block its job; use isolated execution for uncertain code.

Report a suspected security issue privately to the repository owner once a private reporting channel is configured. Do not publish secrets or exploit payloads in public issue reports. No email address or external reporting endpoint is invented by this template.
