# Controls
WASDQE -> Camera Translation
Shift -> Translation Speed Modifier
Arrow Keys -> Camera Rotation (smooth)
\-\+ -> FOV
Mouse Motion -> Camera Rotation (jittery)
F -> Toggle Mouse Motion

# Running the Program
Navigate to project root folder
Run the executable: ./bin/GlobalIllumination

# Compilation Requirements
 * VulkanSDK 1.4.313.0 (Windows -> installer.exe, Linux -> tarball and add "source path/to/sdk/setup-env.sh" to .bashrc)
 * Vulkan drivers (should be included with GPU manufacturer's drivers)

## Linux
 * Make
 * Clang

### Ubuntu VM
 * libgl-dev

### Arch VM
No suitable GPU will be detected for the virtual machine.\
Using software rasterisation instead.
 * vulkan-swrast

# Further Ideas
Save precompiled shader paths to number keys for quick-switching
Beauty Pass -> Colour and Gamma Correction, Anti-aliasing, etc.
FXAA