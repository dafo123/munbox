# Security Policy

## Supported Versions

We provide security updates for the following versions of Munbox:

| Version | Supported          |
| ------- | ------------------ |
| 1.0.x   | :white_check_mark: |

## Reporting a Vulnerability

If you discover a security vulnerability in Munbox, please report it responsibly by following these guidelines:

### How to Report

1. **Do NOT** open a public GitHub issue for security vulnerabilities
2. Send an email to the project maintainers with details about the vulnerability
3. Include as much information as possible to help us understand and reproduce the issue

### What to Include

Please include the following information in your security report:

- **Description**: A clear description of the vulnerability
- **Impact**: What an attacker could achieve by exploiting this vulnerability
- **Reproduction Steps**: Step-by-step instructions to reproduce the issue
- **Sample Files**: If the vulnerability involves specific archive formats, include sample files (if safe to share)
- **Environment**: OS, compiler, and Munbox version information
- **Proposed Fix**: If you have suggestions for fixing the issue

### Response Process

- **Acknowledgment**: We will acknowledge receipt of your report within 48 hours
- **Investigation**: We will investigate the issue and provide an initial assessment within 1 week
- **Updates**: We will keep you informed of our progress throughout the investigation
- **Resolution**: We will work to resolve the issue as quickly as possible
- **Disclosure**: We will coordinate with you on appropriate disclosure timing

### Security Considerations

Munbox deals with potentially untrusted archive files from various sources. Key security areas include:

#### Input Validation
- Archive header parsing and validation
- File path sanitization to prevent directory traversal attacks
- Size limits to prevent memory exhaustion
- Compression ratio checks to prevent zip bombs

#### Memory Safety
- Buffer overflow prevention in decompression routines
- Proper bounds checking for array accesses
- Safe string handling throughout the codebase
- Resource cleanup to prevent memory leaks

#### File System Safety
- Path traversal protection (e.g., preventing `../../../etc/passwd` extraction)
- Symlink handling security
- File permission preservation considerations
- Disk space exhaustion prevention

## Security Best Practices for Users

When using Munbox:

1. **Trusted Sources**: Only process archives from trusted sources when possible
2. **Sandboxing**: Consider running Munbox in a sandboxed environment for untrusted archives
3. **Output Directory**: Use a dedicated output directory that doesn't contain sensitive files
4. **Disk Space**: Ensure sufficient disk space and monitor extraction sizes
5. **Updates**: Keep Munbox updated to the latest version for security fixes

## Known Security Considerations

### Archive Format Risks
- **Malformed Headers**: Munbox includes extensive validation, but malformed archives may still pose risks
- **Resource Consumption**: Large or deeply nested archives can consume significant system resources
- **Path Traversal**: While protections are in place, always extract to safe directories

### Mitigation Strategies
- Input validation at multiple layers
- Resource limits and timeouts
- Safe file path handling
- Comprehensive error handling

## Security Testing

We encourage security researchers to:
- Test Munbox with malformed or malicious archive files
- Perform fuzzing tests on the parsing code
- Review the source code for potential vulnerabilities
- Test on different platforms and configurations

## Acknowledgments

We appreciate the security research community's efforts to improve the safety of open source software. Responsible security researchers who report vulnerabilities will be acknowledged in our security advisories (with their permission).

## Contact

For security-related inquiries, please contact the project maintainers through the appropriate channels mentioned above.

---

Thank you for helping keep Munbox and its users safe!