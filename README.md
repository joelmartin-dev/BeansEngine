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
`cargo build` (build only)\
**OR**\
`cargo run` (run after build)\
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
- **default**: unlit rasterizer

Running `cargo run` will compile and run the Cornell Box with Blender Suzanne scene as unlit 
rasterized.
