# Documentation Directory

This directory contains all project documentation organized by topic.

## Documents

### Session Tracking
- **[SESSION_NOTES.md](SESSION_NOTES.md)**: Development session history and progress tracking
  - **IMPORTANT**: Update this file at the end of each session
  - Tracks accomplishments, issues, and next steps
  - Essential for resuming work on different machines

### Future Documents
As the project grows, add documentation here:
- Hardware schematics and pin mappings
- API documentation
- Performance benchmarks
- Testing procedures
- Troubleshooting guides

## Documentation Guidelines

### When to Create a New Document
- **Technical specifications**: Hardware details, peripheral configs
- **How-to guides**: Step-by-step procedures
- **Architecture docs**: System design, component interactions
- **Meeting notes**: Design decisions, requirement discussions

### When to Update SESSION_NOTES.md
✅ At the end of each development session
✅ After completing a major feature
✅ Before switching to a different machine
✅ When encountering and solving significant issues

### Document Naming Convention
- Use descriptive names: `FEATURE_NAME.md`, `PERIPHERAL_CONFIG.md`
- Use UPPERCASE for index/important docs: `README.md`, `SESSION_NOTES.md`
- Use lowercase for specific topics: `i2c_setup.md`, `display_config.md`

## Root Directory Files

Keep the project root minimal:
- **CLAUDE.md**: Claude Code development guide (REQUIRED)
- **CMakeLists.txt**: Build configuration
- **prj.conf**: Kconfig settings
- **.gitignore**: Git exclusions

All other documentation belongs in `docs/`.
