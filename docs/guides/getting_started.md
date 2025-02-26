# Getting Started {#getting_started_guide}
\page getting_started_guide Getting Started
\ingroup guides

## What is Signals?
Signals is a modern C++ framework for building modular multimedia applications. It provides:
- Flexible pipeline architecture
- Plugin-based extensibility
- Real-time media processing
- Minimal boilerplate code

## Key Concepts

### Modules
- Basic processing units
- Handle specific media operations
- Connected to form pipeline
- Plugin-based architecture

### Pipelines
- Connect modules together
- Manage data flow
- Handle resource allocation
- Provide error handling

### Data Flow
- Type-safe data passing
- Reference counted containers
- Metadata propagation
- Thread-safe operations

## Quick Setup

1. **Install Dependencies**
   - See @ref building_guide "Building Guide" for platform-specific instructions
   - Ensure all required tools are available

2. **Build the Framework**
   - Follow platform-specific build instructions
   - Use CMake presets for your platform

3. **Run Tests**
   - Build and run unit tests
   - Verify framework functionality

## Next Steps

1. **Learn More**
   - Review @ref mainpage "Architecture Overview"
   - Understand @ref modules "Module System"
   - Explore @ref plugins "Plugin System"

2. **Start Building**
   - Check @ref basic_pipeline_example "Basic Pipeline"
   - Create @ref custom_module_example "Custom Modules"
   - Learn @ref media_processing_example "Media Processing"

3. **Get Help**
   - Review documentation
   - Check GitHub issues
   - Join discussions

## Resources
- Documentation in `docs/`
- Examples in `docs/examples/`
- Unit tests in `tests/`
- Source code in `src/`