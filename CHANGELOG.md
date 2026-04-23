# Changelog

All notable changes to this project are documented in this file.

## [v0.2.0-alpha] - 2026-04-24

### Added
- Camera capture pipeline wired end-to-end through service manager and storage service.
- Storage service writes captured frames to SD card and reports success/failure events.
- Event origin mapping aligned to component ids for clearer source-aware routing.
- Release process documentation for tag-triggered GitHub Releases.

### Changed
- Service orchestration now dispatches events by event source component.
- Service command contract simplified to command id and data pointer.
- Payload ownership flow updated for camera-to-storage handoff.
- README updated to reflect current multi-service architecture and dependencies.

### Notes
- This is the second alpha release candidate after v0.1.0-alpha.

## [v0.1.0-alpha] - 2026-04-22

### Added
- Initial service-based architecture with application queue wiring.
- Power service deep-sleep flow.
- CI build workflow and tag-based release publishing.
