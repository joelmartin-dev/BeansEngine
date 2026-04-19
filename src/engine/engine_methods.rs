use std::ffi::{CStr, CString, c_void};
use std::fs::{self, File};
use std::io::{Write};
use std::path::{Path};
use std::{u64, f32};

use crate::app_options::AppOptions;
use crate::buffer_structs::{MVP, SubMesh};
use crate::buffer_structs::RasterPushConstant;

use crate::engine::{
  DEFAULT_SLANG_PATH, DEFAULT_SPIRV_PATH, DebugGuiContext, ENABLE_VALIDATION_LAYERS, 
  Engine, EngineContext, FALLBACK_TEXTURE_PATH, ImageData, 
  SHADER_ROOT_PATH, VALIDATION_LAYERS, VertexData
};
use crate::camera::Camera;
use crate::model::CUBE;
use crate::vertex::Vertex;
use ash::util::Align;
use image::EncodableLayout;
use ::image::ImageReader;
use imgui::{Condition, Context, DrawData};
use imgui_rs_vulkan_renderer::{DynamicRendering, Options, Renderer};
use imgui_winit_support::{HiDpiMode, WinitPlatform};
use shader_slang::{self as slang, Downcast};

use ash::ext::debug_utils;
use ash::khr::{surface, swapchain};
use ash::{Entry, Instance, Device, vk};

use nalgebra_glm::{self as glm};
use winit::dpi::PhysicalSize;
use winit::raw_window_handle::{HasDisplayHandle, HasWindowHandle};
use winit::window::Window;

// A logging function made available to the Vulkan API at runtime
// Determines which types and severities of information are passed through
unsafe extern "system" fn debug_callback(
  msg_severity: vk::DebugUtilsMessageSeverityFlagsEXT, 
  _msg_type: vk::DebugUtilsMessageTypeFlagsEXT, 
  p_callback_data: *const vk::DebugUtilsMessengerCallbackDataEXT<'_>, 
  _: *mut c_void
) -> vk::Bool32
{
  // If msg_severity is error, print error else print warning
  let severity = 
    if msg_severity & vk::DebugUtilsMessageSeverityFlagsEXT::ERROR == msg_severity { "error" } 
    else if msg_severity & vk::DebugUtilsMessageSeverityFlagsEXT::WARNING == msg_severity { "warning" } 
    else if msg_severity & vk::DebugUtilsMessageSeverityFlagsEXT::INFO == msg_severity { "info" }
    else { "verbose" };

  println!("validation layer: type {} msg: {}", severity,
    // The message is passed as a pointer to a CStr. Reconstruct the message and convert it to a UTF-8 str slice
    unsafe { CStr::from_ptr((*p_callback_data).p_message) }.to_string_lossy()
  );

  // Result is unused
  return vk::FALSE;
}

impl Engine
{
  pub fn new(window: &Window, options: AppOptions) -> Self
  {
    /*============================================= INSTANTIATION ORDER ==============================================//
      SETUP VULKAN CONTEXT
       1. Load Vulkan functions
       2. Create Vulkan instance
       3. Set up validation layer messenger
       4. Choose a physical device, then create an interfaceable logical device from its capabilities
       5. Select a queue/queues from a queue family (use case dependent, we need graphics-compute-present)
       6. Load the surface-specific functions into an Instance
       7. Create an interfaceable surface from the Winit window
       8. Load the swapchain-specific functions into a Device
       9. Create and populate a swapchain
      10. Establish a command pool (using queue family capabilities) to allocate command buffers from
      11. Load the acceleration structure-specific functions into a Device
      12. Establish a global session for the shader-slang compilation API to create sessions from
    */
    // Similar to Volk, load the Vulkan function pointers from the driver at runtime
    // Persist as struct member (otherwise you lose access to functions)
    let entry: Entry = unsafe { Entry::load().expect("failed to create entry!")};
    
    // Create a vulkan instance
    let instance: Instance = Self::create_instance(&entry, window);

    // Set up a debug messenger (displays validation layer outputs). 
    // Set up ASAP to cover all subsequent API calls
    Self::setup_debug_messenger(&entry, &instance);
    
    // The device extensions required by the application at some point during runtime
    let required_device_extensions = vec![
      vk::KHR_SWAPCHAIN_NAME, vk::KHR_SPIRV_1_4_NAME, vk::KHR_SYNCHRONIZATION2_NAME, vk::KHR_CREATE_RENDERPASS2_NAME, 
      vk::KHR_ACCELERATION_STRUCTURE_NAME, vk::KHR_BUFFER_DEVICE_ADDRESS_NAME, vk::KHR_DEFERRED_HOST_OPERATIONS_NAME, 
      vk::KHR_RAY_QUERY_NAME
    ];

    // Select a physical device
    let physical_device = Self::pick_physical_device(&instance, &required_device_extensions);

    // Create the logical device from the physical device (selecting queues)
    let (device, queue, qfi) = Self::create_logical_device(&instance, physical_device, &required_device_extensions);

    // Set up the surface abstraction, allowing Vulkan to interface with the winit Window as a surface
    let surface: surface::Instance = surface::Instance::new(&entry, &instance); // this platform's surface functions
    let surface_khr: vk::SurfaceKHR = unsafe { ash_window::create_surface(
        &entry, &instance, window.display_handle().unwrap().into(), window.window_handle().unwrap().into(), None
      ).unwrap()  
    };  
    let (surface_capabilities, surface_format) = Self::get_surface_info(&surface, surface_khr, physical_device);

    // Load swapchain functions
    let swapchain = swapchain::Device::new(&instance, &device);
    // Create swapchain
    let (swapchain_present_mode, swapchain_extent) = 
      Self::get_swapchain_info(&surface, surface_khr, surface_capabilities, physical_device, &window);
    let (swapchain_khr, swapchain_images) = Self::create_swapchain(
      &swapchain, surface_khr, surface_capabilities, surface_format, swapchain_extent, swapchain_present_mode);
    let swapchain_image_views = Self::create_swapchain_image_views(&device, &swapchain_images, surface_format.format);
      
    let swapchain_image_data = ImageData {
      images: swapchain_images.iter().map(|&img| (img, vk::DeviceMemory::null())).collect(),
      views: swapchain_image_views,
      sampler: None
    };
    // Establish command pool for command buffer allocation
    let command_pool = Self::create_command_pool(&device, qfi);
    
    // Initialise Shader-Slang Compilation API
    let global_session = Self::init_slang(); // Compile shaders
    
    // Everything needed for any given Vulkan operation
    let context = EngineContext {
      _entry: entry, instance, surface, surface_khr, physical_device, device, 
      queue, command_pool, swapchain, swapchain_khr, global_session
    };
    
    // Set up ImGui
    let mut imgui = Context::create();
    let mut platform = WinitPlatform::new(&mut imgui);
    platform.attach_window(imgui.io_mut(), window, HiDpiMode::Default);

    let depth_format = Self::find_depth_format(&context.instance, context.physical_device);
    let dynamic_rendering = DynamicRendering {
      color_attachment_format: surface_format.format, depth_attachment_format: Some(depth_format)};
    let imgui_options = Options { in_flight_frames: options.frames_in_flight, ..Default::default() };

    // Handles command buffer recording for DearImGui
    let renderer = Renderer::with_default_allocator(
      &context.instance, context.physical_device, context.device.clone(), 
      queue, command_pool, dynamic_rendering, &mut imgui, Some(imgui_options)
    ).unwrap();

    // Initialise the data ImGui can change
    let slang_path = String::from(DEFAULT_SLANG_PATH);
    let spirv_path = String::from(DEFAULT_SPIRV_PATH);
    let delta = 0;

    let debug_gui_context = DebugGuiContext { imgui, platform, renderer, slang_path, spirv_path, delta };

    let fallback_texture_data = {
      let command_buffer = Self::begin_single_time_commands(&context.device, context.command_pool);
      let (image, format, mip_levels) = Self::create_texture_image_from_png(
        &context.instance, &context.device, 
        context.physical_device, command_buffer, &Path::new(FALLBACK_TEXTURE_PATH)
      );
      Self::end_single_time_commands(&context.device, context.queue, command_buffer);
      let view = Self::create_image_view(&context.device, image.0, format, vk::ImageAspectFlags::COLOR, mip_levels);
      ImageData { images: vec![image], views: vec![view], sampler: Some(Self::create_texture_sampler(&context)) }
    };

    let vertices: Vec<Vertex> = CUBE.vertices.clone();
    let indices: Vec<u32> = CUBE.indices.clone();
    let submeshes = vec![SubMesh {
      index_offset: 0, index_count: indices.len() as u32, material_id: 0, 
      first_vertex: 0, max_vertex: vertices.len() as u32, alpha_cut: vk::FALSE
    }];

    // Create the depth stencil
    let depth_image_data = {
      let (depth_image, depth_image_view) = Self::create_depth_resources(&context, swapchain_extent);
      ImageData {
        images: vec![depth_image],
        views: vec![depth_image_view],
        sampler: None
      }
    };

    // Define how data is organised in descriptor sets
    let (descriptor_set_layout_global, descriptor_set_layout_material) = 
      Self::create_descriptor_set_layouts(&context, &fallback_texture_data);
    
    // Create the draw-time GPU synchronisation objects
    let (timeline_semaphore, in_flight_fences) = Self::create_sync_objects(&context, &options);

    // Allocate buffers (one per frame in flight) that record commands for either the compute or graphics pipeline
    let (draw_command_buffers, compute_command_buffers) = Self::create_command_buffers(&context, &options);
    
    // We manually call CompileShader for all shaders on start, ensuring SPIR-V exists by the time pipelines are created
    Self::compile_shader(&context, &Path::new(SHADER_ROOT_PATH).join(DEFAULT_SLANG_PATH), &Path::new(DEFAULT_SPIRV_PATH));

    // Define how data is passed through stages, with an attached descriptor set layout and shader
    let graphics_pipeline = Self::create_graphics_pipeline(
      &context, &Path::new(DEFAULT_SPIRV_PATH), descriptor_set_layout_global, 
      descriptor_set_layout_material, surface_format.format
    );
    let compute_pipeline = Self::create_compute_pipeline(
      &context, &Path::new(DEFAULT_SPIRV_PATH), descriptor_set_layout_global, descriptor_set_layout_material);

    //=========== VERTEX TRANSFORMATION AND ATTRIBUTE INFORMATION AS GPU-ACCESSIBLE AND INDEXABLE BUFFERS ============//
    // Per-frame camera-based transformations
    let mvp_buffers = Self::create_uniform_buffers(&context, &options);

    let vertex_buffer = Self::create_vertex_buffer(&context, &vertices);
    
    let index_buffer = Self::create_index_buffer(&context, &indices, vk::BufferUsageFlags::STORAGE_BUFFER);
    
    let colour_buffer = Self::create_colour_buffer(&context, &vertices);
    let uv_buffer = Self::create_uv_buffer(&context, &vertices);
    let nrm_buffer = Self::create_normal_buffer(&context, &vertices);

    let vertex_data = VertexData { vertex_buffer, index_buffer, colour_buffer, uv_buffer, nrm_buffer };

    // Set limits on the number of descriptor sets that can be allocated at any time
    let descriptor_pool = Self::create_descriptor_pools(&context, &options);
      
    // Organise buffers so that they are accessible on the GPU
    let (global_descriptor_sets, material_descriptor_sets) = Self::create_descriptor_sets(
      &context, &options, &fallback_texture_data, 
      descriptor_set_layout_global, descriptor_set_layout_material, descriptor_pool, &mvp_buffers, 
      &vertices, &indices, &vertex_data
    );

    let camera = Camera::new(
      options.resolution.0, options.resolution.1, glm::vec3(0.0, 0.0, 5.0), 0.0, glm::half_pi::<f32>()
    );

    Self {
      options,
      context: Some(context),
      debug_gui_context: Some(debug_gui_context),
      surface_format,
      swapchain_extent,
      swapchain_present_mode,
      swapchain_image_data,
      depth_image_data,
      draw_command_buffers,
      compute_command_buffers,
      in_flight_fences,
      current_frame: 0,
      timeline_semaphore,
      timeline_value: 0,
      descriptor_set_layout_global,
      descriptor_set_layout_material,
      compute_pipeline,
      graphics_pipeline,
      mvp_buffers,
      fallback_texture_data,
      vertices,
      indices,
      submeshes,
      vertex_data,
      // indirect_commands: Default::default(),
      // indirect_commands_buffer: Default::default(),
      descriptor_pool,
      global_descriptor_sets,
      material_descriptor_sets,
      camera,
      framebuffer_resized: false,
      delta: 0,
      runtime: 0,
      frame: 0,
      spirv_path: String::from(DEFAULT_SPIRV_PATH),
      old_sun_intensity: 1.0,
      sun_intensity: 1.0,
      old_sun_dir: glm::normalize(&glm::vec3(0.6, 0.6, 0.6)),
      sun_dir: glm::normalize(&glm::vec3(0.6, 0.6, 0.6)),
      old_view: Default::default(),
      interval: 10.0,
    }
  }

  // Set up base Vulkan instance and RAII context
  fn create_instance(entry: &Entry, window: &Window) -> Instance
  {
    let app_name = CString::new("Demo").unwrap();
    let engine_name = CString::new("Beans Engine").unwrap();
    // Only apiVersion is relevant to execution, the rest is superfluous
    let app_info: vk::ApplicationInfo = vk::ApplicationInfo::default()
      .api_version(vk::API_VERSION_1_3)
      .application_version(vk::make_api_version(0, 1, 0, 0))
      .application_name(&app_name)
      .engine_version(vk::make_api_version(0, 1, 0, 0))
      .engine_name(&engine_name);
 
    // the only required layers are enabled solely in debug mode
    let mut required_layers: Vec<&CStr> = vec![];
    if ENABLE_VALIDATION_LAYERS {
      required_layers.extend_from_slice(&VALIDATION_LAYERS);
    }
    
    // get available instance layers
    let layer_properties: Vec<vk::LayerProperties> = unsafe { entry.enumerate_instance_layer_properties().unwrap() };

    required_layers.iter().for_each(|&required_layer| {
      let mut layer_is_available = false;
      for layer in &layer_properties {
        if required_layer == layer.layer_name_as_c_str().unwrap() { layer_is_available = true; break; }
      }
      // if none of the available layers match the required layer, throw error
      if !layer_is_available { panic!("Required layer {:?} not available", required_layer); }
    });

    let mut extension_names: Vec<*const i8> = vec![vk::KHR_GET_PHYSICAL_DEVICE_PROPERTIES2_NAME.as_ptr()];
    extension_names.extend_from_slice(
      ash_window::enumerate_required_extensions(window.display_handle().unwrap().into()
    ).unwrap());

    if ENABLE_VALIDATION_LAYERS { extension_names.push(vk::EXT_DEBUG_UTILS_NAME.as_ptr()) };

    let cstr_as_ptr: Vec<*const i8> = required_layers.iter().map(|s| s.as_ptr().try_into().unwrap()).collect();

    // By now we've verified that the required layers and extensions are valid and available
    let create_info = vk::InstanceCreateInfo::default()
      .application_info(&app_info).enabled_extension_names(&extension_names).enabled_layer_names(&cstr_as_ptr); 
    
    return unsafe { entry.create_instance(&create_info, None).unwrap() };
  }
  
  // Associate DebugCallback with the Vulkan instance
  fn setup_debug_messenger(
    entry: &Entry, instance: &Instance
  ) -> Option<(debug_utils::Instance, vk::DebugUtilsMessengerEXT)>
  {
    // Only set up in debug mode
    if !ENABLE_VALIDATION_LAYERS { return None; }

    // Determine which message severities to even consider printing
    let severity_flags = vk::DebugUtilsMessageSeverityFlagsEXT::WARNING | vk::DebugUtilsMessageSeverityFlagsEXT::ERROR;
    
    // Determine which message types to even consider printing
    let message_type_flags = 
      vk::DebugUtilsMessageTypeFlagsEXT::GENERAL | vk::DebugUtilsMessageTypeFlagsEXT::PERFORMANCE |
      vk::DebugUtilsMessageTypeFlagsEXT::VALIDATION;

    // The instantiation data for the debugMessenger
    let debug_utils_messenger_create_info = vk::DebugUtilsMessengerCreateInfoEXT::default()
      .message_severity(severity_flags).message_type(message_type_flags).pfn_user_callback(Some(debug_callback)); 
    
    // Associate the debugMessenger with the instance
    let utils_instance: debug_utils::Instance = debug_utils::Instance::new(entry, instance);
    let debug_messenger = unsafe {
      utils_instance.create_debug_utils_messenger(&debug_utils_messenger_create_info, None)
        .expect("failed to create debug messenger!")
    };
    return Some((utils_instance, debug_messenger));
  }

  // The Vulkan PhysicalDevice represents a series of capabilities available to the logical Device
  // We store this physical device and can query its capabilities and limits whenever
  fn pick_physical_device(instance: &Instance, required_device_extensions: &Vec<&CStr>) -> vk::PhysicalDevice
  {
    // Get all the capable hardware detected by Vulkan
    let physical_devices: Vec<vk::PhysicalDevice> = unsafe {
      instance.enumerate_physical_devices().expect("failed to enumerate physical devices")
    };

    // If there are no devices detected continuing is impossible
    if physical_devices.is_empty() {
      panic!("failed to find any physical devices");
    }

    // Find a physical device that is capable of what we need
    // Ranking multiple devices by capability is possible, but we will assume that the first detected Discrete GPU is good
    // enough, if not try the Integrated GPU (found on most modern APUs and laptops)
    let first_suitable_device = physical_devices.iter().find(|&physical_device| {
      // get the properties of the current device
      let properties = unsafe { instance.get_physical_device_properties(*physical_device) };
      // Check if the device supports the Vulkan 1.3 API version
      let supports_vulkan_1_3 = properties.api_version >= vk::API_VERSION_1_3;
      if !supports_vulkan_1_3 { println!("{:?} does not support Vulkan v1.3.x", *physical_device); return false; }
      // Check if the device is capable of anisotropic sampling (quality transitioning between mip levels)
      let supports_sampler_anisotropy = properties.limits.max_sampler_anisotropy >= 1.0;
      if !supports_sampler_anisotropy { println!("{:?} does not support anisotropic sampling", *physical_device); return false; }
      
      // Get the queue families and their properties of the physical device
      let queue_families = unsafe {
        instance.get_physical_device_queue_family_properties(*physical_device)
      };

      // Check if any of the queue families supports graphics AND compute operations
      // I have not implemented logic for separate queue families
      let supports_graphics_compute: bool = queue_families.iter().any(|&qfp| 
        (qfp.queue_flags & (vk::QueueFlags::GRAPHICS | vk::QueueFlags::COMPUTE)) != vk::QueueFlags::default());
      if !supports_graphics_compute { println!("{:?} does not have single queue graphics compute queue", *physical_device); return false; }

      // Get the device extensions available on this physical device
      let available_device_extensions = unsafe {
        instance.enumerate_device_extension_properties(*physical_device)
          .expect("failed to enumerate device extension properties!")
      };
      
      // Check if ALL required device extensions are available
      let supports_all_required_extensions = required_device_extensions.iter().all(|&extension| {
          // Check if the current requiredDeviceExtension is offered by the physical device
          let extension_supported = available_device_extensions.iter().any(|&available_extension| 
            available_extension.extension_name_as_c_str().unwrap() == extension);
          if extension_supported { true } else { println!("{} not supported!", extension.to_string_lossy()); false }
        }
      );
      if !supports_all_required_extensions { println!("{:?} does not support required extensions", *physical_device); return false; }

      let mut ray_query_features = vk::PhysicalDeviceRayQueryFeaturesKHR::default();
      let mut acceleration_structure_features = vk::PhysicalDeviceAccelerationStructureFeaturesKHR::default();
      let mut extended_dynamic_state_features = vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT::default();
      let mut vulkan_13_features = vk::PhysicalDeviceVulkan13Features::default();
      let mut vulkan_12_features = vk::PhysicalDeviceVulkan12Features::default();

      // Build the pNext chain (in reverse order)
      // Each structure's pNext points to the next one in the chain
      ray_query_features.p_next = std::ptr::null_mut();
      acceleration_structure_features.p_next = 
        &mut ray_query_features as *mut _ as *mut c_void;
      extended_dynamic_state_features.p_next = 
        &mut acceleration_structure_features as *mut _ as *mut c_void;
      vulkan_13_features.p_next = 
        &mut extended_dynamic_state_features as *mut _ as *mut c_void;
      vulkan_12_features.p_next = 
        &mut vulkan_13_features as *mut _ as *mut c_void;

      // Create the main features2 structure with the chain
      let mut features2 = vk::PhysicalDeviceFeatures2::default().push_next(&mut vulkan_12_features);

      // Query the features
      unsafe { instance.get_physical_device_features2(*physical_device, &mut features2); }

      // Vulkan has structs available as templates populated by the driver
      // Each of these structs' members have been set by the device, and we can query the members' availability through
      // them. I don't know why we use a .template getFeatures2, but it works and is visually comprehensible
      // Query those specific features against the available implementation (the device's Vulkan driver)
      let supports_required_min_spec_features = 
        // allows anisotropic sampling to some degree
        features2.features.sampler_anisotropy != vk::FALSE &&
        // simplified API for Vulkan synchronization objects e.g. semaphores, fences
        vulkan_13_features.synchronization2 != vk::FALSE &&
        // allows for implicit render passes
        vulkan_13_features.dynamic_rendering != vk::FALSE &&
        extended_dynamic_state_features.extended_dynamic_state != vk::FALSE &&
        // makes timeline semaphores available for synchronisation
        vulkan_12_features.timeline_semaphore != vk::FALSE;
        
      if !supports_required_min_spec_features { 
        println!("{:?} does not support required features", *physical_device); return false; }

      let supports_required_hardware_features =
        vulkan_12_features.descriptor_binding_uniform_buffer_update_after_bind != vk::FALSE &&
        vulkan_12_features.descriptor_binding_sampled_image_update_after_bind != vk::FALSE &&
        vulkan_12_features.descriptor_binding_storage_image_update_after_bind != vk::FALSE &&
        vulkan_12_features.descriptor_binding_storage_buffer_update_after_bind != vk::FALSE &&
        vulkan_12_features.descriptor_binding_partially_bound != vk::FALSE &&
        vulkan_12_features.descriptor_binding_variable_descriptor_count != vk::FALSE &&
        vulkan_12_features.runtime_descriptor_array != vk::FALSE &&
        vulkan_12_features.shader_sampled_image_array_non_uniform_indexing != vk::FALSE &&
        vulkan_12_features.host_query_reset != vk::FALSE &&
        vulkan_12_features.buffer_device_address != vk::FALSE &&
        acceleration_structure_features.acceleration_structure != vk::FALSE &&
        acceleration_structure_features.descriptor_binding_acceleration_structure_update_after_bind != vk::FALSE &&
        ray_query_features.ray_query != vk::FALSE;

      return supports_vulkan_1_3 &&
        supports_sampler_anisotropy &&
        supports_graphics_compute && 
        supports_all_required_extensions &&
        supports_required_min_spec_features &&
        supports_required_hardware_features;
    }).expect("failed to find a suitable physical device!");
    return *first_suitable_device;
  }

  // Set up as single queue for all needs
  // Technically, we checked for this when we found a suitable device and just need to get the index of the suitable
  // queue family HOWEVER we are able to double check that the queue does exist as a side effect of looking
  fn find_queue_families(instance: &Instance, physical_device: vk::PhysicalDevice) -> u32
  {  
    // The properties of each queue family available on the physical device
    let queue_family_properties = unsafe { instance.get_physical_device_queue_family_properties(physical_device) };

    // Find the queue family that supports queues capable of Graphics AND Compute
    let qfi: u32 = queue_family_properties.iter().position(|&qfp| 
      qfp.queue_flags & (vk::QueueFlags::GRAPHICS | vk::QueueFlags::COMPUTE) != vk::QueueFlags::default())
        .expect("failed to find a suitable queue for Graphics AND Compute!").try_into().unwrap();
    
    // return the index of the queue with a graphics queue family
    return qfi;
  }

  // A Device is an instance of a PhysicalDevice's Vulkan implementation with its own state and resources
  fn create_logical_device(
    instance: &Instance, physical_device: vk::PhysicalDevice, required_device_extensions: &Vec<&CStr>
  ) -> (Device, vk::Queue, u32)
  {
    // Find the index of the Graphics Compute queue family that we know exists on the physical device
    let qfi = Self::find_queue_families(instance, physical_device);

    let mut vulkan_12_features = vk::PhysicalDeviceVulkan12Features::default()
      .descriptor_binding_uniform_buffer_update_after_bind(true)
      .descriptor_binding_sampled_image_update_after_bind(true)
      .descriptor_binding_storage_image_update_after_bind(true)
      .descriptor_binding_storage_buffer_update_after_bind(true)
      .descriptor_binding_partially_bound(true)
      .descriptor_binding_variable_descriptor_count(true)
      .runtime_descriptor_array(true)
      .shader_sampled_image_array_non_uniform_indexing(true)
      .host_query_reset(true)
      .buffer_device_address(true)
      .timeline_semaphore(true);
    let mut vulkan_13_features = vk::PhysicalDeviceVulkan13Features::default()
      .synchronization2(true)
      .dynamic_rendering(true);
    let mut extended_dynamic_state_features = vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT::default()
      .extended_dynamic_state(true);
    let mut acceleration_structure_features = vk::PhysicalDeviceAccelerationStructureFeaturesKHR::default()
      .acceleration_structure(true)
      .descriptor_binding_acceleration_structure_update_after_bind(true);
    let mut ray_query_features = vk::PhysicalDeviceRayQueryFeaturesKHR::default()
      .ray_query(true);

    // Build the pNext chain (in reverse order)
    // Each structure's pNext points to the next one in the chain
    // compute_shader_derivatives_features.p_next = std::ptr::null_mut();
    ray_query_features.p_next = std::ptr::null_mut();
        // &mut compute_shader_derivatives_features as *mut _ as *mut c_void;
    acceleration_structure_features.p_next = 
        &mut ray_query_features as *mut _ as *mut c_void;
      extended_dynamic_state_features.p_next = 
        &mut acceleration_structure_features as *mut _ as *mut c_void;
    vulkan_13_features.p_next = 
      &mut extended_dynamic_state_features as *mut _ as *mut c_void;
    vulkan_12_features.p_next = 
      &mut vulkan_13_features as *mut _ as *mut c_void;

    // Create the main features2 structure with the chain
    let mut features2 = vk::PhysicalDeviceFeatures2::default()
      .features(
        vk::PhysicalDeviceFeatures::default()
          .sampler_anisotropy(true)
        ).push_next(&mut vulkan_12_features);
    
    //============================================== Devices and Queues ==============================================//

    // Useful for multi-queue operation (which we're not doing but we need memory initialised for such purpose)
    let queue_priority = [0.0];

    // The logical Device must be created with information about which type and how many queues will be used by the app
    let device_queue_create_info = [vk::DeviceQueueCreateInfo::default()
      .queue_family_index(qfi)
      .queue_priorities(&queue_priority)];

    let cstr_as_ptr: Vec<*const i8> = required_device_extensions.iter().map(|s| s.as_ptr() as *const i8).collect();

    // collate all the information needed to create the Device
    let device_create_info = vk::DeviceCreateInfo::default()
      .push_next(&mut features2)
      .queue_create_infos(&device_queue_create_info)
      .enabled_extension_names(&cstr_as_ptr);

    // Create the Device, then create the Queue from the Device
    let device = unsafe { 
      instance.create_device(physical_device, &device_create_info, None)
      .expect("failed to create logical device!") 
    };
    let queue = unsafe { device.get_device_queue(qfi, 0) };
    return (device, queue, qfi);
  }

  /*================================= Helper Functions for Swap Chain Initialisation =================================*/  
  pub fn set_framebuffer_resized(&mut self) {
    self.framebuffer_resized = true;
  }

  // Choose B8G8R8A8_SRGB and SRGB Non Linear colour space if available, else fallback to the first available format
  // BGRA seems to be preferred by older drivers and hardware, newer are ambivalent
  fn choose_swap_surface_format(available_formats: Vec<vk::SurfaceFormatKHR>) -> vk::SurfaceFormatKHR
  {
    let format = available_formats.iter().find(|available_format| 
      available_format.format == vk::Format::B8G8R8A8_SRGB && 
      available_format.color_space == vk::ColorSpaceKHR::SRGB_NONLINEAR
    );
  
    if format.is_none() { available_formats[0] } else { *format.unwrap() }
  }

  // Prefer Mailbox present mode, fallback to Fifo (always supported). Both are Vsync present modes: Mailbox has a
  // single-entry wait queue replacing entry with newest, Fifo can have multiple entries (consumes each in order).
  fn choose_swap_present_mode(available_present_modes: Vec<vk::PresentModeKHR>) -> vk::PresentModeKHR
  {
    // Check the surface is Mailbox-capable
    let present_mode = available_present_modes.iter().find(|&available_present_mode| 
      *available_present_mode == vk::PresentModeKHR::MAILBOX);

    // Prefer Mailbox, fallback Fifo
    return if present_mode.is_none() { vk::PresentModeKHR::FIFO } 
           else { *present_mode.unwrap() }
  }

  // Match the extent of the surface. If the surface is acting up, match the extent of the framebuffer
  pub fn choose_swap_extent(
    capabilities: vk::SurfaceCapabilitiesKHR, framebuffer_extent: PhysicalSize<u32>
  ) -> vk::Extent2D
  {
    // Match the framebuffer's extent as closely as the surface is capable
    return if capabilities.current_extent.width != u32::MAX {
      capabilities.current_extent
    } else {
      vk::Extent2D {
        width: framebuffer_extent.width.clamp(
          capabilities.min_image_extent.width, capabilities.max_image_extent.width),
        height: framebuffer_extent.height.clamp(
          capabilities.min_image_extent.height, capabilities.max_image_extent.height)
      }
    }
  }

  // Get the minimum image count necessary (we assume 3, could be more, could be less)
  fn choose_swap_min_image_count(capabilities: vk::SurfaceCapabilitiesKHR) -> u32
  {
    // There is a minimum count of images required for the swap chain to function
    let min_image_count = capabilities.min_image_count.max(3);

    // Clamp to the maxImageCount so long as maxImageCount has a maximum and is < than minImageCount
    // Why don't we just use std::clamp? Because if the surface had no maxImageCount the result would always clamp to 0
    return if capabilities.max_image_count > 0 && min_image_count > capabilities.max_image_count {
      capabilities.max_image_count
    }
    else { min_image_count }
  }

  fn get_surface_info(
    surface: &surface::Instance, surface_khr: vk::SurfaceKHR, physical_device: vk::PhysicalDevice
  ) -> (vk::SurfaceCapabilitiesKHR, vk::SurfaceFormatKHR)
  {
    // See what the surface is capable of
    let surface_capabilities = unsafe {
      surface.get_physical_device_surface_capabilities(physical_device, surface_khr)
        .expect("failed to get physical device's surface capabilities")
    };
    
    // Try to get the preferred format, or fallback
    let surface_format = Self::choose_swap_surface_format(unsafe {
      surface.get_physical_device_surface_formats(physical_device, surface_khr)
        .expect("failed to get physical device's surface formats!")
    });

    return (surface_capabilities, surface_format);
  }

  fn get_swapchain_info(
    surface: &surface::Instance, surface_khr: vk::SurfaceKHR, surface_capabilities: vk::SurfaceCapabilitiesKHR, 
    physical_device: vk::PhysicalDevice, window: &Window
  ) -> (vk::PresentModeKHR, vk::Extent2D)
  {
    // Try to get the preferred present mode, or fallback
    let swapchain_present_mode = Self::choose_swap_present_mode(unsafe {
      surface.get_physical_device_surface_present_modes(physical_device, surface_khr)
      .expect("failed to get physical device's surface present modes!")
    });
    
    let swapchain_extent = Self::choose_swap_extent(surface_capabilities, window.inner_size());
    
    return (swapchain_present_mode, swapchain_extent);
  }
  /*============================================ END SWAP CHAIN HELPERS ==============================================*/

  // The chain of framebuffers, alternated between which is being written to and which is being presented
  fn create_swapchain(
    swapchain: &swapchain::Device, surface_khr: vk::SurfaceKHR,
    surface_capabilities: vk::SurfaceCapabilitiesKHR, surface_format: vk::SurfaceFormatKHR, 
    swapchain_extent: vk::Extent2D, swapchain_present_mode: vk::PresentModeKHR
  ) -> (vk::SwapchainKHR, Vec<vk::Image>)
  {
    // Collate the swap chain information
    let swapchain_create_info = vk::SwapchainCreateInfoKHR::default()
      .surface(surface_khr)
      .min_image_count(Self::choose_swap_min_image_count(surface_capabilities))
      .image_format(surface_format.format)
      .image_color_space(surface_format.color_space)
      .image_extent(swapchain_extent)
      .image_array_layers(1)
      .image_usage(vk::ImageUsageFlags::COLOR_ATTACHMENT)
      .image_sharing_mode(vk::SharingMode::EXCLUSIVE)
      .pre_transform(surface_capabilities.current_transform)
      .composite_alpha(vk::CompositeAlphaFlagsKHR::OPAQUE)
      .present_mode(swapchain_present_mode)
      .clipped(true);
    
    let swapchain_khr = unsafe {
      swapchain.create_swapchain(&swapchain_create_info, None).expect("failed to create swapchain")
    };
    let swapchain_images = unsafe {
      swapchain.get_swapchain_images(swapchain_khr).expect("failed to get swapchain images")
    };
    return (swapchain_khr, swapchain_images);
  }

  // Might be the most straight-forward function, simple input-output
  fn create_image_view(
    device: &Device, image: vk::Image, format: vk::Format, aspect_flags: vk::ImageAspectFlags, mip_levels: u32
  ) -> vk::ImageView
  {
    let view_info = vk::ImageViewCreateInfo::default()
      .image(image)
      .view_type(vk::ImageViewType::TYPE_2D)
      .format(format)
      .subresource_range(
        vk::ImageSubresourceRange::default()
          .aspect_mask(aspect_flags)
          .base_mip_level(0)
          .level_count(mip_levels)
          .base_array_layer(0)
          .layer_count(1)
      );

    return unsafe { device.create_image_view(&view_info, None).expect("failed to create image view!") };
  }

  // The swap chain images are accessed just like any other images, they just serve a specific purpose
  fn create_swapchain_image_views(
    device: &Device, swapchain_images: &Vec<vk::Image>, format: vk::Format
  ) -> Vec<vk::ImageView>
  {
    // The swap chain may be recreated at runtime, ensure the views are replaced
    let mut swapchain_image_views: Vec<vk::ImageView> = vec![];
    swapchain_image_views.reserve(swapchain_images.len());

    // Create identical views, one for each image
    for image in swapchain_images {
      swapchain_image_views.push(
        Self::create_image_view(device, *image, format, vk::ImageAspectFlags::COLOR, 1));   
    }

    return swapchain_image_views;
  }

  fn find_memory_type(
    instance: &Instance, physical_device: vk::PhysicalDevice, 
    required_properties: vk::MemoryRequirements, properties: vk::MemoryPropertyFlags
  ) -> u32
  {
    // typeFilter is a bitmask, and we iterate over it by shifting 1 by i
    // then we check if it has the same properties as properties
    let mem_properties = unsafe { instance.get_physical_device_memory_properties(physical_device) };
    for i in 0..mem_properties.memory_type_count {
      if (required_properties.memory_type_bits & (1 << i) != 0) && 
         (mem_properties.memory_types[i as usize].property_flags.contains(properties)) {
        return i;
      }
    }

    // if you got here, it's wrong
    panic!("failed to find suitable memory type!");
  }

  // Allocate DeviceMemory for an image, return handles to the objects
  fn create_image(
    instance: &Instance, device: &Device, physical_device: vk::PhysicalDevice, width: u32, height: u32, mip_levels: u32, 
    format: vk::Format, tiling: vk::ImageTiling, usage: vk::ImageUsageFlags, properties: vk::MemoryPropertyFlags
  ) -> (vk::Image, vk::DeviceMemory)
  {
    let image_info = vk::ImageCreateInfo::default()
      .image_type(vk::ImageType::TYPE_2D)
      .format(format)
      .extent(vk::Extent3D::default().width(width).height(height).depth(1))
      .mip_levels(mip_levels)
      .array_layers(1)
      .samples(vk::SampleCountFlags::TYPE_1)
      .tiling(tiling)
      .usage(usage)
      .sharing_mode(vk::SharingMode::EXCLUSIVE);
    let image = unsafe { device.create_image(&image_info, None).expect("failed to create image!") };

    // Back up the image on DEVICE_LOCAL memory
    let mem_requirements = unsafe { device.get_image_memory_requirements(image) };
    let alloc_info = vk::MemoryAllocateInfo::default()
      .allocation_size(mem_requirements.size)
      .memory_type_index(Self::find_memory_type(instance, physical_device, mem_requirements, properties));

    let image_memory = unsafe { device.allocate_memory(&alloc_info, None).expect("failed to allocate device memory!") };
    unsafe { device.bind_image_memory(image, image_memory, 0).expect("failed to bind image memory!") };

    return (image, image_memory);
  }

  // Retrieve a usable depth format, not especially important it just needs to work
  fn find_depth_format(instance: &Instance, physical_device: vk::PhysicalDevice) -> vk::Format
  {
    // We don't require a stencil, but if a format containing a stencil is the first available it is welcome
    return Self::find_supported_format(
      instance, physical_device,
      vec![vk::Format::D32_SFLOAT, vk::Format::D32_SFLOAT_S8_UINT, vk::Format::D24_UNORM_S8_UINT],
      vk::ImageTiling::OPTIMAL,
      vk::FormatFeatureFlags::DEPTH_STENCIL_ATTACHMENT
    );
  }

  // Check for some desired format-capabilities against those available
  fn find_supported_format(
    instance: &Instance, physical_device: vk::PhysicalDevice, candidates: Vec<vk::Format>, 
    tiling: vk::ImageTiling, features: vk::FormatFeatureFlags
  ) -> vk::Format
  {
    // Iterate through candidate formats until one possesses the desired capabilities
    let format = candidates.iter().find(|&candidate| {
      let properties = unsafe { instance.get_physical_device_format_properties(physical_device, *candidate) };
      return ((tiling == vk::ImageTiling::LINEAR)  && ((properties.linear_tiling_features & features)  == features)) ||
             ((tiling == vk::ImageTiling::OPTIMAL) && ((properties.optimal_tiling_features & features) == features));
    }).expect("failed to find supported format!");

    return *format;
  }

  // Create the depth buffer, shared between all framebuffers
  // The depth buffer is not used during presentation - it can be safely accessed without affecting the colour results
  fn create_depth_resources(
    context: &EngineContext, extent: vk::Extent2D
  ) -> ((vk::Image, vk::DeviceMemory), vk::ImageView)
  {
    let instance = &context.instance;
    let device = &context.device;
    let physical_device = context.physical_device;
    
    // Get an available depth format (from a selection made within FindDepthFormat)
    let depth_format = Self::find_depth_format(instance, physical_device);
    
    // Create a depth image
    let image = Self::create_image(
      instance, device, physical_device,
      extent.width, extent.height, 1,
      depth_format, vk::ImageTiling::OPTIMAL,
      vk::ImageUsageFlags::DEPTH_STENCIL_ATTACHMENT,
      vk::MemoryPropertyFlags::DEVICE_LOCAL
    );
    
    // Create the view
    let image_view = Self::create_image_view(
      device, image.0, depth_format, vk::ImageAspectFlags::DEPTH, 1);

    return (image, image_view);
  }

  // All command buffers are allocated from a pool
  // We need to know from which queue family the pool is connected
  fn create_command_pool(device: &Device, qfi: u32) -> vk::CommandPool
  {
    // How will we use the command buffers from this pool, and which queue family will they be from
    let command_pool_info = vk::CommandPoolCreateInfo::default()
      .flags(vk::CommandPoolCreateFlags::RESET_COMMAND_BUFFER) // required to allow command buffer resetting
      .queue_family_index(qfi);

    return unsafe { 
      device.create_command_pool(&command_pool_info, None).expect("failed to create command pool!") 
    };
  }

  /*========================================== Why multiple command buffers? ===========================================//
      1. We want to record both compute and graphics in the same frame
      2. Some commands in graphics rely on compute being completed before they operate. Once a command buffer has been
        submitted it cannot be reused that cycle (command buffer cannot be in pending state while recording)
      
      Multiple submits requires multiple command buffers.
  */

  // Allocate the command buffers from the pool and device
  fn create_command_buffers(context: &EngineContext, options: &AppOptions) -> (Vec<vk::CommandBuffer>, Vec<vk::CommandBuffer>)
  {
    let device = &context.device; let command_pool = context.command_pool;
    let alloc_info = vk::CommandBufferAllocateInfo::default()
      .command_pool(command_pool) // the pool to allocate from
      .level(vk::CommandBufferLevel::PRIMARY) // will be submitted directly to queue
      .command_buffer_count(options.frames_in_flight.try_into().unwrap()); // how many buffers to allocate for

    let draw_command_buffers = unsafe { 
      device.allocate_command_buffers(&alloc_info).expect("failed to allocate command buffers!") 
    };
    let compute_command_buffers = unsafe { 
      device.allocate_command_buffers(&alloc_info).expect("failed to allocate command buffers!") 
    };
    return (draw_command_buffers, compute_command_buffers);
  }

  // We need a way to synchronise queue operations (submit, present, dispatch)
  // The CPU and GPU can work on things in parallel, but some are dependent on others and need to wait
  fn create_sync_objects(context: &EngineContext, options: &AppOptions) -> (vk::Semaphore, Vec<vk::Fence>)
  {
    let device = &context.device;
    /*========================================== SYNCHRONISATION OBJECT TYPES ==========================================//
        Objects in some memory that have some state tied to operations. Can be CPU-only, GPU-only, CPU-to-GPU, 
        GPU-to-CPU, GPU<->CPU
            Mutex: CPU-only. Indicates mutual exclusion of execution by thread while held. Used for "critical
                    sections"; parts of code that require sole access to their accessed resources while executing
        Binary
        Semaphores: GPU-only. Simple signalling on completion of an attached batch of GPU commands
            Fences: GPU-to-CPU. Attaches to a queue submission (GPU) and waited on by host with WaitForFences (CPU)
        Timeline
        Semaphores: GPU<->CPU. Uses an integer counter incremented by semaphores, host waits for some integer.

        This program uses a mutex during texture transcoding operations, a fence for swapping between swap chain
        members, and a timeline semaphore for 
    */

    // timelineSemaphore starting at 0
    let mut semaphore_info = vk::SemaphoreTypeCreateInfo::default()
      .semaphore_type(vk::SemaphoreType::TIMELINE).initial_value(0);
    let timeline_semaphore = unsafe {
      device.create_semaphore(&vk::SemaphoreCreateInfo::default().push_next(&mut semaphore_info), None)
        .expect("failed to create timeline semaphore!")
    };
    
    // Fences for swapping between swap chain images
    let mut in_flight_fences: Vec<vk::Fence> = vec![];
    for _i in 0..options.frames_in_flight {
      in_flight_fences.push(unsafe { 
        device.create_fence(&vk::FenceCreateInfo::default(), None).expect("failed to create fence!")
      });
    }
    return (timeline_semaphore, in_flight_fences);
  }

  // Load PNG texture from path into textureImages[idx]
  fn create_texture_image_from_png(
    instance: &Instance, device: &Device,
    physical_device: vk::PhysicalDevice, command_buffer: vk::CommandBuffer, texture_path: &Path
  ) -> ((vk::Image, vk::DeviceMemory), vk::Format, u32)
  {
    let texture = ImageReader::open(texture_path).unwrap().decode().unwrap();

    let width = texture.width(); let height = texture.height();
    let binding = texture.into_rgba8();
    let texture_data = binding.as_bytes();
    let image_data_size = texture_data.len().try_into().unwrap();

    let mip_levels = 1; let format = vk::Format::R8G8B8A8_SRGB;

    // We don't need host visibility once it's written to the GPU, copy into memory heap 0 after creation
    // Only creates a buffer, data inside is uninitialised
    let (staging_buffer, staging_buffer_memory) = Self::create_buffer(
      instance, device, physical_device, image_data_size,
      vk::BufferUsageFlags::TRANSFER_SRC,
      vk::MemoryPropertyFlags::HOST_VISIBLE | vk::MemoryPropertyFlags::HOST_COHERENT,
    );

    // Map the required host-visible GPU memory, copy ktxTextureData in, then unmap
    unsafe {
      let data = 
        device.map_memory(staging_buffer_memory, 0, image_data_size, vk::MemoryMapFlags::default())
        .expect("failed to map texture memory!");
      let mut align = Align::new(data, size_of::<u8>().try_into().unwrap(), image_data_size);
      align.copy_from_slice(&texture_data);
      device.unmap_memory(staging_buffer_memory);
    }

    // Initialise Image
    let base_texture_image = Self::create_image(
      instance, device, physical_device,
      width, height, mip_levels, format,
      vk::ImageTiling::OPTIMAL,
      vk::ImageUsageFlags::TRANSFER_DST | vk::ImageUsageFlags::SAMPLED,
      vk::MemoryPropertyFlags::DEVICE_LOCAL
    );

    // Get the image ready for copying to
    Self::transition_image_layout(
      device, command_buffer,
      base_texture_image.0, vk::ImageLayout::UNDEFINED, vk::ImageLayout::TRANSFER_DST_OPTIMAL, mip_levels);
    // Copy the host-visible buffer to somewhere else in GPU memory
    Self::copy_buffer_to_image(
      device, command_buffer, staging_buffer, base_texture_image.0, width, height, mip_levels, &vec![0]);
    // Get the image ready for sampling in the shader
    Self::transition_image_layout(
      device, command_buffer, base_texture_image.0, vk::ImageLayout::TRANSFER_DST_OPTIMAL, 
      vk::ImageLayout::SHADER_READ_ONLY_OPTIMAL, mip_levels);
    
    return (base_texture_image, format, mip_levels);
  }

  // Specifies how a shader retrieves texture information
  fn create_texture_sampler(context: &EngineContext) -> vk::Sampler
  {
    let instance = &context.instance;
    let physical_device = &context.physical_device;
    let device = &context.device;

    let anisotropy = 
      unsafe {instance.get_physical_device_properties(*physical_device)}.limits.max_sampler_anisotropy;
    // vk::PhysicalDeviceProperties properties = physicalDevice.getProperties();
    // eLinear interpolates based on subtexel coordinates
    // eRepeat tiles the image for texture coordinates outside of [float2(0.0), float2(1.0)]
    // mipLodBias is like an offset for mip levels
    // Anisotropy preserves parallel lines in textures when viewed from oblique angles by sampling a kernel of texels
    // with a bias reflecting the shape of the surface after the MVP matrix is applied (there are great visualisations
    // online)
    // Changing minLod will change which mipLevel to start from
    // Mess around with the options, see what happens
    let sampler_info = vk::SamplerCreateInfo::default()
      .mag_filter(vk::Filter::LINEAR).min_filter(vk::Filter::LINEAR)
      .mipmap_mode(vk::SamplerMipmapMode::LINEAR)
      .address_mode_u(vk::SamplerAddressMode::REPEAT)
      .address_mode_v(vk::SamplerAddressMode::REPEAT)
      .address_mode_w(vk::SamplerAddressMode::REPEAT)
      .mip_lod_bias(0.0).anisotropy_enable(false)
      .compare_enable(false).compare_op(vk::CompareOp::ALWAYS)
      .min_lod(0.0).max_lod(vk::LOD_CLAMP_NONE)
      .anisotropy_enable(true)
      .max_anisotropy(anisotropy);

    let base_texture_sampler = unsafe {
        device.create_sampler(&sampler_info, None).expect("failed to create texture sampler!")
    };
    return base_texture_sampler;
  }

  /*============================================== USING GENERIC TYPES ===============================================//
      Generic types can be used when the operations within a function are type-agnostic within appropriate use cases
      but the return type must be defined when called.
      In our case, we know that we are operating on data structures of known size solely containing floats. Execution
      logic of getAccessorData is identical for all cases but working on varying numbers of components.
  */

  // Allocate and start recording a one-time command buffer
  fn begin_single_time_commands(device: &Device, command_pool: vk::CommandPool) -> vk::CommandBuffer
  {
    // Same allocInfo as computeCommandBuffers and drawCommandBuffers
    let alloc_info = vk::CommandBufferAllocateInfo::default()
      .command_pool(command_pool)
      .level(vk::CommandBufferLevel::PRIMARY)
      .command_buffer_count(1);

    // We only need one command buffer but there is no suitable constructor for a single buffer, so grab the first
    let command_buffer = unsafe {
        device.allocate_command_buffers(&alloc_info).expect("failed to allocate command buffers!")
    }[0];

    // Start recording the command buffer (with the understanding that it will only been used once)
    let begin_info = vk::CommandBufferBeginInfo::default().flags(vk::CommandBufferUsageFlags::ONE_TIME_SUBMIT);

    unsafe { device.begin_command_buffer(command_buffer, &begin_info).expect("failed to being command buffer!") };

    return command_buffer;
  }

  // Stop recording, submit and wait for completion of commandBuffer
  fn end_single_time_commands(device: &Device, queue: vk::Queue, command_buffer: vk::CommandBuffer)
  {
    let command_buffers = [command_buffer];
    // Stop recording
    unsafe { device.end_command_buffer(command_buffer).expect("failed to end command buffer!") };

    // Submit to queue
    let submit_info = vk::SubmitInfo::default().command_buffers(&command_buffers);

    unsafe { 
      device.queue_submit(queue, &[submit_info], vk::Fence::null())
        .expect("failed to submit single time commands to queue!") 
    };

    // Wait until submission completed
    unsafe { device.queue_wait_idle(queue).expect("failed to wait for queue idle!") };
  }

  // Transition-only command buffer submission
  fn transition_image_layout(
    device: &Device, command_buffer: vk::CommandBuffer, image: vk::Image, 
    old_layout: vk::ImageLayout, new_layout: vk::ImageLayout, mip_levels: u32
  )
  {
    // An ImageMemoryBarrier is like a critical section for image memory operations. When we hit the srcStage we check how
    // we were accessing and define the next stage the Image will be used in and how it will be accessed
    let mut barrier = vk::ImageMemoryBarrier::default()
      .old_layout(old_layout).new_layout(new_layout).image(image)
      .subresource_range(vk::ImageSubresourceRange::default()
        .aspect_mask(vk::ImageAspectFlags::COLOR)
        .base_mip_level(0).level_count(mip_levels)
        .base_array_layer(0).layer_count(1)
      );

    // We intentionally support only the following transitions
    // Texture of some material getting ready to be copied to
    if old_layout == vk::ImageLayout::UNDEFINED && 
       new_layout == vk::ImageLayout::TRANSFER_DST_OPTIMAL {
      // How the subresources have/will be accessed at the stages
      barrier.src_access_mask = vk::AccessFlags::default();
      barrier.dst_access_mask = vk::AccessFlags::TRANSFER_WRITE;

      // The stage at which to begin barricading, and when to end. 
      // As in where the last write took place -> where we pick up
      let src_stage = vk::PipelineStageFlags::TOP_OF_PIPE;
      let dst_stage = vk::PipelineStageFlags::TRANSFER;

      // Attach the barrier to the command buffer
      unsafe { 
        device.cmd_pipeline_barrier(
          command_buffer, src_stage, dst_stage, 
          vk::DependencyFlags::default(), &[], 
          &[], &[barrier]) 
      };
    }
    // Texture of some model after being copied to from host-visible memory
    else if old_layout == vk::ImageLayout::TRANSFER_DST_OPTIMAL && 
            new_layout == vk::ImageLayout::SHADER_READ_ONLY_OPTIMAL {
      // Once transferred, we will only be using this for sampling in the Fragment stage
      barrier.src_access_mask = vk::AccessFlags::TRANSFER_WRITE;
      barrier.dst_access_mask = vk::AccessFlags::SHADER_READ;

      // The stage at which to begin barricading, and when to end. 
      // As in where the last write took place -> where we pick up
      let src_stage = vk::PipelineStageFlags::TRANSFER;
      let dst_stage = vk::PipelineStageFlags::FRAGMENT_SHADER;
      
      // Attach the barrier to the command buffer
      unsafe { 
        device.cmd_pipeline_barrier(
          command_buffer, src_stage, dst_stage, 
          vk::DependencyFlags::default(), &[], 
          &[], &[barrier]) 
      };
    }
    // Transition render texture
    else if old_layout == vk::ImageLayout::UNDEFINED && 
            new_layout == vk::ImageLayout::GENERAL {
      // Not confident this is correct
      barrier.src_access_mask = vk::AccessFlags::SHADER_WRITE;
      barrier.dst_access_mask = vk::AccessFlags::SHADER_READ;

      // The stage at which to begin barricading, and when to end. 
      // As in where the last write took place -> where we pick up
      let src_stage = vk::PipelineStageFlags::COMPUTE_SHADER;
      let dst_stage = vk::PipelineStageFlags::FRAGMENT_SHADER;
      
      // Attach the barrier to the command buffer
      unsafe { 
        device.cmd_pipeline_barrier(
          command_buffer, src_stage, dst_stage, 
          vk::DependencyFlags::default(), &[], 
          &[], &[barrier]) 
      };
    }
    else { panic!("unsupported layout transition!"); }
  }

  // Each pipeline needs to know what structures will be passed to the GPU during its lifetime. Not specific data, but
  // just the expected layout of the data once it exists
  fn create_descriptor_set_layouts(
    context: &EngineContext, gltf_textures_data: &ImageData
  ) -> (vk::DescriptorSetLayout, vk::DescriptorSetLayout)
  {
    let device = &context.device;
    let gltf_texture_views = &gltf_textures_data.views;
    // STANDARD 3D MODELS
    // Descriptor bindings are like slots in descriptor sets
    let global_bindings = [
      // Binding for the Model View Projection matrix from the Camera, used exclusively by the vertex shader
      // Model-View-Projection Buffer
      vk::DescriptorSetLayoutBinding::default().binding(0)
        .descriptor_type(vk::DescriptorType::UNIFORM_BUFFER).descriptor_count(1)
        .stage_flags(vk::ShaderStageFlags::VERTEX | vk::ShaderStageFlags::COMPUTE),
      // Index Buffer
      vk::DescriptorSetLayoutBinding::default().binding(1)
        .descriptor_type(vk::DescriptorType::STORAGE_BUFFER).descriptor_count(1)
        .stage_flags(vk::ShaderStageFlags::COMPUTE),
      // Vertex Colour Buffer
      vk::DescriptorSetLayoutBinding::default().binding(2)
        .descriptor_type(vk::DescriptorType::STORAGE_BUFFER).descriptor_count(1)
        .stage_flags(vk::ShaderStageFlags::COMPUTE | vk::ShaderStageFlags::FRAGMENT),
      // UV Buffer
      vk::DescriptorSetLayoutBinding::default().binding(3)
        .descriptor_type(vk::DescriptorType::STORAGE_BUFFER).descriptor_count(1)
        .stage_flags(vk::ShaderStageFlags::COMPUTE | vk::ShaderStageFlags::FRAGMENT),
      // Normals Buffer
      vk::DescriptorSetLayoutBinding::default().binding(4)
        .descriptor_type(vk::DescriptorType::STORAGE_BUFFER).descriptor_count(1)
        .stage_flags(vk::ShaderStageFlags::COMPUTE),
      // Storage Image read-only Sampler
      vk::DescriptorSetLayoutBinding::default().binding(5)
        .descriptor_type(vk::DescriptorType::SAMPLER).descriptor_count(1)
        .stage_flags(vk::ShaderStageFlags::FRAGMENT)
    ];

    let binding_flags = [
      vk::DescriptorBindingFlags::UPDATE_AFTER_BIND,
      vk::DescriptorBindingFlags::UPDATE_AFTER_BIND,
      vk::DescriptorBindingFlags::UPDATE_AFTER_BIND,
      vk::DescriptorBindingFlags::UPDATE_AFTER_BIND,
      vk::DescriptorBindingFlags::UPDATE_AFTER_BIND,
      vk::DescriptorBindingFlags::UPDATE_AFTER_BIND
    ];

    let mut flags_create_info = vk::DescriptorSetLayoutBindingFlagsCreateInfo::default().binding_flags(&binding_flags);

    // Copy the bindings into the layout info
    let global_layout_info = vk::DescriptorSetLayoutCreateInfo::default().push_next(&mut flags_create_info)
      .flags(vk::DescriptorSetLayoutCreateFlags::UPDATE_AFTER_BIND_POOL).bindings(&global_bindings);

    // Initialise
    let descriptor_set_layout_global = unsafe {
        device.create_descriptor_set_layout(&global_layout_info, None)
          .expect("failed to create global descriptor set layouts")
    };

    let material_bindings = [
      // Binding for a texture (colloquial), used exclusively by the fragment shader
      vk::DescriptorSetLayoutBinding::default().binding(0)
        .descriptor_type(vk::DescriptorType::SAMPLER).descriptor_count(1)
        .stage_flags(vk::ShaderStageFlags::FRAGMENT | vk::ShaderStageFlags::COMPUTE),

      vk::DescriptorSetLayoutBinding::default().binding(1)
        .descriptor_type(vk::DescriptorType::SAMPLED_IMAGE).descriptor_count(gltf_texture_views.len() as u32)
        .stage_flags(vk::ShaderStageFlags::FRAGMENT | vk::ShaderStageFlags::COMPUTE)
    ];

    let mat_binding_flags = [
      vk::DescriptorBindingFlags::UPDATE_AFTER_BIND,
      vk::DescriptorBindingFlags::PARTIALLY_BOUND | 
        vk::DescriptorBindingFlags::VARIABLE_DESCRIPTOR_COUNT | 
        vk::DescriptorBindingFlags::UPDATE_AFTER_BIND
    ];
 
    let mut mat_flags_create_info = vk::DescriptorSetLayoutBindingFlagsCreateInfo::default()
      .binding_flags(&mat_binding_flags);

    let mat_layout_info = vk::DescriptorSetLayoutCreateInfo::default().push_next(&mut mat_flags_create_info)
      .flags(vk::DescriptorSetLayoutCreateFlags::UPDATE_AFTER_BIND_POOL).bindings(&material_bindings);

    let descriptor_set_layout_material = unsafe {
        device.create_descriptor_set_layout(&mat_layout_info, None)
          .expect("failed to create material descriptor set layouts!")
    };

    return (descriptor_set_layout_global, descriptor_set_layout_material);
  }

  // Initialise a Global Slang Session, allowing Slang to SPIR-V compilation in-app at runtime
  // Shader compilation to SPIR-V creates a session from the global session
  fn init_slang() -> slang::GlobalSession
  {
    // Create a default global session that following sessions will be created from
    let global_session = slang::GlobalSession::new().expect("failed to create slang globalsession!");
    return global_session;
  }

  /*============================================= HOW DOES MEMORY WORK? ==============================================//
      Memory available to Vulkan across all hardware is referenced through heaps. Each device is different, but as a
      general rule of thumb Memory heap 0 is all the GPU VRAM available and Memory heap 1 is all the CPU RAM
      available. On a GPU with Shared Memory there may only be one memory heap that represents both. Other memory
      heaps may exist, for example Memory heap 2 may be a ~256Mb window of GPU memory available to the host CPU.
      Windows 11 only makes half of the total RAM available to Vulkan, saving the rest for other applications.
      The GPU has DEVICE_LOCAL memory, the memory on the GPU itself. If a memory heap does not have the DEVICE_LOCAL
      flag, then it is host memory available to Vulkan.
      The host may be able to directly interact with DEVICE_LOCAL memory, indicated by that memory type having both
      DEVICE_LOCAL and HOST_VISIBLE flagged. The host can then map segments of DEVICE_LOCAL|HOST_VISIBLE memory.
      The concept of Mapping Memory requires some understanding of how programs see themselves. Programs believe
      themselves to be a contiguous block of memory containing all of the data and instructions to execute properly.
      In reality, the operating system Virtualizes the memory addresses. Virtualization is the segmentation of a
      program into Pages which can exist in any physical order. Think of it like a book where the pages are out of
      order, but the page numbers let you know the order in which they logically exist. The page number is its 
      virtual address, and its physical order is its physical address.
      Mapping is like including the pages of another book in a way that maintains the logical flow of the page
      numbers. This saves you from going over to the other book each time you want to see those pages.
      So if the host has mapped the DEVICE_LOCAL|HOST_VISIBLE|HOST_COHERENT memory it is able to update that memory
      with the CPU and it will change on the GPU.
  */
  // Create and Map the Uniform Buffers that will be passed to shaders
  // We create as many of each uniform buffer as there are frames in flight, as data may change between calls while the
  // data is still being read for the previous frame
  fn create_uniform_buffers(context: &EngineContext, options: &AppOptions) -> Vec<(vk::Buffer, vk::DeviceMemory)>
  {
    let instance = &context.instance;
    let device = &context.device;
    let physical_device = context.physical_device;
    let mut mvp_buffers: Vec<(vk::Buffer, vk::DeviceMemory)> = Vec::with_capacity(options.frames_in_flight);

    // General breakdown:
    // 1. Create a buffer with sizeof(UserStruct) in DeviceMemory with accessor userStructBuffer
    // 2. Persist the created objects as class members
    // 3. Map the memory and store as a void*

    for _i in 0..options.frames_in_flight {
      let mvp_buffer_size: vk::DeviceSize = size_of::<MVP>().try_into().unwrap();
      mvp_buffers.push(Self::create_buffer(
        instance, device, physical_device, mvp_buffer_size, vk::BufferUsageFlags::UNIFORM_BUFFER, 
        vk::MemoryPropertyFlags::HOST_VISIBLE | vk::MemoryPropertyFlags::HOST_COHERENT
      ));
    }
    return mvp_buffers;
  }

  // fn create_indirect_commands(device: &Device, submeshes: Vec<SubMesh>) -> (vk::Buffer, vk::DeviceMemory)
  // {
  //   let indirect_commands: Vec<DrawIndexedIndirectCommand> = vec![]; indirect_commands.reserve(submeshes.len());

  //   for i in submeshes.len() {
  //     let cmd = vk::DrawIndexedIndirectCommand {
  //       index_count: submeshes[i].index_count,
  //       instance_count: 1,
  //       first_index: submeshes[i].index_offset,
  //       vertex_offset: submeshes[i].first_vertex as i32,
  //       first_instance: i as u32
  //     };
  //     indirect_commands.push(cmd);
  //   }

  //   return Self::create_buffer_from_vector(device, indirect_commands, 
  //     vk::BufferUsageFlags::INDIRECT_BUFFER |
  //     vk::BufferUsageFlags::STORAGE_BUFFER  | 
  //     vk::BufferUsageFlags::TRANSFER_DST
  //   );
  // }

  // Create DescriptorPools that can allocate DescriptorSets. It's like a check making sure not too many descriptors of
  // some type are allocated, as it does not take layouts into account
  fn create_descriptor_pools(context: &EngineContext, options: &AppOptions) -> vk::DescriptorPool
  {
    let device = &context.device;
    // It's possible that a driver allows overallocation from pool, avoiding VK_ERROR_OUT_OF_POOL_MEMORY when allocating
    // for more descriptor sets than the descriptor pool sizes "allow". In these cases it may not seem like the
    // descriptorCount member has any effect, but it will for some other drivers.
    // STANDARD 3D MODELS
    // We need at least 1 Uniform Buffer and 1 CombinedImageSampler per material group per frame in flight
    let descriptor_count: u32 = options.frames_in_flight.try_into().unwrap();
    let pool_sizes = [
      // Model View Projection matrices
      vk::DescriptorPoolSize::default().ty(vk::DescriptorType::UNIFORM_BUFFER).descriptor_count(descriptor_count),
      // Cube transform matrices
      vk::DescriptorPoolSize::default().ty(vk::DescriptorType::UNIFORM_BUFFER).descriptor_count(descriptor_count),
      // Index Buffer
      vk::DescriptorPoolSize::default().ty(vk::DescriptorType::STORAGE_BUFFER).descriptor_count(descriptor_count),
      // Vertex Colour Buffer
      vk::DescriptorPoolSize::default().ty(vk::DescriptorType::STORAGE_BUFFER).descriptor_count(descriptor_count),
      // UV Buffer
      vk::DescriptorPoolSize::default().ty(vk::DescriptorType::STORAGE_BUFFER).descriptor_count(descriptor_count),
      // Normal Buffer
      vk::DescriptorPoolSize::default().ty(vk::DescriptorType::STORAGE_BUFFER).descriptor_count(descriptor_count),
      // Compute Output Image Sampler
      vk::DescriptorPoolSize::default().ty(vk::DescriptorType::SAMPLER).descriptor_count(descriptor_count),
      // Material Texture Sampler
      vk::DescriptorPoolSize::default().ty(vk::DescriptorType::SAMPLER).descriptor_count(descriptor_count),
      // Textures
      vk::DescriptorPoolSize::default().ty(vk::DescriptorType::SAMPLED_IMAGE)
        .descriptor_count(1)
    ];

    let pool_info = vk::DescriptorPoolCreateInfo::default()
    // DescriptorSets can be individually freed
      .flags(vk::DescriptorPoolCreateFlags::FREE_DESCRIPTOR_SET | vk::DescriptorPoolCreateFlags::UPDATE_AFTER_BIND)
      // We need at least one descriptor set per material group per frame in flight
      .max_sets(descriptor_count + 1)
      // Attach the DescriptorPoolSizes
      .pool_sizes(&pool_sizes);

    // Initialise pool
    let descriptor_pool = unsafe {
      device.create_descriptor_pool(&pool_info, None).expect("failed to create graphics descriptor pool!")
    };

    return descriptor_pool;
  }

  // Collation of Descriptors for shaders
  fn create_descriptor_sets(
    context: &EngineContext, options: &AppOptions, fallback_texture_data: &ImageData,
    descriptor_set_layout_global: vk::DescriptorSetLayout, descriptor_set_layout_material: vk::DescriptorSetLayout,
    descriptor_pool: vk::DescriptorPool, mvp_buffers: &Vec<(vk::Buffer, vk::DeviceMemory)>,
    vertices: &Vec<Vertex>, indices: &Vec<u32>, vertex_data: &VertexData
  ) -> (Vec<vk::DescriptorSet>, Vec<vk::DescriptorSet>)
  {
    let device = &context.device;
    let fallback_texture_view = fallback_texture_data.views[0];
    let fallback_texture_sampler = fallback_texture_data.sampler.unwrap();
    let index_buffer = vertex_data.index_buffer.0;
    let colour_buffer = vertex_data.colour_buffer.0;
    let uv_buffer = vertex_data.uv_buffer.0;
    let normal_buffer = vertex_data.nrm_buffer.0;

    // STANDARD 3D MODELS
    // Shader Resources
    // [value; num_of_copies]
    
    // MAX_FRAMES_IN_FLIGHT copies of DescriptorSetLayout
    let global_layouts = vec![descriptor_set_layout_global; options.frames_in_flight];

    // Collate the relevant info (DescriptorPool and DescriptorSetLayouts)
    let mut global_alloc_info = vk::DescriptorSetAllocateInfo::default()
      .descriptor_pool(descriptor_pool).set_layouts(&global_layouts);

    let global_descriptor_sets = unsafe {
      device.allocate_descriptor_sets(&global_alloc_info)
        .expect("failed to allocate descriptor sets for descriptor_set_layout_global!")
    };

    for i in 0..options.frames_in_flight {
      let (mvp_buffer_info, index_buffer_info, colour_buffer_info, uv_buffer_info, norms_buffer_info) = {
        (
          [vk::DescriptorBufferInfo::default().buffer(mvp_buffers[i].0)
            .offset(0).range(size_of::<MVP>().try_into().unwrap())],
          [vk::DescriptorBufferInfo::default().buffer(index_buffer)
            .offset(0).range(size_of::<u32>() as vk::DeviceSize * indices.len() as vk::DeviceSize)],
          [vk::DescriptorBufferInfo::default().buffer(colour_buffer)
            .offset(0).range(size_of::<glm::Vec4>() as vk::DeviceSize * vertices.len() as vk::DeviceSize)],
          [vk::DescriptorBufferInfo::default().buffer(uv_buffer)
            .offset(0).range(size_of::<glm::Vec2>() as vk::DeviceSize * vertices.len() as vk::DeviceSize)],
          [vk::DescriptorBufferInfo::default().buffer(normal_buffer)
            .offset(0).range(size_of::<glm::Vec4>() as vk::DeviceSize * vertices.len() as vk::DeviceSize)]
        )
      };
          
      let (mvp_write, indices_write, colours_write, uvs_write, norms_write) = {
        (
          vk::WriteDescriptorSet::default().dst_set(global_descriptor_sets[i]).dst_binding(0).dst_array_element(0)
            .descriptor_count(1).descriptor_type(vk::DescriptorType::UNIFORM_BUFFER).buffer_info(&mvp_buffer_info),
          vk::WriteDescriptorSet::default().dst_set(global_descriptor_sets[i]).dst_binding(1).dst_array_element(0)
            .descriptor_count(1).descriptor_type(vk::DescriptorType::STORAGE_BUFFER).buffer_info(&index_buffer_info),
          vk::WriteDescriptorSet::default().dst_set(global_descriptor_sets[i]).dst_binding(2).dst_array_element(0)
            .descriptor_count(1).descriptor_type(vk::DescriptorType::STORAGE_BUFFER).buffer_info(&colour_buffer_info),
          vk::WriteDescriptorSet::default().dst_set(global_descriptor_sets[i]).dst_binding(3).dst_array_element(0)
            .descriptor_count(1).descriptor_type(vk::DescriptorType::STORAGE_BUFFER).buffer_info(&uv_buffer_info),
          vk::WriteDescriptorSet::default().dst_set(global_descriptor_sets[i]).dst_binding(4).dst_array_element(0)
            .descriptor_count(1).descriptor_type(vk::DescriptorType::STORAGE_BUFFER).buffer_info(&norms_buffer_info)
        )
      };

      let descriptor_writes = [
        mvp_write,
        colours_write,
        indices_write, 
        uvs_write,
        norms_write
      ];

      // Write the descriptor sets to the GPU
      unsafe { device.update_descriptor_sets(&descriptor_writes, &[]) };
    }
    // Mapped Textures
    let mat_layouts = [descriptor_set_layout_material];

    let mat_variable_counts = [1];
    let mut mat_variable_count_info = vk::DescriptorSetVariableDescriptorCountAllocateInfo::default()
      .descriptor_counts(&mat_variable_counts);

    let mat_alloc_info = vk::DescriptorSetAllocateInfo::default().push_next(&mut mat_variable_count_info)
      .descriptor_pool(descriptor_pool).set_layouts(&mat_layouts);

    let material_descriptor_sets = unsafe { 
      device.allocate_descriptor_sets(&mat_alloc_info).expect("failed to create material descriptor sets!") };

    let mat_sampler_info = [vk::DescriptorImageInfo::default().sampler(fallback_texture_sampler)];

    let mat_sampler_write = vk::WriteDescriptorSet::default().image_info(&mat_sampler_info)
      .dst_set(material_descriptor_sets[0]).dst_binding(0).dst_array_element(0)
      .descriptor_count(1).descriptor_type(vk::DescriptorType::SAMPLER);

    unsafe { device.update_descriptor_sets(&[mat_sampler_write], &[]) };

    let mut mat_image_infos: Vec<vk::DescriptorImageInfo> = vec![
      vk::DescriptorImageInfo::default()
        .image_view(fallback_texture_view).image_layout(vk::ImageLayout::SHADER_READ_ONLY_OPTIMAL)
    ];
    
    let material_write = vk::WriteDescriptorSet::default().image_info(&mat_image_infos)
      .dst_set(material_descriptor_sets[0]).dst_binding(1).dst_array_element(0)
      .descriptor_count(mat_image_infos.len().try_into().unwrap())
      .descriptor_type(vk::DescriptorType::SAMPLED_IMAGE);

    unsafe { device.update_descriptor_sets(&[material_write], &[]) };

    return (global_descriptor_sets, material_descriptor_sets);
  }


  fn setup_imgui_frame(
    debug_gui_context: &mut DebugGuiContext, camera: &mut Camera, window: &Window
  )
  {
    let imgui = &mut debug_gui_context.imgui;
    let platform = &mut debug_gui_context.platform;

    let _frame_io = platform.prepare_frame(imgui.io_mut(), window).unwrap();
    let ui = imgui.new_frame();

    if let Some(_) = ui
      .window("Camera Controls")
      .title_bar(true)
      .resizable(true)
      .always_auto_resize(true)
      .movable(true)
      .collapsible(true)
      .position([20.0, 20.0], Condition::FirstUseEver)
      .begin()
      {
        ui.text_wrapped(format!("{:.2}ms", (debug_gui_context.delta as f64) / 1000.0));
        ui.slider("Move Speed", 0.01, 10.0, &mut camera.move_speed);
        let upper = 30.0; let lower = -upper;
        ui.slider("X", lower, upper, &mut camera.position.x);
        ui.slider("Y", lower, upper, &mut camera.position.y);
        ui.slider("Z", lower, upper, &mut camera.position.z);

        ui.spacing();

        ui.slider("Pitch Speed", 0.01, 10.0, &mut camera.pitch_speed);
        ui.slider("Pitch", -glm::half_pi::<f32>(), glm::half_pi::<f32>(), &mut camera.pitch);
        ui.slider("Yaw Speed", 0.01, 10.0, &mut camera.yaw_speed);
        ui.slider("Yaw", -glm::pi::<f32>(), glm::pi::<f32>(), &mut camera.yaw);

        ui.spacing();

        ui.slider("FOV", 20.0, 170.0, &mut camera.fov);
        ui.slider("FOV Speed", 0.01, 1000.0, &mut camera.fov_speed);

        ui.spacing();

        ui.slider("Speed Mod", 0.01, 4.0, &mut camera.shift_speed);
        // ImGui::SliderInt("Delta Mult", &deltaExp, 0, 32);
      };

    if let Some(_) = ui
      .window("Shaders")
      .title_bar(true)
      .resizable(true)
      .always_auto_resize(true)
      .movable(true)
      .collapsible(true)
      .position([1110.0, 20.0], Condition::FirstUseEver)
      .begin()
      {
        ui.input_text("Slang Path", &mut debug_gui_context.slang_path).build();
        ui.input_text("SPIR-V Path", &mut debug_gui_context.spirv_path).build();      
      };

    platform.prepare_render(ui, window);
  }

  // High-level Vulkan frame logic
  pub fn draw_frame(&mut self, window: &Window)
  {
    let context = self.context.as_ref().unwrap();
    let device = &context.device;
    let queue = context.queue;
    let swapchain = &context.swapchain;
    let swapchain_khr = context.swapchain_khr;
    
    let fence = self.in_flight_fences[self.current_frame];
    let global_descriptor_set = self.global_descriptor_sets[self.current_frame];
    let material_descriptor_set = self.material_descriptor_sets[0];
    let depth_image = self.depth_image_data.images[0].0;
    let depth_view = self.depth_image_data.views[0];

    let debug_gui_context = self.debug_gui_context.as_mut().unwrap();
    let camera = &mut self.camera;
    Self::setup_imgui_frame(debug_gui_context, camera, window);
    
    let imgui = &mut debug_gui_context.imgui;
    let draw_data = imgui.render();

    // Try to acquire the next swap chain image.
    let raw_result = unsafe { 
      swapchain.acquire_next_image(
        swapchain_khr, 
        u64::MAX, // Timeout
        vk::Semaphore::null(), // Semaphore to signal
        fence // Fence to signal
      )
    };
    let (image_index, _) = match raw_result {
      Ok(value) => { Some(value) }
      Err(err) => {
        // Check if the swap chain needs to be recreated or if some other error occured
        if err == vk::Result::ERROR_OUT_OF_DATE_KHR || self.framebuffer_resized {
          self.framebuffer_resized = false; self.recreate_swapchain(window); return;
        }
        else if err != vk::Result::SUCCESS && err != vk::Result::SUBOPTIMAL_KHR {
          panic!("failed to acquire swap chain image!");
        }
        None
      }
    }.unwrap();

    let swapchain_extent = self.swapchain_extent;
    let image = self.swapchain_image_data.images[image_index as usize].0;
    let view = self.swapchain_image_data.views[image_index as usize];
    
    // Wait until next image is acquired
    while match unsafe {
      device.wait_for_fences(&[fence], true, u64::MAX)
    } { Ok(_val) => {vk::Result::SUCCESS} Err(err) => {err}} == vk::Result::TIMEOUT { }

    // unsignal this fence, ready to be signalled again
    unsafe { device.reset_fences(&[fence]).expect("failed to reset fence!") };

    // Update uniform buffers
    let (camera_moved, new_view) = Self::update_model_view_projection(
      context, &self.camera, self.old_view, self.mvp_buffers[self.current_frame].1);
    if camera_moved { self.frame = 0; self.old_view = new_view; }
    
    // A command buffer needs to be in the initial state to record. Resetting the command pool resets all of the buffers
    // allocated in that pool. Command buffers can also be reset individually (begin() has an implicit reset if the 
    // buffer is not in the initial state).

    let semaphore = [self.timeline_semaphore];

    //// Update timeline value for this frame
    let compute_wait_value = [self.timeline_value]; self.timeline_value += 1;
    let compute_signal_value = [self.timeline_value]; let graphics_wait_value = compute_signal_value; 
    self.timeline_value += 1; let graphics_signal_value = [self.timeline_value];
    
    // COMPUTE
    {
      let compute_command_buffer = self.compute_command_buffers[self.current_frame];
      let compute_pipeline = self.compute_pipeline;

      Self::record_compute_command_buffers(
        context, compute_command_buffer, compute_pipeline, global_descriptor_set, material_descriptor_set
      );
      let command_buffers = [compute_command_buffer];

      // Submission will wait for computeWaitValue
      // Submission will signal to computeSignalValue upon completion of queue.submit()
      let mut compute_timeline_info = vk::TimelineSemaphoreSubmitInfo::default()
        .wait_semaphore_values(&compute_wait_value).signal_semaphore_values(&compute_signal_value);

      // Pipeline stage to wait at
      let wait_stages = [vk::PipelineStageFlags::COMPUTE_SHADER];

      let compute_submit_info = vk::SubmitInfo::default()
        .push_next(&mut compute_timeline_info)
        .wait_semaphores(&semaphore).wait_dst_stage_mask(&wait_stages)
        .signal_semaphores(&semaphore)
        .command_buffers(&command_buffers);

      // submit the recorded command buffer to the GPU so it can start working
      unsafe { device.queue_submit(queue, &[compute_submit_info], vk::Fence::null()).expect("failed to submit to compute commands to queue!") };
    }
    
    // GRAPHICS
    {
      let draw_command_buffer = self.draw_command_buffers[self.current_frame];
      let pipeline = self.graphics_pipeline;
      let renderer = &mut debug_gui_context.renderer;
      Self::record_command_buffers(
        context, renderer, draw_data, draw_command_buffer, pipeline, image, view, swapchain_extent, 
        depth_image, depth_view, global_descriptor_set, material_descriptor_set,
        &self.vertex_data, &self.submeshes
      );

      let command_buffers = [draw_command_buffer];

      // Submission will wait for graphicsWaitValue
      // Submission will signal to graphicsSignalValue upon completion of queue.submit()
      let mut graphics_timeline_info = vk::TimelineSemaphoreSubmitInfo::default()
        .wait_semaphore_values(&graphics_wait_value).signal_semaphore_values(&graphics_signal_value);

      // Pipeline stage to wait at
      let wait_stage = [vk::PipelineStageFlags::COLOR_ATTACHMENT_OUTPUT];
      
      let graphics_submit_info = vk::SubmitInfo::default()
        .push_next(&mut graphics_timeline_info)
        .wait_semaphores(&semaphore).wait_dst_stage_mask(&wait_stage)
        .signal_semaphores(&semaphore)
        .command_buffers(&command_buffers);

      // submit the recorded command buffer to the GPU so it can start working
      // the Vertex stage will be completed in parallel with the previously submitted compute work
      unsafe { device.queue_submit(
        queue, &[graphics_submit_info], vk::Fence::null()).expect("failed to submit draw commands to queue!") };
    }

    // explicitly wait on the timeline semaphore, PresentInfoKHR only accepts binary semaphores
    let wait_info = vk::SemaphoreWaitInfo::default()
      .semaphores(&semaphore).values(&graphics_signal_value);

    while match unsafe { device.wait_semaphores(&wait_info, u64::MAX)} 
      { Ok(_val) => {vk::Result::SUCCESS}, Err(err) => {err}} == vk::Result::TIMEOUT { }

    // PresentInfo without wait semaphores
    let present_info = vk::PresentInfoKHR {
      swapchain_count: 1, p_swapchains: &swapchain_khr,
      p_image_indices: &image_index,
      ..Default::default()
    };

    // Present the swap chain image to the surface
    let present_result = match unsafe {
        swapchain.queue_present(queue, &present_info)} { Ok(_val) => {vk::Result::SUCCESS}, Err(err) => {err}};
    
    // Double check that the framebuffer hasn't been resized during the frame
    if present_result == vk::Result::ERROR_OUT_OF_DATE_KHR || present_result == vk::Result::SUBOPTIMAL_KHR || self.framebuffer_resized {
      self.framebuffer_resized = false; self.recreate_swapchain(window);
    }
    else if present_result != vk::Result::SUCCESS {
      panic!("failed to present swap chain image!");
    }

    // move on to the next frame
    self.current_frame = (self.current_frame + 1) % self.options.frames_in_flight;
    self.frame = self.frame % u32::MAX + 1;
  }

  // When the surface's resolution is updated/the swap chain has gone bad
  pub fn recreate_swapchain(&mut self, window: &Window)
  {
    let context = self.context.as_ref().unwrap();
    let device = &context.device;
    let physical_device = context.physical_device;
    let surface = &context.surface;
    let surface_khr = context.surface_khr;
    let swapchain = &context.swapchain;
    // Don't mess with the resources until you're sure they're not being used
    unsafe { device.device_wait_idle().expect("failed to wait for device!") };

    // Reset swap chain class members
    self.cleanup_swapchain();

    let capabilities = unsafe { surface.get_physical_device_surface_capabilities(physical_device, surface_khr)}.unwrap();
    let swapchain_extent = Self::choose_swap_extent(capabilities, window.inner_size());
    // Re-initialise swap chain class members i.e. colour attachment
    let (swapchain_khr, swapchain_images) = Self::create_swapchain(
      swapchain, surface_khr, capabilities, self.surface_format, swapchain_extent, self.swapchain_present_mode);
    let swapchain_image_views = Self::create_swapchain_image_views(device, &swapchain_images, self.surface_format.format);

    // Recreate the depth attachment
    let (depth_image, depth_image_view) = Self::create_depth_resources(context, swapchain_extent);

    // Update the camera to reflect the new resolution
    self.camera.viewport_width = swapchain_extent.width as f32;
    self.camera.viewport_height = swapchain_extent.height as f32;

    let context = self.context.as_mut().unwrap();

    context.swapchain_khr = swapchain_khr;
    self.swapchain_extent = swapchain_extent;
    self.swapchain_image_data = ImageData { 
      images: swapchain_images.iter().map(|&img| (img, vk::DeviceMemory::null())).collect(), 
      views: swapchain_image_views, sampler: None 
    };
    self.depth_image_data = ImageData { images: vec![depth_image], views: vec![depth_image_view], sampler: None };

    self.reload_shaders();
  }

  fn cleanup_swapchain(&self)
  {
    // End the lifetime of the swap chain class members
    let context = self.context.as_ref().unwrap();
    let device = &context.device; let swapchain = &context.swapchain; let swapchain_khr = context.swapchain_khr;
    unsafe {
      self.depth_image_data.views.iter().for_each(|&view| device.destroy_image_view(view, None));
      self.depth_image_data.images.iter().for_each(|&img| 
        { device.destroy_image(img.0, None); device.free_memory(img.1, None); } 
      );
      self.swapchain_image_data.views.iter().for_each(|&view| device.destroy_image_view(view, None));
      swapchain.destroy_swapchain(swapchain_khr, None);
    }
  }

  // We compile from SPIR-V to a GPU kernel
  // nodiscard means the compiler makes sure that you handle the returned value
  fn create_shader_module(device: &Device, code: &[u8]) -> vk::ShaderModule
  {
    // take in SPIR-V
    let create_info = vk::ShaderModuleCreateInfo {
      code_size: code.len() * size_of::<u8>(),
      p_code: code.as_ptr() as *const _ as *const u32,
      ..Default::default()
    };
    // compile to GPU-compatible code
    return unsafe { device.create_shader_module(&create_info, None).expect("failed to create shader module!") };
  }

  // Pipelines are almost identical, refer to CreateGraphicsPipeline for a complete rundown
  // Define exactly what data and how to process it on the GPU for a specific shader
  fn create_graphics_pipeline(
    context: &EngineContext, spirv_path: &Path,
    descriptor_set_layout_global: vk::DescriptorSetLayout, descriptor_set_layout_material: vk::DescriptorSetLayout,
    swapchain_surface_format: vk::Format
  ) -> (vk::PipelineLayout, vk::Pipeline)
  {
    let instance = &context.instance;
    let device = &context.device;
    let physical_device = context.physical_device;
    // The GPU-ready compiled shader code
    let shader_module = Self::create_shader_module(device, fs::read(spirv_path).expect("failed to read file!").as_slice());

    let vert_name = CString::new("vertMain").unwrap();
    let frag_name = CString::new("fragMain").unwrap();
    // All the stages come from the same SPIR-V file, so same shaderModule
    // Need to identify the entrypoint names: "vertMain", "fragMain"
    let vert_shader_stage_create_info = vk::PipelineShaderStageCreateInfo::default()
      .stage(vk::ShaderStageFlags::VERTEX)
      .module(shader_module)
      .name(&vert_name);

    let frag_shader_stage_create_info = vk::PipelineShaderStageCreateInfo::default()
      .stage(vk::ShaderStageFlags::FRAGMENT)
      .module(shader_module)
      .name(&frag_name);
    let shader_stages = [vert_shader_stage_create_info, frag_shader_stage_create_info];

    // Get the user-defined format of vertices. Ours is each entry is a vertex, not an instance
    let binding_description = [Vertex::get_binding_description()];
    // Vertices are comprised of float3 pos, float3 colour, float2 texCoord
    let attributes_descriptions = Vertex::get_attribute_descriptions();

    // Combine all that vertex info
    let vertex_input_info = vk::PipelineVertexInputStateCreateInfo::default()
      .vertex_binding_descriptions(&binding_description)
      .vertex_attribute_descriptions(&attributes_descriptions);

    // We access vertices in groups of 3 indices
    // Example: { 0, 1, 2, 2, 3, 0 }, { 0, 1, 2 } is one triangle, { 2, 3, 0 } is another
    // whereas TriangleStrip would be { 0, 1, 2, 3 }, { 0, 1, 2 } is one triangle, { 1, 2, 3 } is another
    let input_assembly_info = vk::PipelineInputAssemblyStateCreateInfo::default()
      .topology(vk::PrimitiveTopology::TRIANGLE_LIST);

    // We need to be able to read from and write to the depthStencil
    let depth_stencil = vk::PipelineDepthStencilStateCreateInfo::default()
      .depth_test_enable(true).depth_write_enable(true)
      .depth_compare_op(vk::CompareOp::LESS).depth_bounds_test_enable(false)
      .stencil_test_enable(false);

    let descriptor_set_layouts = [descriptor_set_layout_global, descriptor_set_layout_material];

    let dynamic_states = [vk::DynamicState::VIEWPORT, vk::DynamicState::SCISSOR];
    let dynamic_info = vk::PipelineDynamicStateCreateInfo::default().dynamic_states(&dynamic_states);
    
    let viewport_info = vk::PipelineViewportStateCreateInfo::default()
      .viewport_count(1).scissor_count(1);

    let rasterizer_info = vk::PipelineRasterizationStateCreateInfo::default()
      .depth_clamp_enable(false).rasterizer_discard_enable(false)
      .polygon_mode(vk::PolygonMode::FILL).cull_mode(vk::CullModeFlags::BACK)
      .front_face(vk::FrontFace::COUNTER_CLOCKWISE)
      .depth_bias_enable(false).depth_bias_constant_factor(0.0)
      .depth_bias_clamp(0.0).depth_bias_slope_factor(1.0)
      .line_width(1.0);

    let multisampling_info = vk::PipelineMultisampleStateCreateInfo::default()
      .rasterization_samples(vk::SampleCountFlags::TYPE_1).sample_shading_enable(false);

    let color_blend_attachment = [vk::PipelineColorBlendAttachmentState::default()
      .blend_enable(true)
      .src_color_blend_factor(vk::BlendFactor::SRC_ALPHA).src_alpha_blend_factor(vk::BlendFactor::ONE_MINUS_SRC_ALPHA)
      .dst_alpha_blend_factor(vk::BlendFactor::ONE_MINUS_SRC_ALPHA).dst_alpha_blend_factor(vk::BlendFactor::ZERO)
      .color_blend_op(vk::BlendOp::ADD).color_write_mask(vk::ColorComponentFlags::RGBA)];

    let color_blend_info = vk::PipelineColorBlendStateCreateInfo::default()
      .logic_op_enable(false).logic_op(vk::LogicOp::COPY)
      .attachments(&color_blend_attachment);

    let push_constant_range = [vk::PushConstantRange {
      stage_flags: vk::ShaderStageFlags::FRAGMENT, offset: 0, 
      size: size_of::<RasterPushConstant>().try_into().unwrap()
    }];

    // Which DescriptorSetLayouts will be used by this pipeline
    let pipeline_layout_info = vk::PipelineLayoutCreateInfo::default()
      .set_layouts(&descriptor_set_layouts).push_constant_ranges(&push_constant_range);

    let graphics_pipeline_layout = unsafe {
        device.create_pipeline_layout(&pipeline_layout_info, None).expect("failed to create graphics pipeline layout!")
    };

    let colour_attachment_format = [swapchain_surface_format];
    let depth_attachment_format = Self::find_depth_format(instance, physical_device);
    // Which attachments are involved
    let mut rendering_info = vk::PipelineRenderingCreateInfo::default()
      .color_attachment_formats(&colour_attachment_format)
      .depth_attachment_format(depth_attachment_format);

    // Collate all the info
    let pipeline_info = vk::GraphicsPipelineCreateInfo::default()
      .push_next(&mut rendering_info)
      .stages(&shader_stages)
      .vertex_input_state(&vertex_input_info)
      .input_assembly_state(&input_assembly_info)
      .viewport_state(&viewport_info)
      .rasterization_state(&rasterizer_info)
      .depth_stencil_state(&depth_stencil)
      .multisample_state(&multisampling_info)
      .color_blend_state(&color_blend_info)
      .dynamic_state(&dynamic_info)
      .layout(graphics_pipeline_layout);

    let graphics_pipeline = unsafe {
      device.create_graphics_pipelines(vk::PipelineCache::null(), &[pipeline_info], None)
        .expect("failed to create graphics pipelines!")
    }[0];
    return (graphics_pipeline_layout, graphics_pipeline);
  }

  fn create_compute_pipeline(
    context: &EngineContext, spirv_path: &Path,
    descriptor_set_layout_global: vk::DescriptorSetLayout,
    descriptor_set_layout_material: vk::DescriptorSetLayout
  ) -> (vk::PipelineLayout, vk::Pipeline)
  {
    let device = &context.device;
    // The GPU-ready compiled shader code
    let shader_module = Self::create_shader_module(device, fs::read(spirv_path).expect("failed to read file!").as_slice());

    let comp_name = CString::new("compMain").unwrap();
    // All the stages come from the same SPIR-V file, so same shaderModule
    // Need to identify the entrypoint names: "vertMain", "fragMain"
    let comp_shader_stage_create_info = vk::PipelineShaderStageCreateInfo::default()
      .stage(vk::ShaderStageFlags::COMPUTE)
      .module(shader_module)
      .name(&comp_name);

    let descriptor_set_layouts = [descriptor_set_layout_global, descriptor_set_layout_material];

    let push_constant_range = [vk::PushConstantRange {
      stage_flags: vk::ShaderStageFlags::COMPUTE, offset: 0,
      size: size_of::<RasterPushConstant>().try_into().unwrap()
    }];

    // Which DescriptorSetLayouts will be used by this pipeline
    let pipeline_layout_info = vk::PipelineLayoutCreateInfo::default()
      .set_layouts(&descriptor_set_layouts)
      .push_constant_ranges(&push_constant_range);

    let compute_pipeline_layout = unsafe {
        device.create_pipeline_layout(&pipeline_layout_info, None).expect("failed to create graphics pipeline layout!")
    };

    // Collate all the info
    let pipeline_info = vk::ComputePipelineCreateInfo::default()
      .stage(comp_shader_stage_create_info)
      .layout(compute_pipeline_layout);

    let compute_pipeline = unsafe {
      device.create_compute_pipelines(vk::PipelineCache::null(), &[pipeline_info], None)
        .expect("failed to create graphics pipelines!")
    }[0];
    return (compute_pipeline_layout, compute_pipeline);
  }

  fn create_buffer_from_vector<T: std::marker::Copy>(
    context: &EngineContext, vec: &Vec<T>, dst_usage_flags: vk::BufferUsageFlags
  ) -> (vk::Buffer, vk::DeviceMemory)
  {
    let instance = &context.instance;
    let device = &context.device;
    let physical_device = context.physical_device;
    let command_pool = context.command_pool;
    let queue = context.queue;
    // Our user-defined Vertex struct is of uniform size for all instantiations
    let buffer_size: vk::DeviceSize = (size_of::<T>() * vec.len()).try_into().unwrap();
    let src_usage_flags = vk::BufferUsageFlags::TRANSFER_SRC;
    let memory_flags = vk::MemoryPropertyFlags::HOST_VISIBLE | vk::MemoryPropertyFlags::HOST_COHERENT;
    // We create a CPU-editable buffer, insert the data, then copy that buffer into one that does not require CPU access
    // Notice the buffer usage flag TransferSrc. This lets the GPU know we will be copying this buffer at some point
    let (staging_buffer, staging_buffer_memory) = Self::create_buffer(instance, device, physical_device, buffer_size, src_usage_flags, memory_flags);

    // Map and copy our loaded vertices data to the GPU host-visible memory
    unsafe {
      let staging_data =  
        device.map_memory(staging_buffer_memory, 0, buffer_size, vk::MemoryMapFlags::empty())
        .expect("failed to map device memory!");
      let mut align = ash::util::Align::new(staging_data, size_of::<T>() as _, buffer_size);
      align.copy_from_slice(vec.as_slice());
      // We don't need to access this buffer anymore, unmap
      device.unmap_memory(staging_buffer_memory);
    };

    // Create a Vertex Buffer in DEVICE_LOCAL memory not necessarily visible to host
    // Notice the buffer usage flag TransferDst. We will be copying to this buffer.
    let copy_buffer = Self::create_buffer(
      instance, device, physical_device, buffer_size, dst_usage_flags, vk::MemoryPropertyFlags::DEVICE_LOCAL);

    // Copy the host-visible buffer to the non-host-visible buffer
    Self::copy_buffer(device, command_pool, queue, staging_buffer, copy_buffer.0, buffer_size);

    return copy_buffer;
  }

  // We just need the vertices stored on the GPU. We will instruct the GPU on how to interpret the data later.
  fn create_vertex_buffer(context: &EngineContext, verts: &Vec<Vertex>) -> (vk::Buffer, vk::DeviceMemory)
  {
    let usage_flags = 
      vk::BufferUsageFlags::VERTEX_BUFFER | 
      vk::BufferUsageFlags::TRANSFER_DST | 
      vk::BufferUsageFlags::SHADER_DEVICE_ADDRESS |
      vk::BufferUsageFlags::ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_KHR;

    return Self::create_buffer_from_vector(context, verts, usage_flags);
  }

  fn create_index_buffer(context: &EngineContext, indices: &Vec<u32>, add_flags: vk::BufferUsageFlags) -> (vk::Buffer, vk::DeviceMemory)
  {
    let usage_flags = 
      vk::BufferUsageFlags::INDEX_BUFFER | 
      vk::BufferUsageFlags::TRANSFER_DST | 
      add_flags;
    return Self::create_buffer_from_vector(context, indices, usage_flags);
  }

  fn create_colour_buffer(context: &EngineContext, verts: &Vec<Vertex>) -> (vk::Buffer, vk::DeviceMemory)
  {
    let colours: Vec<glm::Vec4> = verts.iter().map(|v| glm::make_vec4(&[v.colour.x, v.colour.y, v.colour.z, 1.0])).collect();

    let usage_flags = vk::BufferUsageFlags::STORAGE_BUFFER | vk::BufferUsageFlags::TRANSFER_DST;
    return Self::create_buffer_from_vector(context, &colours, usage_flags);
  }

  fn create_uv_buffer(context: &EngineContext, verts: &Vec<Vertex>) -> (vk::Buffer, vk::DeviceMemory)
  {
    let uvs: Vec<glm::Vec2> = verts.iter().map(|v| v.tex_coord).collect();
    
    let usage_flags = vk::BufferUsageFlags::STORAGE_BUFFER | vk::BufferUsageFlags::TRANSFER_DST;
    return Self::create_buffer_from_vector(context, &uvs, usage_flags);
  }

  fn create_normal_buffer(context: &EngineContext, verts: &Vec<Vertex>) -> (vk::Buffer, vk::DeviceMemory)
  {
    let norms: Vec<glm::Vec4> = verts.iter().map(|v| glm::make_vec4(&[v.norm.x, v.norm.y, v.norm.z, 1.0])).collect();

    let usage_flags = vk::BufferUsageFlags::STORAGE_BUFFER | vk::BufferUsageFlags::TRANSFER_DST;
    return Self::create_buffer_from_vector(context, &norms, usage_flags);
  }

  // Use the Slang Compilation API to compile slang shaders to SPIR-V during and by the application
  pub fn compile_shader(context: &EngineContext, src: &Path, dst: &Path)
  {
    let global_session = &context.global_session;
    // Early exit if source file does not exist
    let fsrc = File::open(src);
    if fsrc.is_err() {
      println!("failed to open {}!", src.display());
      return;
    }

    // We need to establish what this compilation session can and will do
    // Targeting SPIR-V v1.4 and writing it straight to a file
    let target_desc = slang::TargetDesc::default()
    .format(slang::CompileTarget::Spirv)
    .profile(global_session.find_profile("spirv_1_4"));
  
    // target_desc.flags = SlangTargetFlagGenerateSpirvDirectly;

    let targets = [target_desc];

    // Some options that ensure proper output
    let compiler_option_entries = slang::CompilerOptions::default()
      .vulkan_use_entry_point_name(true)
      .matrix_layout_column(true)
      .emit_spirv_directly(true)
      .capability(global_session.find_capability("vk_mem_model"));
 
    // Slang likes to look for the files by itself, even if you pass in an absolute path, so direct it to look in the
    // parent directory of src
    let search_path = CString::new(src.parent().unwrap().to_str().unwrap())
      .expect("failed to convert source file path to CString!");
    let search_paths = [search_path.as_ptr()];

    let session_desc = slang::SessionDesc::default()
      .options(&compiler_option_entries)
      .search_paths(&search_paths)
      .targets(&targets);

    // Create this session from the global session
    // Notice writeRef(). ComPtr is not directly interfaceable, but comes with helper functions like writeRef for passing
    // by reference
    let session = global_session.create_session(&session_desc).expect("failed to create slang session!");

    // Slang does not expect the entirety of a shader to be in one file. It treats each file like a module, links all 
    // the modules together and then outputs the SPIR-V.
    let module = session.load_module(src.to_str().unwrap()).expect("failed to load slang module!");
    let vertex_entry_point = module.find_entry_point_by_name("vertMain").expect("failed to load vertex entry point!");
    let fragment_entry_point = module.find_entry_point_by_name("fragMain").expect("failed to load frag entry point!");
    let compute_entry_point = module.find_entry_point_by_name("compMain").expect("failed to load compute entry point!");

    // Compose/Assemble a program from the module and entrypoints
    let components = [
      module.downcast().clone(), vertex_entry_point.downcast().clone(), 
      fragment_entry_point.downcast().clone(), compute_entry_point.downcast().clone()
    ];
    let program = session.create_composite_component_type(&components).expect("failed to create composite component type!");

    // Grab everything from the shader-imported modules
    let linked_program = program.link().expect("failed to link slang program!");

    // Convert the linked program into target-compatible bytecode
    let shader_byte_code = linked_program.target_code(0).expect("failed to get target code from linked program");
    write_bytes_to_file(dst, shader_byte_code.as_slice());
  }

  fn cleanup_pipelines(context: &EngineContext, pipelines: &Vec<(vk::PipelineLayout, vk::Pipeline)>)
  {
    let device = &context.device;

    unsafe {
      pipelines.iter().for_each(|&pipeline| {
        device.destroy_pipeline(pipeline.1, None);
        device.destroy_pipeline_layout(pipeline.0, None);
      });
    }
  }

  // Recreate all the pipelines if the SPIR-V is valid
  pub fn reload_shaders(&mut self)
  {
    let context = self.context.as_ref().unwrap();
    let device = &context.device;
    // wait until queues are empty
    unsafe { device.device_wait_idle().expect("failed to wait for device!") };

    // Check that the SPIR-V files exist before continuing
    let spirv_file = File::open(Path::new(&self.spirv_path));
    if spirv_file.is_err() {
      // if the SPIR-V does not exist, abort reloading
      println!("failed to open {:?}!", self.spirv_path);
      return;
    }

    Self::cleanup_pipelines(context, &vec![self.graphics_pipeline, self.compute_pipeline]);
    
    // After the initial creation, this acts more like RecreatePipelines
    let graphics_pipeline = Self::create_graphics_pipeline(
      context, &Path::new(&self.spirv_path), 
      self.descriptor_set_layout_global, self.descriptor_set_layout_material, self.surface_format.format
    );
    let compute_pipeline = Self::create_compute_pipeline(
      context, &Path::new(&self.spirv_path), 
      self.descriptor_set_layout_global, self.descriptor_set_layout_material
    );

    self.frame = 0;
    self.graphics_pipeline = graphics_pipeline;
    self.compute_pipeline = compute_pipeline;
  }

  // For an explanation of memory mapping, see the comment "HOW DOES MEMORY WORK?" above CreateUniformBuffers
  // Update the uniform buffer containing the Model View Projection matrix from the Camera
  fn update_model_view_projection(
    context: &EngineContext, camera: &Camera, old_view: glm::Mat4, mvp_buffer_memory: vk::DeviceMemory
  ) -> (bool, glm::Mat4)
  {
    let device = &context.device;
    let mut mvp = MVP {
      model: camera.get_model_matrix(),
      view: camera.get_view_matrix(),
      proj: camera.get_proj_matrix(),
      ..Default::default()
    };
    mvp.inv_view = glm::inverse(&mvp.view.clone());
    mvp.inv_proj = glm::inverse(&mvp.proj.clone());

    let ubos = [mvp];

    // Copy the new data to the mapped data
    let size = size_of::<MVP>() as vk::DeviceSize;
    unsafe {
      let data_ptr = device.map_memory(mvp_buffer_memory, 0, size, vk::MemoryMapFlags::empty()).unwrap();
      let mut align = ash::util::Align::new(data_ptr, align_of::<f32>() as _, size);
      align.copy_from_slice(&ubos);
      device.unmap_memory(mvp_buffer_memory);
    }
    return (old_view != mvp.view, mvp.view);
  }

  // Record commands for compute (dispatching)
  fn record_compute_command_buffers(
    context: &EngineContext, command_buffer: vk::CommandBuffer, pipeline: (vk::PipelineLayout, vk::Pipeline),
    global_descriptor_set: vk::DescriptorSet, material_descriptor_set: vk::DescriptorSet
  )
  {
    let device = &context.device;
    unsafe {
      // Fairly simple, just dispatch a number of threads to work on a compute shader
      device.begin_command_buffer(command_buffer, &vk::CommandBufferBeginInfo::default())
        .expect("failed to begin compute command buffer!");
      device.cmd_bind_pipeline(command_buffer, vk::PipelineBindPoint::COMPUTE, pipeline.1);

      device.cmd_bind_descriptor_sets(
        command_buffer, vk::PipelineBindPoint::COMPUTE, pipeline.0, 0, &[global_descriptor_set], &[]);
      device.cmd_bind_descriptor_sets(
        command_buffer, vk::PipelineBindPoint::COMPUTE, pipeline.0, 1, &[material_descriptor_set], &[]);
    }

    // For path tracing, dispatch(WIDTH, HEIGHT, 1) lets the shader use the threadID as pixel coordinates for writing
    unsafe { device.end_command_buffer(command_buffer).expect("failed to end compute command buffer!") };
  }

  // The graphics command buffer
  fn record_command_buffers(
    context: &EngineContext, renderer: &mut Renderer, draw_data: &DrawData, command_buffer: vk::CommandBuffer, 
    pipeline: (vk::PipelineLayout, vk::Pipeline), image: vk::Image, view: vk::ImageView, extent: vk::Extent2D, 
    depth_image: vk::Image, depth_view: vk::ImageView,
    global_descriptor_set: vk::DescriptorSet, material_descriptor_set: vk::DescriptorSet,
    vertex_data: &VertexData, submeshes: &Vec<SubMesh>
  )
  {
    let device = &context.device;

    unsafe {
      device.begin_command_buffer(command_buffer, &vk::CommandBufferBeginInfo::default())
        .expect("failed to begin draw command buffer!");
    }
    // Transition the swap chain image so its optimal for writing to the colour attachment of the framebuffer
    
    Self::transition_render_texture_layout(
      context, command_buffer, image,
      vk::ImageLayout::UNDEFINED,
      vk::ImageLayout::COLOR_ATTACHMENT_OPTIMAL,
      vk::AccessFlags2::default(),
      vk::AccessFlags2::COLOR_ATTACHMENT_WRITE,
      vk::PipelineStageFlags2::TOP_OF_PIPE,
      vk::PipelineStageFlags2::COLOR_ATTACHMENT_OUTPUT
    );

    let depth_barrier = [vk::ImageMemoryBarrier2::default()
      .src_stage_mask(vk::PipelineStageFlags2::TOP_OF_PIPE)
      .dst_stage_mask(vk::PipelineStageFlags2::EARLY_FRAGMENT_TESTS | vk::PipelineStageFlags2::LATE_FRAGMENT_TESTS)
      .dst_access_mask(vk::AccessFlags2::DEPTH_STENCIL_ATTACHMENT_READ | 
                       vk::AccessFlags2::DEPTH_STENCIL_ATTACHMENT_WRITE)
      .old_layout(vk::ImageLayout::DEPTH_STENCIL_ATTACHMENT_OPTIMAL)
      .new_layout(vk::ImageLayout::DEPTH_STENCIL_ATTACHMENT_OPTIMAL)
      .src_queue_family_index(vk::QUEUE_FAMILY_IGNORED)
      .dst_queue_family_index(vk::QUEUE_FAMILY_IGNORED)
      .image(depth_image)
      .subresource_range(
        vk::ImageSubresourceRange::default()
          .aspect_mask(vk::ImageAspectFlags::DEPTH)
          .base_mip_level(0).level_count(1)
          .base_array_layer(0).layer_count(1)
      )];

    let depth_dependency_info = vk::DependencyInfo::default()
      .image_memory_barriers(&depth_barrier);
    unsafe { device.cmd_pipeline_barrier2(command_buffer, &depth_dependency_info) };

    let clear_depth = vk::ClearValue {
      depth_stencil: vk::ClearDepthStencilValue{depth: 1.0, stencil: 0}};

    let depth_attachment_info = vk::RenderingAttachmentInfo::default()
      .image_view(depth_view)
      .image_layout(vk::ImageLayout::DEPTH_STENCIL_ATTACHMENT_OPTIMAL)
      .load_op(vk::AttachmentLoadOp::CLEAR).store_op(vk::AttachmentStoreOp::DONT_CARE)
      .clear_value(clear_depth);

    let clear_colour = vk::ClearValue {
      color: vk::ClearColorValue{float32: [0.0, 0.0, 0.0, 1.0]}};

    let colour_attachment_info = [vk::RenderingAttachmentInfo::default()
      .image_view(view)
      .image_layout(vk::ImageLayout::ATTACHMENT_OPTIMAL)
      .load_op(vk::AttachmentLoadOp::CLEAR).store_op(vk::AttachmentStoreOp::STORE)
      .clear_value(clear_colour)];

    let rendering_info = vk::RenderingInfo::default()
      .render_area(vk::Rect2D{offset: vk::Offset2D{x:0,y:0}, extent: extent})
      .layer_count(1)
      .color_attachments(&colour_attachment_info)
      .depth_attachment(&depth_attachment_info);

    unsafe { 
      device.cmd_begin_rendering(command_buffer, &rendering_info);

      device.cmd_set_viewport(command_buffer, 0, 
        &[vk::Viewport::default()
          .x(0.0).y(0.0)
          .width(extent.width as f32)
          .height(extent.height as f32)
          .min_depth(0.0).max_depth(1.0)]
      );
      device.cmd_set_scissor(command_buffer, 0, 
        &[vk::Rect2D{offset: vk::Offset2D{x:0,y:0}, extent: extent}]);
    };

  // STATIC MODELS
    {
      unsafe {
        device.cmd_bind_pipeline(command_buffer, vk::PipelineBindPoint::GRAPHICS, pipeline.1);

        device.cmd_bind_vertex_buffers(command_buffer, 0, &[vertex_data.vertex_buffer.0], &[0]);
        device.cmd_bind_index_buffer(command_buffer, vertex_data.index_buffer.0, 0, vk::IndexType::UINT32);
        device.cmd_bind_descriptor_sets(
          command_buffer, vk::PipelineBindPoint::GRAPHICS, pipeline.0, 0, &[global_descriptor_set], &[]);
        device.cmd_bind_descriptor_sets(
          command_buffer, vk::PipelineBindPoint::GRAPHICS, pipeline.0, 1, &[material_descriptor_set], &[]);

        for i in 0..submeshes.len() {
          let push_constant = RasterPushConstant {
              material_index: submeshes[i].material_id.try_into().unwrap(),
          };
          let push_constants = std::slice::from_raw_parts(
            &push_constant as *const _ as *const u8, size_of::<RasterPushConstant>());
      
          device.cmd_push_constants(command_buffer, pipeline.0, vk::ShaderStageFlags::FRAGMENT, 0, push_constants);
          device.cmd_draw_indexed(command_buffer, submeshes[i].index_count, 1, submeshes[i].index_offset, 0, 0);
        }
      }
    }
    // COMPUTE RESULTS
    // render these after model as we want them in front without needing the depth buffer
    renderer.cmd_draw(command_buffer, draw_data).expect("failed to record ImGui draw commands!");    

    // All done dealing with rendering
    unsafe { device.cmd_end_rendering(command_buffer) };

    // Transition the swap chain image, ready for presenting
    Self::transition_render_texture_layout(
      context, command_buffer, image,
      vk::ImageLayout::COLOR_ATTACHMENT_OPTIMAL,
      vk::ImageLayout::PRESENT_SRC_KHR,
      vk::AccessFlags2::COLOR_ATTACHMENT_WRITE,
      vk::AccessFlags2::default(),
      vk::PipelineStageFlags2::COLOR_ATTACHMENT_OUTPUT,
      vk::PipelineStageFlags2::BOTTOM_OF_PIPE
    );

    // All done recording
    unsafe { device.end_command_buffer(command_buffer).expect("failed to end draw command buffer!")};
  }

  // Specify access changes for the swap chain images during command buffer recording
  fn transition_render_texture_layout(
    context: &EngineContext, command_buffer: vk::CommandBuffer, image: vk::Image,
    old_layout: vk::ImageLayout, new_layout: vk::ImageLayout,
    src_access_mask: vk::AccessFlags2, dst_access_mask: vk::AccessFlags2,
    src_stage_mask: vk::PipelineStageFlags2, dst_stage_mask: vk::PipelineStageFlags2
  )
  {
    let device = &context.device;
    // See the other TransitionImageLayout for information ImageMemoryBarrier
    let barrier = [vk::ImageMemoryBarrier2::default()
      .src_stage_mask(src_stage_mask).src_access_mask(src_access_mask)
      .dst_stage_mask(dst_stage_mask).dst_access_mask(dst_access_mask)
      .old_layout(old_layout).new_layout(new_layout)
      .src_queue_family_index(vk::QUEUE_FAMILY_IGNORED)
      .dst_queue_family_index(vk::QUEUE_FAMILY_IGNORED)
      .image(image)
      .subresource_range(
        vk::ImageSubresourceRange::default()
          .aspect_mask(vk::ImageAspectFlags::COLOR)
          .base_mip_level(0).level_count(1)
          .base_array_layer(0).layer_count(1)
      )];

    let dependency_info = vk::DependencyInfo::default()
      .image_memory_barriers(&barrier);

    unsafe { device.cmd_pipeline_barrier2(command_buffer, &dependency_info);}
  }

  // Initialise device space for a Buffer
  fn create_buffer(
    instance: &Instance, device: &Device, physical_device: vk::PhysicalDevice, size: vk::DeviceSize, 
    usage_flags: vk::BufferUsageFlags, memory_flags: vk::MemoryPropertyFlags
  ) -> (vk::Buffer, vk::DeviceMemory)
  {
    let buffer_info = vk::BufferCreateInfo::default()
      .size(size).usage(usage_flags).sharing_mode(vk::SharingMode::EXCLUSIVE);

    let buffer = unsafe {
        device.create_buffer(&buffer_info, None).expect("failed to create buffer!")
    };
    
    // Back up the buffer with DeviceMemory
    let mem_requirements = unsafe { device.get_buffer_memory_requirements(buffer) };
    
    // if the buffer shares a shader device address we need space in the buffer for those addresses
    // let mut alloc_flags_info = vk::MemoryAllocateFlagsInfo::default();
    let mut alloc_flags_info = 
      if usage_flags & vk::BufferUsageFlags::SHADER_DEVICE_ADDRESS != vk::BufferUsageFlags::empty() {
        vk::MemoryAllocateFlagsInfo::default().flags(vk::MemoryAllocateFlags::DEVICE_ADDRESS)
      } 
      else { vk::MemoryAllocateFlagsInfo::default() };
    let alloc_info = vk::MemoryAllocateInfo::default()
      .allocation_size(mem_requirements.size)
      .memory_type_index(Self::find_memory_type(instance, physical_device, mem_requirements, memory_flags))
      .push_next(&mut alloc_flags_info);
    
    let buffer_memory = unsafe {
        device.allocate_memory(&alloc_info, None).expect("failed to allocate DeviceMemory!")
    };
    unsafe { let _ = device.bind_buffer_memory(buffer, buffer_memory, 0); };
    return (buffer, buffer_memory);
  }

  // Simple submission of a buffer copy command to the GPU
  fn copy_buffer(
    device: &Device, command_pool: vk::CommandPool, queue: vk::Queue, 
    src_buffer: vk::Buffer, dst_buffer: vk::Buffer, size: vk::DeviceSize
  )
  {
    let command_copy_buffer = Self::begin_single_time_commands(device, command_pool);
    unsafe { 
      device.cmd_copy_buffer(command_copy_buffer, src_buffer, dst_buffer, &[vk::BufferCopy::default().size(size)]) 
    };
    Self::end_single_time_commands(device, queue, command_copy_buffer);
  }

  // Mip level inclusive copying of a host-visible Buffer to an Image
  fn copy_buffer_to_image(
    device: &Device, command_buffer: vk::CommandBuffer, buffer: vk::Buffer, image: vk::Image,
    initial_width: u32, initial_height: u32, mip_levels: u32, offsets: &Vec<u64>
  )
  {
    let mut regions: Vec<vk::BufferImageCopy> = vec![];
    regions.reserve(mip_levels as usize);

    // Get each mip level as a region of the texture
    for level in 0..mip_levels {
      let offset = offsets[level as usize];

      // Mip levels are always half the size of previous (cascading resolutions)
      // Dividing by 2 is super easy with unsigned integers, a single bit shift towards the endian
      let width = (initial_width >> level).max(1); let height = (initial_height >> level).max(1);

      let region = vk::BufferImageCopy::default()
        .buffer_offset(offset).buffer_row_length(0).buffer_image_height(0)
        .image_subresource(
          vk::ImageSubresourceLayers::default()
            .aspect_mask(vk::ImageAspectFlags::COLOR)
            .mip_level(level)
            .base_array_layer(0).layer_count(1) 
        )
        .image_offset(vk::Offset3D{x:0,y:0,z:0})
        .image_extent(vk::Extent3D{width, height, depth: 1});
      regions.push(region);
    }
    // Copy the collated regions into an image
    unsafe { 
      device.cmd_copy_buffer_to_image(command_buffer, buffer, image, vk::ImageLayout::TRANSFER_DST_OPTIMAL, &regions) 
    };
  }
}

// Write bytes to file (creates file if file does not exist)
fn write_bytes_to_file(file_name: &Path, code: &[u8])
{
  File::create(file_name).unwrap().write_all(code).expect("failed to write code to file!");
}
