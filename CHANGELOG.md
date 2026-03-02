# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added
- Initial release of EU Skill Reader
- Screen capture and OCR-based skill reading from Entropia Universe skill window
- Multi-template normalized grid matching for digit recognition
- Font atlas extraction from game pak files with SHA-256 validation
- Skill name matching against known skill list
- Automatic page change detection and monitoring
- CSV export of skill data
- Dark mode UI
- Single-instance enforcement
- Entropia Universe installation check on startup
- Decryption key prompt with registry caching (supports hex, escaped hex, base64)
- User disclaimer dialog
- Version info resource and DPI-aware app manifest
- Task runner script (`want.bat`) for build, test, format, and publish commands
- GitHub Actions workflows for CI build and release publishing
- Synthetic regression tests (149 tests)
- PNG-based regression tests (339 rows across 29 images)
