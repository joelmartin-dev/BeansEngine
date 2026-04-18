mod engine_methods;
mod app_methods;

//================== Standard Libraries =================//
use std::u32;
use std::ffi::CStr;

use ash::khr::{surface, swapchain};
use ash::khr::acceleration_structure;
//====== Vulkan Types and Functions ======//
use ash::{Device, Instance, vk};
use ash::Entry;

use imgui::Context;
use imgui_rs_vulkan_renderer::Renderer;
use imgui_winit_support::WinitPlatform;
//==== Computer Graphics Mathematic Structures ====//
use nalgebra_glm as glm;

#[cfg(any(not(debug_assertions), feature = "measure"))]
const ENABLE_VALIDATION_LAYERS: bool = false;

#[cfg(all(debug_assertions, not(feature = "measure")))]
const ENABLE_VALIDATION_LAYERS: bool = true;

// Vulkan Validation Layers to enable in Debug mode
const VALIDATION_LAYERS: [&'static CStr; 1] = [
  c"VK_LAYER_KHRONOS_validation"
];

use winit::keyboard::ModifiersState;
//============================ Window Management ============================//
use winit::window::Window; // Platform-agnostic windowing API

//======== Slang to SPIR-V Compilation ========//
use shader_slang as slang;

//=========== Asset Management and Loading ===========//
// #include "ktx.h" // Image loader, for ktxTexture2
// #include "cgltf.h" // Model loader, for cgltf_asset

//============================== User-Defined Structs ==============================//
use crate::camera::Camera; // Provides a global MVP matrix a.k.a. Camera
use crate::vertex::Vertex; // Hashable Vertex primitive, with position, colour and uvs
#[cfg(any(feature = "reference", feature = "restir", feature = "radiance_cascades"))]
use crate::buffer_structs::InstanceLUT; // Structs for passing data to shaders
use crate::buffer_structs::SubMesh;

//============================== Application Defaults ==============================//
// Let's the CPU start working on the next frame before the GPU asks (higher values == latency, CPU too far ahead)
const MAX_FRAMES_IN_FLIGHT: usize = 2;

// Screen resolution defaults
const RES: [u32; 2] = [800, 600];

#[cfg(any(feature = "reference", feature = "radiance_cascades"))]
const WORKGROUP_SIZE: [u32; 2] = [8, 8];

// Maximum number of cascades to calculate, less is permitted. Used to iteratively instantiate render textures
#[cfg(feature = "radiance_cascades")] const MAX_RENDER_TEXTURES: u32 = 4;
// Number of probes in Cascade 0, given as width and height
#[cfg(feature = "radiance_cascades")] const CASCADE_0_PROBES: [u32; 2] = [800, 600];
// Cascade 0 Probes are always square, just define a side length > 1
#[cfg(feature = "radiance_cascades")] const CASCADE_0_RAYS: u32 = 16;

// Stochastic approaches only use 1 render texture
#[cfg(not(feature = "radiance_cascades"))] const MAX_RENDER_TEXTURES: u32 = 1;

const TEXTURES_DESCRIPTOR_ARRAY_LENGTH: u32 = 32;

// Default asset paths
#[cfg(feature = "sponza")] const DEFAULT_MODEL_PATH: &'static str = "assets/sponza/Sponza.gltf";
#[cfg(not(feature = "sponza"))] const DEFAULT_MODEL_PATH: &'static str = "assets/suzanne/SuzanneCornell_Opt.gltf";

const SHADER_ROOT_PATH: &'static str = "assets/shaders/hardware";

#[cfg(feature = "reference")]
const DEFAULT_SLANG_PATH: &'static str = "reference.slang";
#[cfg(feature = "radiance_cascades")]
const DEFAULT_SLANG_PATH: &'static str = "radiance_cascades.slang";
#[cfg(not(any(feature = "reference", feature = "radiance_cascades")))]
const DEFAULT_SLANG_PATH: &'static str = "raster.slang";

const DEFAULT_SPIRV_PATH: &'static str = "assets/shaders/shader.spv";
const FALLBACK_TEXTURE_PATH: &'static str = "assets/fallback.png";

/*===================================================== Terminology ==================================================//
       Surface: an abstraction of an image, something a framebuffer can present to
         Queue: data highways with different capabilities i.e. graphics, compute, transfer
  Queue Family: groups of queues with the same capabilities
     Swapchain: a list of framebuffers the size of which can be determined by how many frames ahead of the GPU you
                will allow the CPU to work on
          View: Describes how to access some data. Vulkan often does not allow non-descript memory access e.g.
                shaders will use an ImageView to modify an Image, not access the Image directly
   Framebuffer: All the ImageViews that represent attachments/render targets e.g. Colour, Z-buffer, G-buffer
    Descriptor: Similar to Views, they provide access to some data. They are organised in sets with defined layouts
                and passed to shaders as needed. Accessors for GPU objects.
      Pipeline: Define data processing flow on the GPU e.g. from glTF vertices to shaded meshes (one Pipeline per
                unique shader)
       Uniform 
        Buffer: A data structure that is passed to shaders whose values are uniform/consistent across all
                invocations of the shader program.
*/

/*=============================================== HOW TO BUILD A VULKAN APP ==========================================//
     A fairly lengthy process for the most boilerplate graphical application. 
     CPU is smart, GPU is dumb but a very willing and hard-working participant. If the CPU can create explicit
     instructions for tasks within the GPU's capabilities, the GPU will execute them as quickly as possible.

  1. Create a Vulkan Instance (loading required function pointers, etc.)
  2. (* Optional) Set up a debug message callback
  3. Create/Get an addressable surface
  4. Pick your preferred hardware
  5. Create a logical abstraction of chosen hardware that can interface with Vulkan, choosing which queues and queue
       families to use. A single queue capable of graphics and transfer is enough for simple rendering
  6. Set up a swapchain
  7. Create respective views for the swapchain members 
  8. Set up a command pool, which will be used to acquire command buffers to submit to your queues
  9. Allocate the command buffers from the command pool (~ one per pipeline per frame in flight, depends how you
       combine pipelines)
 10. Initialise the synchronisation objects you will use for recording the command buffers later
 11. (* Optional) Create the depth buffer (only needed if you intend to do drawcall-order-independent depth sorting)
 12. Define how information will be passed to shaders through DescriptorLayouts (which structs will be passed to
       which shader stage in a Pipeline)
 13. Construct your initial Pipelines (we don't need to know the exact data that will be passed in yet, just how it
       will be passed)
 14. Create the arbitrary data structures referenced in the DescriptorLayouts e.g. uniform buffers
 15. (* Optional) Load your assets
 16. Create the Vertex and Index buffers (unique vertices accessed at draw time by indices, static meshes can share
       the same vertex buffer with offset indices)
 17. Create descriptor pools (like command pools or families for descriptor sets, predefining commonalities and
       placing limitations on descriptor sets)
 18. Allocate for (on device) and construct explicit descriptor sets (At least one per pipeline per frame in flight)
    
     Now everything is in place to render!
*/
/*======================================================== RENDERING WITH VULKAN ========================================================//

*/

// Struct instead of Class as there is no inheritance, variable protection or even multiple instances
// All we need is run(), which calls everything else
#[derive(Default)]
pub struct App {
  pub engine: Option<Engine>,
  pub window: Option<Window>,
  pub modifiers_state: ModifiersState
}

// The most common arguments in one package (often do not implement the Default trait)
pub struct EngineContext {
  _entry: Entry,
  instance: Instance,
  surface: surface::Instance,
  surface_khr: vk::SurfaceKHR,
  physical_device: vk::PhysicalDevice,
  device: Device,
  queue: vk::Queue,
  command_pool: vk::CommandPool,
  swapchain: swapchain::Device,
  swapchain_khr: vk::SwapchainKHR,
  global_session: slang::GlobalSession,
  #[cfg(any(feature = "reference", feature = "radiance_cascades"))] as_device: acceleration_structure::Device,
}

pub struct DebugGuiContext {
  imgui: Context,
  platform: WinitPlatform,
  renderer: Renderer,

  // Data to change at runtime
  model_path: String,
  slang_path: String,
  spirv_path: String,
  delta: u128,
}

#[cfg(any(feature = "reference", feature = "radiance_cascades"))]
#[derive(Default)]
pub struct RayTraceData {
  blas_handles: Vec<vk::AccelerationStructureKHR>,
  blas_instance_buffer: (vk::Buffer, vk::DeviceMemory),
  
  tlas_handle: vk::AccelerationStructureKHR,
  tlas_buffer: (vk::Buffer, vk::DeviceMemory),
  
  blas_instance_luts: Vec<InstanceLUT>,
  blas_instance_lut_buffer: (vk::Buffer, vk::DeviceMemory),
}

#[derive(Default)]
pub struct VertexData {
  vertex_buffer: (vk::Buffer, vk::DeviceMemory),
  index_buffer: (vk::Buffer, vk::DeviceMemory),
  colour_buffer: (vk::Buffer, vk::DeviceMemory),
  uv_buffer: (vk::Buffer, vk::DeviceMemory),
  nrm_buffer: (vk::Buffer, vk::DeviceMemory),
}

#[derive(Default)]
pub struct ImageData {
  images: Vec<(vk::Image, vk::DeviceMemory)>,
  views: Vec<vk::ImageView>,
  sampler: Option<vk::Sampler>,
}

#[derive(Default)]
pub struct Engine {
  context: Option<EngineContext>,
  debug_gui_context: Option<DebugGuiContext>,
  
  surface_format: vk::SurfaceFormatKHR,
  
  swapchain_extent: vk::Extent2D,
  swapchain_present_mode: vk::PresentModeKHR,
  swapchain_image_data: ImageData,
  
  depth_image_data: ImageData,
  
  draw_command_buffers: Vec<vk::CommandBuffer>,
  compute_command_buffers: Vec<vk::CommandBuffer>,

  in_flight_fences: Vec<vk::Fence>,
  current_frame: usize,
  timeline_semaphore: vk::Semaphore,
  timeline_value: u64,
  
  descriptor_set_layout_global: vk::DescriptorSetLayout,
  descriptor_set_layout_material: vk::DescriptorSetLayout,
  
  graphics_pipeline: (vk::PipelineLayout, vk::Pipeline),
  compute_pipeline: (vk::PipelineLayout, vk::Pipeline),
  
  mvp_buffers: Vec<(vk::Buffer, vk::DeviceMemory)>,

  fallback_texture_data: ImageData,
  
  vertices: Vec<Vertex>,
  indices: Vec<u32>,
  submeshes: Vec<SubMesh>,
  gltf_textures_data: ImageData,
  
  vertex_data: VertexData,
  #[cfg(any(feature = "reference", feature = "radiance_cascades"))] triangle_vertex_buffer: (vk::Buffer, vk::DeviceMemory),
  
  #[cfg(any(feature = "reference", feature = "radiance_cascades"))] triangle_index_buffer: (vk::Buffer, vk::DeviceMemory),
  

  // Acceleration Structures
  #[cfg(any(feature = "reference", feature = "radiance_cascades"))] 
  ray_trace_data: RayTraceData,
  
  // create_indirect_commands
  // indirect_commands: Vec<vk::DrawIndexedIndirectCommand>,
  // indirect_commands_buffer: (vk::Buffer, vk::DeviceMemory),
  
  // create_render_texture
  initial_render_texture_extent: vk::Extent2D,
  render_textures_data: ImageData,
  
  // create_descriptor_pools
  descriptor_pool: vk::DescriptorPool,
  
  // create_descriptor_sets
  global_descriptor_sets: Vec<vk::DescriptorSet>,
  material_descriptor_sets: Vec<vk::DescriptorSet>,

  // Main Loop
  // draw_frame
  camera: Camera,
  framebuffer_resized: bool,
  delta: u128,
  runtime: u128,
  frame: u32,
  spirv_path: String,

  old_sun_intensity: f32,
  sun_intensity: f32,
  old_sun_dir: glm::Vec3,
  sun_dir: glm::Vec3,
  old_view: glm::Mat4,
  interval: f32
}
