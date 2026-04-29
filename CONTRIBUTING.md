# Contributing

## Getting Started

1. Fork the repository
2. Create a feature branch (`git checkout -b feature/amazing-feature`)
3. Make your changes
4. Build and test the firmware:
   ```bash
   idf.py build
   ```
5. Commit your changes (`git commit -m 'Add amazing feature'`)
6. Push to the branch (`git push origin feature/amazing-feature`)
7. Open a Pull Request

## Code Style

- Follow ESP-IDF coding conventions
- Use consistent indentation (4 spaces)
- Add appropriate comments for complex logic
- Keep changes minimal and focused on a single feature/bugfix

## Development Workflow

- Build regularly to ensure no compilation errors
- Test on target hardware before submitting PR
- Include documentation for new features
- Update README if adding new functionality

## Pull Request Guidelines

- Title should be descriptive and concise
- Include detailed description of changes
- Reference any related issues
- Ensure all builds pass on CI/CD
- Keep PRs as small as possible for easier review