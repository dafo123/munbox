# Contributing to Munbox

Thank you for your interest in contributing to Munbox! We welcome contributions from the community to help preserve and improve access to classic Macintosh archive formats.

## Getting Started

1. **Fork** the repository on GitHub
2. **Clone** your fork locally:
   ```bash
   git clone https://github.com/YOUR-USERNAME/munbox.git
   cd munbox
   ```
3. **Create a branch** for your changes:
   ```bash
   git checkout -b feature/your-feature-name
   ```

## Development Setup

### Prerequisites
- A C99-compliant C compiler (GCC, Clang, or MSVC)
- CMake 3.16 or newer
- Make or Ninja build tool

### Building
```bash
make              # Build the project
make test         # Run all tests
make shell-tests  # Run legacy shell test harness
```

For more detailed build instructions, see the [README.md](README.md#building-the-project).

## Types of Contributions

### 🐛 Bug Reports
- Use the GitHub issue tracker
- Include steps to reproduce
- Provide sample files when possible (if not confidential)
- Include system information (OS, compiler, CMake version)

### 🚀 Feature Requests
- Open an issue to discuss the feature first
- Explain the use case and rationale
- Consider backwards compatibility

### 📝 Documentation Improvements
- Fix typos, improve clarity
- Add examples or clarify existing ones
- Update technical documentation in `docs/`

### 🔧 Code Contributions
- Follow the project's [Style Guide](docs/STYLE_GUIDE.md)
- Add tests for new functionality
- Ensure all existing tests continue to pass
- Keep changes focused and atomic

## Code Style

Please follow the established coding conventions outlined in [docs/STYLE_GUIDE.md](docs/STYLE_GUIDE.md). Key points:

- Use C99 standard features only
- Follow the existing naming conventions
- Include appropriate error handling
- Add comments for complex algorithms
- Preserve existing comment styles unless fixing clear errors

## Testing

### Running Tests
```bash
# Run CTest integration
make test

# Run shell test harness
make shell-tests

# Run specific test
cd build && ctest -R test_name
```

### Adding Tests
- Add test files to `test/testfiles/` following existing structure
- Each test case needs:
  - Input file: `testfile.*`
  - Expected checksums: `md5sums.txt`
- Test new format support with real-world archives

## Format Implementation

When adding support for new archive formats:

1. **Research**: Document the format in `docs/internal/`
2. **Implement**: Add format handler in `lib/layers/`
3. **Test**: Include test cases with various format variations
4. **Document**: Update README and architecture docs

## Submission Process

1. **Commit** your changes with clear, descriptive messages:
   ```bash
   git commit -m "Add support for XYZ archive format"
   ```

2. **Push** to your fork:
   ```bash
   git push origin feature/your-feature-name
   ```

3. **Open a Pull Request** with:
   - Clear description of changes
   - Reference to any related issues
   - Test results showing your changes work
   - Updates to documentation if needed

## Review Process

- All contributions will be reviewed before merging
- Reviewers may request changes or improvements
- Please be patient and responsive to feedback
- Once approved, a maintainer will merge your PR

## Getting Help

- Open an issue for questions about the codebase
- Check existing documentation in `docs/`
- Review the architecture documentation for understanding the layer system

## License

By contributing to Munbox, you agree that your contributions will be licensed under the same [MIT License](LICENSE) that covers the project.

## Recognition

Contributors will be acknowledged in release notes and documentation. We appreciate all contributions, from small bug fixes to major feature additions!