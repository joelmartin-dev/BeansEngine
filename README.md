# Controls

| Key/Button   | Action                          |
| ------------ | ------------------------------- |
| WASDQE       | Camera Translation              |
| LeftShift    | Translation Speed Modifier      |
| Arrow Keys   | Camera Rotation                 |
| - +          | FOV                             |
| Alt+A        | Rebuild acceleration structures |
| Alt+C        | Recompile Shaders               |
| Alt+R        | Reload Shaders                  |

# Compiling
## Example Build:
`cargo build --bin global_illumination -F reference,sponza` (build only)\
**OR**\
`cargo run --bin global_illumination -F reference,sponza` (run after build)\
Add `--release` for release target (long compilation time).
## Requirements
- **rustup**: Rust language toolchain manager (includes **cargo**: https://rust-lang.org/tools/install/)
- **LLVM**: C++ toolchain (for shader-slang-sys)
- **ShaderSlang**: required to generate bindings for shader-slang-sys, can be in **VulkanSDK** if opted for at install

### Recommended
- **VulkanSDK v1.3.281+ w/ ShaderSlang**: not strictly necessary, validation layers throw depth image layout errors 
without it. **v1.3.281** is the Vulkan API version of Ash v0.38.0. 

## Features
### Modes
- **reference**: progressive Monte-Carlo path tracer
- **restir**: progressive ReSTIR path tracer (not implemented)
- **radiance_cascades**: single-shot Radiance Cascades implementation
- **default**: unlit rasterizer

### Scenes
- **sponza**: CryEngine Sponza scene (quantized glTF with KTX2 UASTC textures)
- **default/suzanne**: Cornell Box with Blender Suzanne (quantized glTF with PNG textures)

### Other
- **measure**: disables validation layers in any target, only prints comma-separated frame times in microseconds

Running `cargo run --bin global_illumination` will compile and run the Cornell Box with Blender Suzanne scene as unlit 
rasterized.

## Executable names
- global_illumination
- reference
- restir
- radiance_cascades

Must specify in build line, only impacts executable's name

# Reproducing Measurements
In the `Measuring` folder, there is a script that automates measurements collection, processing, and presentation 
through python scripts. Variables at the top of the file allow the editing of how many tests to run per implementation,
how long each test runs for, and the executable name for each implementation. For the script to work the `Measuring`
folder must be a sibling to the folder containing the executables and the `assets` folder. The initial bash script may 
be ported to a Windows bat script in future.\
To run the automation script:
1. Install Python (initial testing was conducted using v3.13.11)
2. Navigate to the `Measuring` folder in a terminal
3. Create a virtual environment named `.venv` with `python -m venv .venv`
4. Activate the virtual environment with `source .venv/bin/activate`
5. Install the required Python packages with `pip install -r requirements.txt`
6. Deactivate the virtual environment with `deactivate`
7. Ensure your targets are built: `cargo build --bin reference -F reference,sponza --release`, etc.
8. Run `sh automated_testing.sh`
