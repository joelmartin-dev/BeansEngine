mod engine_methods;
mod app_methods;

//================== Standard Libraries =================//
use std::u32;
use std::ffi::CStr;

use ash::khr::{surface, swapchain};
//====== Vulkan Types and Functions ======//
use ash::{Device, Instance, vk};
use ash::Entry;

use imgui::Context;
use imgui_rs_vulkan_renderer::Renderer;
use imgui_winit_support::WinitPlatform;
//==== Computer Graphics Mathematic Structures ====//
use nalgebra_glm as glm;

#[cfg(not(debug_assertions))]
const ENABLE_VALIDATION_LAYERS: bool = false;

#[cfg(debug_assertions)]
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

use crate::app_options::AppOptions;
//============================== User-Defined Structs ==============================//
use crate::camera::Camera; // Provides a global MVP matrix a.k.a. Camera
use crate::vertex::Vertex; // Hashable Vertex primitive, with position, colour and uvs
use crate::buffer_structs::SubMesh;

//============================== Application Defaults ==============================//
const SHADER_ROOT_PATH: &'static str = "assets/shaders";

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
}

pub struct DebugGuiContext {
  imgui: Context,
  platform: WinitPlatform,
  renderer: Renderer,

  // Data to change at runtime
  slang_path: String,
  spirv_path: String,
  slang_content: String,
  delta: u128,
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
  options: AppOptions,
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
  
  vertex_data: VertexData,  
  
  // create_indirect_commands
  // indirect_commands: Vec<vk::DrawIndexedIndirectCommand>,
  // indirect_commands_buffer: (vk::Buffer, vk::DeviceMemory),
  
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
