# Security policy

OpenMagic's drivers run in the Windows kernel, so security problems matter even for a small project.

## Reporting a vulnerability

Please report vulnerabilities privately through
[GitHub security advisories](https://github.com/alefbarbeli/OpenMagic/security/advisories/new), not in
public issues. Include the affected version, the steps to reproduce and the impact you observed. You
should receive an answer within a week.

## Test-signing and the project certificate

Releases are signed with the project's self-signed test certificate (`CN=OpenMagic Test Signing`,
public part in [`certs/`](certs)). Installing OpenMagic turns on Windows test-signing mode and adds that
certificate to the machine's Trusted Root and Trusted Publishers stores. In practice:

- while test-signing mode is on, Windows loads any test-signed kernel driver, not only OpenMagic's;
- code signed with the OpenMagic certificate is trusted on that PC.

The certificate's private key stays with the maintainer and is never published. Uninstalling
OpenMagic removes the certificate and offers to turn test-signing mode off. Only install releases from
this repository.
