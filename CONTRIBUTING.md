# Contributing

[![Build Status](https://img.shields.io/github/actions/workflow/status/Mi-Bee-Studio/ai-thinker-esp32-cam/release.yml?branch=main)](https://github.com/Mi-Bee-Studio/ai-thinker-esp32-cam/actions) [![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT) [![Platform: ESP32](https://img.shields.io/badge/Platform-ESP32-blue.svg)](https://www.espressif.com/en/products/socs/esp32) [![Version: ESP-IDF 6.0.1](https://img.shields.io/badge/ESP--IDF-6.0.1-green.svg)](https://github.com/espressif/esp-idf)


Thank you for your interest in contributing to the AI-Thinker ESP32-CAM firmware project! We welcome contributions from developers worldwide to help improve this ESP32-based camera firmware solution.

## Getting Started

To get started, follow these steps:

1. Fork the repository on GitHub
2. Create a feature branch (`git checkout -b feature/amazing-feature`)
3. Make your changes following our coding standards
4. Build and test the firmware:

   ```bash
   idf.py build
   ```
5. Commit your changes (`git commit -m 'Add amazing feature'`)
6. Push to the branch (`git push origin feature/amazing-feature`)
7. Open a Pull Request with a clear description of your changes

## Code Style

Follow these coding guidelines to maintain consistency and code quality:

- Follow ESP-IDF coding conventions
- Use consistent indentation (4 spaces)
- Add appropriate comments for complex logic
- Keep changes minimal and focused on a single feature/bugfix

## Development Workflow

Follow these best practices during development:

- Build regularly to ensure no compilation errors
- Test on target hardware before submitting PR
- Include documentation for new features
- Update README if adding new functionality

## Pull Request Guidelines

Follow these guidelines when submitting Pull Requests:

- Title should be descriptive and concise
- Include detailed description of changes
- Reference any related issues
- Ensure all builds pass on CI/CD
- Keep PRs as small as possible for easier review