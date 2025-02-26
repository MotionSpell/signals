# Contributing to Signals {#contributing_guide}
\page contributing_guide Contributing
\ingroup guides
## Getting Started

1. Fork the repository
2. Create feature branch
3. Setup development environment

## Development Workflow

1. **Code Style**
   - Follow @ref codingstyle "coding style guidelines"
   - Use provided `.clang-format`
   - Run `scripts/reformat.sh` before committing

2. **Making Changes**
   - Keep commits focused
   - Write descriptive commit messages
   - Update documentation if needed

3. **Testing**
   - Add unit tests for new features
   - Run existing tests:
   ```bash
   cmake --build build --target unittests
   ./build/lib/unittests.exe
   ```

4. **Pull Requests**
   - Create PR against main branch
   - Ensure CI passes
   - Add test results
   - Request review

## Code Quality

- Follow C++14 standards
- Keep cyclomatic complexity low
- Add documentation for public APIs
- Maintain test coverage

## Need Help?

- Check existing issues
- Review documentation
- Ask in discussions
