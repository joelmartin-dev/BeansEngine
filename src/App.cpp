#include "App.hpp"

//================================================ Standard Libraries ================================================//
#include <iostream>   // writing/reading to/from terminal
#include <fstream>    // writing/reading to.from files
#include <chrono>     // for timekeeping
#include <cstdint>    // for typealiases e.g. uint16_t for unsigned short int
#include <future>     // for async execution
#include <limits>     // for maximums of data types e.g. UINT64_MAX
#include <ranges>     // C++ syntax replacement for common control flow structure e.g. std::for instead of for()
#include <stdexcept>  // for exceptions e.g. runtime_error
#include <vector>     // dynamic arrays

//======= Maths for Graphics =======//
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>

// Some C header-only libraries define a macro used to ensure its implementation code (function definitions, etc.) is 
// only included once to avoid redefinitions of function prototypes
//=================================================== Asset Loading ==================================================//
#define CGLTF_IMPLEMENTATION
#include "cgltf.h"

#include "ktxvulkan.h"

//================================================ Immediate Mode GUI ================================================//
#ifndef IMGUI_IMPL_VULKAN_USE_VOLK
#define IMGUI_IMPL_VULKAN_USE_VOLK
#endif
#include "imgui_impl_vulkan.h"
#include "imgui_impl_glfw.h"
#include "imgui.h"

/*=========================================== Code Style and Idiosyncracies ==========================================//
    This code features a few style quirks that by and large do not affect the execution of the application. They 
    increase readability for me but may be obfuscating to readers not made aware of their existence up front.

      - Character Case Patterns:
          User-defined Functions = PascalCase (distinguishes functions from variables, and user-defined functions
                                   from external functions)
              External Functions = generally camelCase, not decided by me
                       Variables = camelCase (many variable names are single words; the difference between snake_
                                   and camel cases are unnoticeable, distinct from user-defined functions)
                         Structs = PascalCase (I think of them as proper nouns)
                           Types = snake_case (following the conventions of glm, stdlib e.g. aligned_mat4x4,
                                   uint32_t)
      
      - Curly Braces:
          On new line for defined, lambda and control flow functions, same line for struct initialisation.
          Curly braces are used in functions where code is repeated to separate the segments operating on 
          different data e.g. recording graphics and compute command buffers sequentially in the same DrawFrame
          function.

      - Maximum Character Line Length:
          Lines will not exceed 120 characters in length. This ensures that most monitors and views can fit all
          code on screen without horizontal scrolling

      - Exception Handling:
          Throw crucial errors, enable validation layers for the debugMessenger for API internal errors, use
          std::clog for any warnings/info.

      - Usage of Lambda Functions [](){} (C++11 feature):
          Lambda functions are used whenever a function requires another function as an argument, or when a
          non-member function serves one purpose with a single use.
          Lambda structure: 
            [] is captured variables, or variables from outside of the lambda's scope that we want to use inside.
            Lambdas do not maintain the context of their parent.
            () is function arguments. These must match the signature of the invokee's required function.
            {} is the code executed by the lambda.
            Lambda functions can have a specified return type using []() -> type {}, but the compiler's deduction
            works really well so it's generally unnecessary.

      - std algorithms:
          The std libraries provide some ease-of-use algorithms with simple signatures and execution that clearly
          communicate the desired checking of some function over a range. Almost any ranged loop e.g. 
          for (const auto& element : container) can be replaced with the std::ranges algorithm that suits the check.
          For instance, retrieving the index of a desired element, where flag is some namespace alias for a literal:
            
            size_t i = 0, val = SIZE_MAX;
            for (const auto& element : container)
            {
              if (!!(element.flags & flag))
              {
                val = i;
                break;
              }
              i++;
            }
            if (val == SIZE_MAX) throw std::runtime_error("No suitable elements!");
            return val;

          Can become:
            
            auto val = std::find_if(container.begin(), container.end(), [](const auto& element)
              { return !!(element.flags & flag); });
            if (val == container.end()) throw std::runtime_error("No suitable elements!");
            return static_cast<size_t>(std::distance(container.begin(), val));

          The second approach clearly conveys the intent through name and signature, and reduced the number of local
          variables to keep mental track of. There is the possible added obfuscation deriving from unfamiliarity
          with the std::find_if call, and that the value returned is an iterator and not a value. The novel will
          at some point become intuitive.

    Sometimes these style rules will be ignored, as in the case of 

    glfwSetFramebufferSizeCallback(pWindow, [](GLFWwindow* _pWindow, int width, int height)
      { static_cast<App*>(glfwGetWindowUserPointer(_pWindow))->framebufferResized = true; });

    This is simply because it fits roughly in one line, and the readability is not improved by segmenting over
    multiple lines according to the style. Additionally, ignorance of style rules (specifically one-line control
    flow statements) can be an indicator that I do not anticipate those lines to change. Their functionality is
    complete.
*/

// The only function called by main, executes the whole app
void App::Run()
{
  InitWindow();
  InitVulkan();
  InitImGui();
  MainLoop();
  Cleanup();
}

// Create a Window capable of a Vulkan-addressable surface
void App::InitWindow()
{
  // load GLFW functions and members
  if (glfwInit() != GLFW_TRUE) throw std::runtime_error("failed to initialise GLFW!");

  // GLFW supports host-client APIs like OpenCL, OpenGL, but Vulkan is explicit in its hardware usage
  // (no client abstraction)
  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
  
  // Should the window be resizable?
  glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

  // Create and point to a GLFW window: 
  //  with dimensions of WIDTH * HEIGHT,                        WIDTH, HEIGHT,
  //  with a label of "App",                                    "App",
  //  in windowed mode (no monitor for fullscreen specified),   nullptr,
  //  that is not sharing context with another Window           nullptr
  if (!(pWindow = glfwCreateWindow(WIDTH, HEIGHT, "App", nullptr, nullptr))) 
    throw std::runtime_error("failed to create GLFWwindow!");

  // Store a reference to the application within the window
  // Let's us invoke App members from static functions
  glfwSetWindowUserPointer(pWindow, this);
  

  // Using lambdas instead of static functions as I think this reads easier with the functionality tied to setting
  // callback
  // For each of these callbacks we grab the running application from the window, effectively de-staticing the function
  //================================================= Input Callbacks ================================================//
  // when user resizes window
  glfwSetFramebufferSizeCallback(pWindow, [](GLFWwindow* _pWindow, int width, int height) 
    { static_cast<App*>(glfwGetWindowUserPointer(_pWindow))->framebufferResized = true; });
  
  // when user presses key
  glfwSetKeyCallback(pWindow, 
    [](GLFWwindow* _pWindow, int key, int scancode, int action, int mods)
    {
      auto app = static_cast<App*>(glfwGetWindowUserPointer(_pWindow));

      // the camera handles input internally, pass the arguments along
      app->camera.key_handler(_pWindow, key, scancode, action, mods);

      if (action != GLFW_PRESS) return;
      
      switch (key)
      {
      // closes window on next while loop check
      case GLFW_KEY_ESCAPE:
        glfwSetWindowShouldClose(_pWindow, GLFW_TRUE);
        break;
      // reload shaders with Alt + R
      case GLFW_KEY_R:
        // bitwise AND check to see if Alt flagged
        // used to be just R, but editing file paths would cause reloads
        if (mods & GLFW_MOD_ALT)
          static_cast<App*>(glfwGetWindowUserPointer(_pWindow))->ReloadShaders();
        break;
      // recompile graphics shaders with Alt + C, compute shaders with Ctrl + Alt + C
      case GLFW_KEY_C:
        // same reason for Alt as reloading case above
        if (mods & GLFW_MOD_ALT)
        {
          if (mods & GLFW_MOD_CONTROL)
            static_cast<App*>(glfwGetWindowUserPointer(_pWindow))->
              CompileShader(compute_slang_path, compute_spirv_path, true);
          else
          {
            static_cast<App*>(glfwGetWindowUserPointer(_pWindow))->
              CompileShader(slang_path, spirv_path, false);
            static_cast<App*>(glfwGetWindowUserPointer(_pWindow))->
              CompileShader(postprocess_slang_path, postprocess_spirv_path, true);
          }
        }
        break;
      }
    });

  // when user moves mouse
  glfwSetCursorPosCallback(pWindow, 
    [](GLFWwindow* _pWindow, double xpos, double ypos)
    {
      // Application uses two input modes: raw motion and cursor position
      // We only want to check cursor position when raw motion is enabled (requires cursor to be disabled)
      if (glfwGetInputMode(_pWindow, GLFW_CURSOR) == GLFW_CURSOR_DISABLED)
        static_cast<App*>(glfwGetWindowUserPointer(_pWindow))->camera.cursor_handler(xpos, ypos);
    });
  
}

// initialise Vulkan backend and struct members
void App::InitVulkan()
{
  //=========== Abstractions =============//
  CreateInstance();
  SetupDebugMessenger();
  
  CreateSurface();
  
  PickPhysicalDevice();
  CreateLogicalDevice();

  //===== Framebuffers ======//
  CreateSwapChain();
  CreateSwapChainImageViews();

  //===== Command Buffers =====//
  CreateCommandPool();
  CreateCommandBuffers();
  CreateComputeCommandBuffers();
  CreateSyncObjects();
  
  CreateDepthResources(); // Optional
  
  CreateDescriptorSetLayouts(); // Define how data is organised in descriptor sets
  
  InitSlang();// Compile shaders
  CreatePipelines(); // Define how data is passed through stages, with an attached descriptor set layout and shader
  
  //======== Transform external data into usable data =========//
  CreateUniformBuffers();
  CreateShaderStorageBuffers();
  LoadGLTF(std::filesystem::path(model_path).make_preferred());
  CreateVertexBuffer();
  CreateIndexBuffers();
  
  //========== Associate external data with defined members =========//
  CreateDescriptorPools();
  CreateDescriptorSets();
  CreateComputeDescriptorSets();
}

void App::InitImGui()
{
  // Make sure the caller and callee are using the same ImGui version
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();

  // Load ImGui GLFW backend with Vulkan in mind, using the current window for context
  if (!ImGui_ImplGlfw_InitForVulkan(pWindow, true)) // "true" makes ImGui interactive
  {
    throw std::runtime_error("failed to initialise ImGui GLFW backend!");
  }

  // Associate ImGui with all of the already initialised App Vulkan members
  ImGui_ImplVulkan_InitInfo initInfo = {
    .ApiVersion = vk::ApiVersion14,
    .Instance = static_cast<VkInstance>(*instance),
    .PhysicalDevice = static_cast<VkPhysicalDevice>(*physicalDevice),
    .Device = static_cast<VkDevice>(*device),
    .QueueFamily = queueFamilyIndex,
    .Queue = static_cast<VkQueue>(*queue),
    .DescriptorPool = static_cast<VkDescriptorPool>(*imguiDescriptorPool),
    .MinImageCount = static_cast<uint32_t>(swapChainImages.size()),
    .ImageCount = static_cast<uint32_t>(swapChainImages.size()),
    .MSAASamples = VkSampleCountFlagBits::VK_SAMPLE_COUNT_1_BIT,
    .UseDynamicRendering = true,
  };

  // Setup ImGui with its own pipeline
  // ImGui does not require the user to explicitly record its commands
  // Just sneak in ImGui_ImplVulkan_RenderDrawData right before you end recording the final command buffer
  vk::PipelineRenderingCreateInfo imguiPipelineInfo{
    .colorAttachmentCount = 1,
    .pColorAttachmentFormats = reinterpret_cast<const vk::Format*>(&swapChainSurfaceFormat),
    .depthAttachmentFormat = FindDepthFormat()
  };

  initInfo.PipelineRenderingCreateInfo = imguiPipelineInfo;

  // check all the data is consistent
  if (ImGui_ImplVulkan_Init(&initInfo) != true) throw std::runtime_error("failed to initialise ImGuiImplVulkan!");
}

// Runtime logic
void App::MainLoop()
{
  bool showControls = true, showPaths = true; // ImGui window expansion/collapse
 
  // delta is too large as is, so delta / 2^(deltaExp)
  int deltaExp = 19;

  // This is not great logic, but at the moment the glTF assets are loaded as triangle lists meaning every 3 indices is 
  // a triangle (as opposed to triangle strips which is indices.size - 2 as each new index uses the previous two)
  for (auto& mat : mats) stats.tris += static_cast<uint32_t>(mat.indices.size());
  stats.tris /= 3; // total number of indices / 3 should give tri count according to triangle list

  double xpos, ypos; // cursor position

  auto frameStart = std::chrono::system_clock::now(); // time at start of loop (including input polling, ui updating)
  auto frameEnd = std::chrono::system_clock::time_point::max(); // time at end of loop

  while (glfwWindowShouldClose(pWindow) != GLFW_TRUE)
  {
    frameStart = std::chrono::system_clock::now();
    glfwPollEvents(); // check user input

    // if the window was resized, resize framebuffers to match
    if (framebufferResized)
    {
      framebufferResized = false;
      RecreateSwapChain();
    }

    glfwGetCursorPos(pWindow, &xpos, &ypos);

    // A straight >> on delta would not allow for decimals
    camera.update(static_cast<double>(delta) / static_cast<double>(2 << deltaExp));

    // You have to call all of these to update the UI
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    /*=================================================== ImGui UI ===================================================//
        To create a window, use ImGui::Begin(label, showBool, flags).
        Each call to some ImGui UI element occuring between Begin and End are treated as part of the same window.
        ImGui::Spacing adds a bit of vertical padding between elements.
    */

    // CAMERA CONTROLS
    {
      ImGui::Begin("Camera Controls", &showControls, {});
      ImGui::SliderFloat("Move Speed", &camera.moveSpeed, 0.01f, 10.0f);
      auto upper = 30.0, lower = -upper;
      ImGui::SliderScalar("X", ImGuiDataType_Double, &camera.position.x, &lower, &upper);
      ImGui::SliderScalar("Y", ImGuiDataType_Double, &camera.position.y, &lower, &upper);
      ImGui::SliderScalar("Z", ImGuiDataType_Double, &camera.position.z, &lower, &upper);

      ImGui::Spacing();
      
      auto pi   = glm::pi<double>(), neg_pi   = -pi;
      auto pi_2 = pi / 2.0,          neg_pi_2 = -pi_2;
      ImGui::SliderFloat("Pitch Speed", &camera.pitchSpeed, 0.01f, 10.0f);
      ImGui::SliderScalar("Pitch", ImGuiDataType_Double, &camera.pitch, &neg_pi_2, &pi_2);
      ImGui::SliderFloat("Yaw Speed", &camera.yawSpeed, 0.01f, 40.0f);
      ImGui::SliderScalar("Yaw", ImGuiDataType_Double, &camera.yaw, &neg_pi, &pi);
      
      ImGui::Spacing();
      
      ImGui::SliderFloat("FOV", &camera.fov, 20.0f, 170.0f);
      ImGui::SliderFloat("FOV Speed", &camera.fovSpeed, 0.01f, 1000.0f);
      
      ImGui::Spacing();
      
      ImGui::SliderFloat("Shift Speed", &camera.shiftSpeed, 0.01f, 4.0f);
      ImGui::SliderInt("Delta Mult", &deltaExp, 0, 32);
      ImGui::End();
    }
    // SHADER PATHS
    {
      ImGui::Begin("Shaders", &showPaths, {});
      ImGui::InputText("Model Path", model_path, IM_ARRAYSIZE(model_path));
      ImGui::InputText("Slang Path", slang_path, IM_ARRAYSIZE(slang_path));
      ImGui::InputText("SPIR-V Path", spirv_path, IM_ARRAYSIZE(spirv_path));
      ImGui::InputText("Compute Slang Path", compute_slang_path, IM_ARRAYSIZE(compute_slang_path));
      ImGui::InputText("Compute SPIR-V Path", compute_spirv_path, IM_ARRAYSIZE(compute_spirv_path));
      ImGui::InputText("PostProcess Slang Path", postprocess_slang_path, IM_ARRAYSIZE(postprocess_slang_path));
      ImGui::InputText("PostProcess SPIR-V Path", postprocess_spirv_path, IM_ARRAYSIZE(postprocess_spirv_path));
      ImGui::End();
    }

    ImGui::Render(); // Prepares the UI for rendering, DOES NOT RENDER ANYTHING

    // Lock to 60fps                                                                              1/60.0
    while (std::chrono::duration_cast<std::chrono::microseconds>(frameEnd - frameStart).count() < 16667)
    {
      frameEnd = std::chrono::system_clock::now();
    }

    auto drawStart = std::chrono::system_clock::now(); // timing specifically how long all of the graphics stuff takes
    DrawFrame();
    
    frameEnd = std::chrono::system_clock::now();

    // we have to set delta here because the next logical line in the loop will set frameStart
    delta = std::chrono::duration_cast<std::chrono::microseconds>(frameEnd - frameStart).count();
  }
  device.waitIdle();
}

// Free all the memory you've allocated
// RAII and dynamic containers take care of most of this
// Have to explicitly call it on a few things that have caused issues
// and on user-defined structs with RAII members (largely defeating their purpose)
// Libraries tend to include helper functions that do this for you
void App::Cleanup()
{
  // ImGui helpers
  ImGui_ImplVulkan_Shutdown();
  ImGui_ImplGlfw_Shutdown();
  ImGui::DestroyContext();

  {
    fullscreenQuad.indexBuffer = std::pair(nullptr, nullptr);
    fullscreenQuad.graphicsPipeline = std::pair(nullptr, nullptr);
    fullscreenQuad.descriptorSets.clear();
    fullscreenQuad.descriptorPool = nullptr;
    fullscreenQuad.descriptorSetLayout = nullptr;
  }
  // while they are user-defined structs, the vector takes care of them
  // so long as it is explicitly cleared
  mats.clear();

  //computeDescriptorSets.clear();

  // there are issues with GLFW and Wayland that requires these be freed first
  CleanupSwapChain();
  surface = nullptr;

  // GLFW helpers
  glfwDestroyWindow(pWindow);
  glfwTerminate();
}

// Set up base Vulkan instance and RAII context
void App::CreateInstance()
{
  // Load initial Vulkan functions e.g. vkCreateInstance
  if (volkInitialize() != VK_SUCCESS) throw std::runtime_error("failed to initialise volk!");

  // Only apiVersion is relevant to execution, the rest is superfluous
  constexpr vk::ApplicationInfo appInfo {
    .applicationVersion = VK_MAKE_VERSION(1, 0, 0),
    .pEngineName        = "Backend Engine",
    .engineVersion      = VK_MAKE_VERSION(1, 0, 0),
    .apiVersion         = vk::ApiVersion14
  };

  // the only required layers are enabled solely in debug mode
  std::vector<char const*> requiredLayers;
  if (enableValidationLayers)
    requiredLayers.insert(requiredLayers.end(), validationLayers.begin(), validationLayers.end());

  // get available instance layers
  auto layerProperties = context.enumerateInstanceLayerProperties();
  for (auto const& requiredLayer : requiredLayers)
  {
      // if none of the available layers match the required layer, throw error
      if (std::ranges::none_of(layerProperties, [requiredLayer](auto const& layerProperty)
        { return strcmp(layerProperty.layerName, requiredLayer) == 0; }))
          throw std::runtime_error("Required layer not supported: " + std::string(requiredLayer));
  }

  // Construct a vector from the names of the instance extensions required by GLFW
  uint32_t glfwExtensionCount; auto glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);
  std::vector requiredExtensions(glfwExtensions, glfwExtensions + glfwExtensionCount);

  // Add the extension required by debugMessenger
  if (enableValidationLayers) requiredExtensions.push_back(vk::EXTDebugUtilsExtensionName);
   
  // get available instance extensions
  auto extensionProperties = context.enumerateInstanceExtensionProperties();
  for (auto const& requiredExtension : requiredExtensions)
  {
    // if none of the available extensions match the required extension, throw error
    if (std::ranges::none_of(extensionProperties, [requiredExtension](auto const& extensionProperty)
      { return strcmp(extensionProperty.extensionName, requiredExtension) == 0; }))
        throw std::runtime_error("required extension not supported: " + std::string(requiredExtension));
  }

  // By now we've verified that the required layers and extensions are valid and available
  vk::InstanceCreateInfo createInfo {
    .pApplicationInfo = &appInfo,
    .enabledLayerCount = static_cast<uint32_t>(requiredLayers.size()),
    .ppEnabledLayerNames = requiredLayers.data(),
    .enabledExtensionCount = static_cast<uint32_t>(requiredExtensions.size()),
    .ppEnabledExtensionNames = requiredExtensions.data()
  };

  // instantiate the instance, add it to the RAII context
  instance = vk::raii::Instance(context, createInfo);

  // load the relevant Vulkan functions for the instance
  volkLoadInstance(static_cast<VkInstance>(*instance));
}

// A logging function made available to the Vulkan API at runtime
// Determines which types and severities of information are passed through
static VKAPI_ATTR vk::Bool32 VKAPI_CALL DebugCallback(
  vk::DebugUtilsMessageSeverityFlagBitsEXT severity, 
  vk::DebugUtilsMessageTypeFlagsEXT type, 
  const vk::DebugUtilsMessengerCallbackDataEXT* pCallbackData, 
  void*
)
{
  if ((severity & vk::DebugUtilsMessageSeverityFlagBitsEXT::eError) == severity)
    std::cerr << "validation layer: type " << to_string(type) << " msg: " << pCallbackData->pMessage << std::endl;

  return vk::False;
}

// Associate DebugCallback with the Vulkan instance
void App::SetupDebugMessenger()
{
  // Only set up in debug mode
  if (!enableValidationLayers) return;

  // Determine which message severities to even consider printing
  vk::DebugUtilsMessageSeverityFlagsEXT severityFlags(
    vk::DebugUtilsMessageSeverityFlagBitsEXT::eVerbose |
    vk::DebugUtilsMessageSeverityFlagBitsEXT::eInfo    |
    vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning |
    vk::DebugUtilsMessageSeverityFlagBitsEXT::eError
  );
  
  // Determine which message types to even consider printing
  vk::DebugUtilsMessageTypeFlagsEXT messageTypeFlags(
    vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral     |
    vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance |
    vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation
  );

  // The instantiation data for the debugMessenger
  vk::DebugUtilsMessengerCreateInfoEXT debugUtilsMessengerCreateInfoEXT {
    .messageSeverity = severityFlags,
    .messageType = messageTypeFlags,
    .pfnUserCallback = &DebugCallback
  };

  // Associate the debugMessenger with the instance
  debugMessenger = instance.createDebugUtilsMessengerEXT(debugUtilsMessengerCreateInfoEXT);
}

// Abstracting the GLFW window as a presentable surface
void App::CreateSurface()
{
  VkSurfaceKHR _surface; // glfwCreateWindowSurface requires the struct defined in the C API
  if (glfwCreateWindowSurface(*instance, pWindow, nullptr, &_surface) != VK_SUCCESS)
  {
    throw std::runtime_error("failed to create window surface!");
  }

  // create the surface instance by copying from the GLFW provided instance
  surface = vk::raii::SurfaceKHR(instance, _surface);
}

// The Vulkan PhysicalDevice represents a series of capabilities available to the logical Device
// We store this physical device and can query its capabilities and limits whenever
void App::PickPhysicalDevice()
{
  // Get all the capable hardware detected by Vulkan
  std::vector<vk::raii::PhysicalDevice> physicalDevices = instance.enumeratePhysicalDevices();
  
  // If there are no devices detected continuing is impossible
  if (physicalDevices.empty()) throw std::runtime_error("failed to find any physical devices");


  // Find a physical device that is capable of what we need
  // Ranking multiple devices by capability is possible, but we will assume that the first detected Discrete GPU is good
  // enough, if not try the Integrated GPU (found on most modern APUs and laptops)
  const auto device = std::ranges::find_if(physicalDevices, [&]( auto const& _physicalDevice)
    {
      // get the properties of the current device
      vk::PhysicalDeviceProperties properties = _physicalDevice.getProperties();
      // Check if the device supports the Vulkan 1.4 API version
      bool supportsVulkan1_4 = properties.apiVersion >= VK_API_VERSION_1_4;
      // Check if the device is capable of anisotropic sampling (quality transitioning between mip levels)
      bool supportsSamplerAnisotropy = properties.limits.maxSamplerAnisotropy >= 1.0f;
      
      // Get the queue families and their properties of the physical device
      auto queueFamilies = _physicalDevice.getQueueFamilyProperties();
      // Check if any of the queue families supports graphics AND compute operations
      // I have not implemented logic for separate queue families (experimental setup is capable)
      /*======================================== Double Exclamation Marks !! =========================================//
          bitwise AND & returns an int. To convert that int explicitly to a bool we can negate it ! (returns true
          when int == 0) and double-negate it !! (returns true when int != 0). In the following code we want to
          check that the bitwise AND & of queueFlags (where each bit represents the presence of a flag) and Graphics
          and Compute is != 0, hence !!. This is equivalent to != 0.
      */    
      bool supportsGraphicsCompute = std::ranges::any_of(queueFamilies, [](auto const& qfp)
        { return !!(qfp.queueFlags & (vk::QueueFlagBits::eGraphics | vk::QueueFlagBits::eCompute)); });

      // Get the device extensions available on this physical device
      auto availableDeviceExtensions = _physicalDevice.enumerateDeviceExtensionProperties();
      // Check if ALL required device extensions are available
      // Each call to the lambda must return true
      bool supportsAllRequiredExtensions = std::ranges::all_of(requiredDeviceExtensions,
        [&availableDeviceExtensions](auto const& requiredDeviceExtension)
        {
          // Check if the current requiredDeviceExtension is offered by the physical device
          return std::ranges::any_of(availableDeviceExtensions,
            [requiredDeviceExtension](auto const& availableDeviceExtension)
            { return strcmp(availableDeviceExtension.extensionName, requiredDeviceExtension) == 0; });
        }
      );

      // Vulkan has structs available as templates populated by the driver
      // Each of these structs' members have been set by the device, and we can query the members' availability through
      // them. I don't know why we use a .template getFeatures2, but it works and is visually comprehensible
      auto features = _physicalDevice.template getFeatures2<vk::PhysicalDeviceFeatures2,
                                                            vk::PhysicalDeviceDynamicRenderingFeatures,
                                                            vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT,
                                                            vk::PhysicalDeviceTimelineSemaphoreFeaturesKHR>();
      // Query those specific features
      bool supportsRequiredFeatures = 
        features.template get<vk::PhysicalDeviceFeatures2>().features.samplerAnisotropy &&
        features.template get<vk::PhysicalDeviceDynamicRenderingFeatures>().dynamicRendering &&
        features.template get<vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT>().extendedDynamicState &&
        features.template get<vk::PhysicalDeviceTimelineSemaphoreFeaturesKHR>().timelineSemaphore;

      // If all true, this physical device is fit for purpose and we can stop checking
      return supportsVulkan1_4 &&
             supportsSamplerAnisotropy &&
             supportsGraphicsCompute && 
             supportsAllRequiredExtensions &&
             supportsRequiredFeatures;
    }
  );

  // physicalDevices.end() represents an iterator placed at where the next element would go
  // So long as device != that null element, we found a suitable device
  if (device != physicalDevices.end()) 
    physicalDevice = *device;
  // Otherwise, we cannot continue without a device
  else 
    throw std::runtime_error( "failed to find a suitable GPU!" );
}

// Set up as single queue for all needs
// Technically, we checked for this when we found a suitable device and just need to get the index of the suitable
// queue family HOWEVER we are able to double check that the queue does exist as a side effect of looking
uint32_t FindQueueFamilies(const vk::raii::PhysicalDevice& _physicalDevice, const vk::SurfaceKHR& _surface)
{  
  /* Example of how to get a potentially separate queue. For specific queues change the QueueFlagBits and variable names
  auto graphicsQueueFamilyProperties = std::find_if(queueFamilyProperties.begin(), queueFamilyProperties.end(),
    [](const auto& qfp)
    {
      return !!(qfp.queueFlags & vk::QueueFlagBits::eGraphics);
    }
  );
  auto graphicsIndex = 
    static_cast<uint32_t>(std::distance(queueFamilyProperties.begin(), graphicsQueueFamilyProperties));
  */

  std::vector<vk::QueueFamilyProperties> queueFamilyProperties = _physicalDevice.getQueueFamilyProperties();

  auto queueFamilyIndex = std::find_if(queueFamilyProperties.begin(), queueFamilyProperties.end(), [](const auto& qfp)
    {
      // See "Double Exclamation Marks" in PickPhysicalDevice for explanation of !!
      return !!(qfp.queueFlags & (vk::QueueFlagBits::eGraphics | vk::QueueFlagBits::eCompute));
    }
  );

  // Check that the queue exists by seeing if the queueFamilyIndex value changed
  if (queueFamilyIndex == queueFamilyProperties.end()) 
    throw std::runtime_error("could not find a queue for graphics AND compute AND present!");
  
  // return the index of the queue with a graphics queue family
  return static_cast<uint32_t>(std::distance(queueFamilyProperties.begin(), queueFamilyIndex));
}

void App::CreateLogicalDevice()
{
  queueFamilyIndex = FindQueueFamilies(physicalDevice, surface);

  vk::StructureChain<vk::PhysicalDeviceFeatures2,
                     vk::PhysicalDeviceVulkan13Features,
                     vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT,
                     vk::PhysicalDeviceTimelineSemaphoreFeaturesKHR>
  featureChain = {
      { .features = {.samplerAnisotropy = true}},
      {.synchronization2 = true, .dynamicRendering = true},
      {.extendedDynamicState = true},
      {.timelineSemaphore = true}
  };
  
  float queuePriority = 0.0f;

  vk::DeviceQueueCreateInfo deviceQueueCreateInfo {
    .queueFamilyIndex = queueFamilyIndex,
    .queueCount = 1,
    .pQueuePriorities = &queuePriority
  };
  vk::DeviceCreateInfo deviceCreateInfo {
    .pNext = &featureChain.get<vk::PhysicalDeviceFeatures2>(),
    .queueCreateInfoCount = 1,
    .pQueueCreateInfos = &deviceQueueCreateInfo,
    .enabledExtensionCount = static_cast<uint32_t>(requiredDeviceExtensions.size()),
    .ppEnabledExtensionNames = requiredDeviceExtensions.data()
  };

  device = vk::raii::Device(physicalDevice, deviceCreateInfo);
  queue = vk::raii::Queue(device, queueFamilyIndex, 0);
  
  volkLoadDevice(static_cast<VkDevice>(*device));
}

vk::Format chooseSwapSurfaceFormat(const std::vector<vk::SurfaceFormatKHR>& availableFormats)
{
  const auto formIter = std::find_if(availableFormats.begin(), availableFormats.end(), [](const auto& availableFormat)
    {
      return (availableFormat.format == vk::Format::eB8G8R8A8Srgb && availableFormat.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear);
    }
  );

  return formIter != availableFormats.end() ? formIter->format : availableFormats[0].format;
}

vk::PresentModeKHR chooseSwapPresentMode(const std::vector<vk::PresentModeKHR>& availablePresentModes)
{
  const auto presIter = std::find_if(availablePresentModes.begin(), availablePresentModes.end(), [](const auto& availablePresentMode)
    {
      return availablePresentMode == vk::PresentModeKHR::eMailbox;
    }
  );

  return presIter != availablePresentModes.end() ? vk::PresentModeKHR::eMailbox : vk::PresentModeKHR::eFifo;
}

vk::Extent2D chooseSwapExtent(const vk::SurfaceCapabilitiesKHR& capabilities, GLFWwindow* const _pWindow)
{
  if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max())
    return capabilities.currentExtent;

  int width, height;
  glfwGetFramebufferSize(_pWindow, &width, &height);

  return vk::Extent2D{
    std::clamp<uint32_t>(width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width),
    std::clamp<uint32_t>(height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height)
  };
}

void App::CreateSwapChain()
{
  auto surfaceCapabilities = physicalDevice.getSurfaceCapabilitiesKHR(surface);
  swapChainSurfaceFormat = chooseSwapSurfaceFormat(physicalDevice.getSurfaceFormatsKHR(surface));
  auto swapChainPresentMode = chooseSwapPresentMode(physicalDevice.getSurfacePresentModesKHR(surface));
  swapChainExtent = chooseSwapExtent(surfaceCapabilities, pWindow);
  camera.viewportWidth = static_cast<float>(swapChainExtent.width);
  camera.viewportHeight = static_cast<float>(swapChainExtent.height);
  uint32_t minImageCount = std::max(3u, surfaceCapabilities.minImageCount);
  // clamp to the maxImageCount so long as maxImageCount has a maximum and is < than minImageCount
  minImageCount = (surfaceCapabilities.maxImageCount > 0 && minImageCount > surfaceCapabilities.maxImageCount) ? surfaceCapabilities.maxImageCount : minImageCount;
  uint32_t imageCount = surfaceCapabilities.minImageCount + 1;
  if (surfaceCapabilities.maxImageCount > 0 && imageCount > surfaceCapabilities.maxImageCount)
    imageCount = surfaceCapabilities.maxImageCount;

  vk::SwapchainCreateInfoKHR swapChainCreateInfo{
    .flags = vk::SwapchainCreateFlagsKHR(),
    .surface = surface,
    .minImageCount = minImageCount,
    .imageFormat = swapChainSurfaceFormat,
    .imageColorSpace = vk::ColorSpaceKHR::eSrgbNonlinear,
    .imageExtent = swapChainExtent,
    .imageArrayLayers = 1,
    .imageUsage = vk::ImageUsageFlagBits::eColorAttachment,
    .imageSharingMode = vk::SharingMode::eExclusive,
    .preTransform = surfaceCapabilities.currentTransform,
    .compositeAlpha = vk::CompositeAlphaFlagBitsKHR::eOpaque,
    .presentMode = swapChainPresentMode,
    .clipped = vk::True,
    .oldSwapchain = VK_NULL_HANDLE
  };

  swapChain = vk::raii::SwapchainKHR(device, swapChainCreateInfo, nullptr);
  swapChainImages = swapChain.getImages();
}

void App::CreateSwapChainImageViews()
{
  swapChainImageViews.clear();

  vk::ImageViewCreateInfo imageViewCreateInfo{
    .viewType = vk::ImageViewType::e2D,
    .format = swapChainSurfaceFormat,
    .subresourceRange = {
      .aspectMask = vk::ImageAspectFlagBits::eColor,
      .baseMipLevel = 0,
      .levelCount = 1,
      .baseArrayLayer = 0,
      .layerCount = 1
    }
  };

  for (auto image : swapChainImages)
  {
    imageViewCreateInfo.image = image;
    swapChainImageViews.emplace_back(device, imageViewCreateInfo);
  }
}

void App::CreateCommandPool()
{
  vk::CommandPoolCreateInfo commandPoolInfo{
    .flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer,
    .queueFamilyIndex = queueFamilyIndex,
  };
  commandPool = vk::raii::CommandPool(device, commandPoolInfo);
}

void App::CreateCommandBuffers()
{
  commandBuffers.clear();

  vk::CommandBufferAllocateInfo allocInfo{
    .commandPool = commandPool,
    .level = vk::CommandBufferLevel::ePrimary,
    .commandBufferCount = MAX_FRAMES_IN_FLIGHT
  };

  commandBuffers = vk::raii::CommandBuffers(device, allocInfo);
}

void App::CreateComputeCommandBuffers()
{
  computeCommandBuffers.clear();
  vk::CommandBufferAllocateInfo allocInfo{
  .commandPool = commandPool,
  .level = vk::CommandBufferLevel::ePrimary,
  .commandBufferCount = MAX_FRAMES_IN_FLIGHT,
  };
  computeCommandBuffers = vk::raii::CommandBuffers(device, allocInfo);
}

void App::CreateSyncObjects()
{
  vk::SemaphoreTypeCreateInfo semaphoreType{
    .semaphoreType = vk::SemaphoreType::eTimeline,
    .initialValue = 0
  };

  timelineSemaphore = vk::raii::Semaphore(device, { .pNext = &semaphoreType });
  timelineValue = 0;

  presentSemaphores.clear();
  for (size_t i = 0; i < swapChainImages.size(); i++)
  {
    presentSemaphores.emplace_back(device, vk::SemaphoreCreateInfo{});
  }

  inFlightFences.clear();

  for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
  {
    const vk::FenceCreateInfo fenceInfo = {};
    inFlightFences.emplace_back(device, fenceInfo);
  }
}

void App::CreateDepthResources()
{
  vk::Format depthFormat = FindDepthFormat();
  CreateImage(
    swapChainExtent.width,
    swapChainExtent.height,
    1,
    depthFormat,
    vk::ImageTiling::eOptimal,
    vk::ImageUsageFlagBits::eDepthStencilAttachment,
    vk::MemoryPropertyFlagBits::eDeviceLocal,
    depthImage.first,
    depthImage.second
  );
  depthImageView = CreateImageView(depthImage.first, depthFormat, vk::ImageAspectFlagBits::eDepth, 1);
}

void App::CreateDescriptorSetLayouts()
{
  std::array fullscreenBindings = {
    vk::DescriptorSetLayoutBinding(0, vk::DescriptorType::eUniformBuffer, 1, vk::ShaderStageFlagBits::eFragment, nullptr)
  };

  vk::DescriptorSetLayoutCreateInfo fullscreenLayoutInfo{
    .bindingCount = static_cast<uint32_t>(fullscreenBindings.size()),
    .pBindings = fullscreenBindings.data()
  };

  fullscreenQuad.descriptorSetLayout = vk::raii::DescriptorSetLayout(device, fullscreenLayoutInfo);

  std::array bindings = {
    vk::DescriptorSetLayoutBinding(0, vk::DescriptorType::eUniformBuffer, 1, vk::ShaderStageFlagBits::eVertex, nullptr),
    vk::DescriptorSetLayoutBinding(1, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eFragment, nullptr),
  };

  vk::DescriptorSetLayoutCreateInfo layoutInfo{
    .bindingCount = static_cast<uint32_t>(bindings.size()),
    .pBindings = bindings.data()
  };

  descriptorSetLayout = vk::raii::DescriptorSetLayout(device, layoutInfo);

  std::array computeBindings = {
    vk::DescriptorSetLayoutBinding(0, vk::DescriptorType::eUniformBuffer, 1, vk::ShaderStageFlagBits::eCompute, nullptr),
    vk::DescriptorSetLayoutBinding(1, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eCompute, nullptr),
    vk::DescriptorSetLayoutBinding(2, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eCompute, nullptr),
  };

  vk::DescriptorSetLayoutCreateInfo computeLayoutInfo{
    .bindingCount = static_cast<uint32_t>(computeBindings.size()),
    .pBindings = computeBindings.data()
  };

  computeDescriptorSetLayout = vk::raii::DescriptorSetLayout(device, computeLayoutInfo);
}

void App::InitSlang()
{
  SlangGlobalSessionDesc desc = {};
  if (!SLANG_SUCCEEDED(slang::createGlobalSession(&desc, globalSession.writeRef())))
  {
    throw std::runtime_error("failed to create slang globalsession");
  }
  CompileShader(slang_path, spirv_path, false);
  CompileShader(compute_slang_path, compute_spirv_path, false);
  CompileShader(postprocess_slang_path, postprocess_spirv_path, false);
}

void App::CreatePipelines()
{
  CreateGraphicsPipeline();
  CreatePostProcessPipeline();
  CreateComputeGraphicsPipeline();
  CreateComputePipeline();
}

void App::CreateUniformBuffers()
{
  mvpBuffers.clear();
  mvpBuffersMapped.clear();
  uniformBuffers.clear();
  uniformBuffersMapped.clear();

  for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
  {
    vk::DeviceSize mvpBufferSize = sizeof(MVP);
    vk::raii::Buffer mvpBuffer({});
    vk::raii::DeviceMemory mvpBufferMemory({});
    CreateBuffer(mvpBufferSize, vk::BufferUsageFlagBits::eUniformBuffer, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent, mvpBuffer, mvpBufferMemory);
    mvpBuffers.emplace_back(std::pair(std::move(mvpBuffer), std::move(mvpBufferMemory)));
    mvpBuffersMapped.emplace_back(mvpBuffers[i].second.mapMemory(0, mvpBufferSize));

    vk::DeviceSize uniformBufferSize = sizeof(UniformBuffer);
    vk::raii::Buffer uniformBuffer({});
    vk::raii::DeviceMemory uniformBufferMemory({});
    CreateBuffer(uniformBufferSize, vk::BufferUsageFlagBits::eUniformBuffer, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent, uniformBuffer, uniformBufferMemory);
    uniformBuffers.emplace_back(std::pair(std::move(uniformBuffer), std::move(uniformBufferMemory)));
    uniformBuffersMapped.emplace_back(uniformBuffers[i].second.mapMemory(0, uniformBufferSize));
  }
}

void App::CreateShaderStorageBuffers()
{
  std::vector<Particle> particles(PARTICLE_COUNT);
  size_t i = 0;
  for (auto& particle : particles) {
    float r = 0.25f;
    float theta = i * glm::pi<float>() / static_cast<float>(PARTICLE_COUNT) * 2.0f;
    float x = r * cosf(theta) * HEIGHT / WIDTH;
    float y = r * sinf(theta);
    particle.pos = glm::vec2(x, y);
    particle.velocity = normalize(glm::vec2(x, y)) * 0.00025f;
    particle.colour = glm::vec4((sinf(theta) / 2.0f + 1.0f), (cosf(theta) / 2.0f + 1.0f), 0.0f, 1.0f);
    i++;
  }

  vk::DeviceSize bufferSize = sizeof(Particle) * particles.size();

  // Create a staging buffer used to upload data to the gpu
  vk::raii::Buffer stagingBuffer({});
  vk::raii::DeviceMemory stagingBufferMemory({});
  CreateBuffer(bufferSize, vk::BufferUsageFlagBits::eTransferSrc, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent, stagingBuffer, stagingBufferMemory);

  void* dataStaging = stagingBufferMemory.mapMemory(0, bufferSize);
  memcpy(dataStaging, particles.data(), (size_t)bufferSize);
  stagingBufferMemory.unmapMemory();

  shaderStorageBuffers.clear();

  // Copy initial particle data to all storage buffers
  for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
    vk::raii::Buffer shaderStorageBufferTemp({});
    vk::raii::DeviceMemory shaderStorageBufferTempMemory({});
    CreateBuffer(bufferSize, vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eTransferDst, vk::MemoryPropertyFlagBits::eDeviceLocal, shaderStorageBufferTemp, shaderStorageBufferTempMemory);
    CopyBuffer(stagingBuffer, shaderStorageBufferTemp, bufferSize);
    shaderStorageBuffers.emplace_back(std::move(shaderStorageBufferTemp), std::move(shaderStorageBufferTempMemory));
  }
}

void App::LoadGLTF(const std::filesystem::path& path)
{
  LoadAsset(path.generic_string().c_str());
  LoadTextures(path.parent_path());
  CreateTextureSampler();
  LoadGeometry();
}

void App::CreateVertexBuffer()
{
  vk::DeviceSize bufferSize = sizeof(vertices[0]) * vertices.size();
  vk::raii::Buffer stagingBuffer({});
  vk::raii::DeviceMemory stagingBufferMemory({});

  CreateBuffer(
    bufferSize,
    vk::BufferUsageFlagBits::eTransferSrc,
    vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent,
    stagingBuffer,
    stagingBufferMemory
  );

  void* dataStaging = stagingBufferMemory.mapMemory(0, bufferSize);
  memcpy(dataStaging, vertices.data(), bufferSize);
  stagingBufferMemory.unmapMemory();

  CreateBuffer(
    bufferSize,
    vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eTransferDst,
    vk::MemoryPropertyFlagBits::eDeviceLocal,
    vertexBuffer.first,
    vertexBuffer.second
  );

  CopyBuffer(stagingBuffer, vertexBuffer.first, bufferSize);
}

void App::CreateIndexBuffers()
{
  {
    vk::DeviceSize bufferSize = sizeof(fullscreenQuad.indices[0]) * fullscreenQuad.indices.size();
    vk::raii::Buffer stagingBuffer({});
    vk::raii::DeviceMemory stagingBufferMemory({});

    CreateBuffer(
      bufferSize,
      vk::BufferUsageFlagBits::eTransferSrc,
      vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent,
      stagingBuffer,
      stagingBufferMemory
    );

    void* dataStaging = stagingBufferMemory.mapMemory(0, bufferSize);
    memcpy(dataStaging, fullscreenQuad.indices.data(), (size_t)bufferSize);
    stagingBufferMemory.unmapMemory();

    CreateBuffer(
      bufferSize,
      vk::BufferUsageFlagBits::eIndexBuffer | vk::BufferUsageFlagBits::eTransferDst,
      vk::MemoryPropertyFlagBits::eDeviceLocal,
      fullscreenQuad.indexBuffer.first,
      fullscreenQuad.indexBuffer.second
    );

    CopyBuffer(stagingBuffer, fullscreenQuad.indexBuffer.first, bufferSize);
  }

  for (auto& mat : mats)
  {
    vk::DeviceSize bufferSize = sizeof(mat.indices[0]) * mat.indices.size();
    vk::raii::Buffer stagingBuffer({});
    vk::raii::DeviceMemory stagingBufferMemory({});

    CreateBuffer(
      bufferSize,
      vk::BufferUsageFlagBits::eTransferSrc,
      vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent,
      stagingBuffer,
      stagingBufferMemory
    );

    void* dataStaging = stagingBufferMemory.mapMemory(0, bufferSize);
    memcpy(dataStaging, mat.indices.data(), (size_t)bufferSize);
    stagingBufferMemory.unmapMemory();

    CreateBuffer(
      bufferSize,
      vk::BufferUsageFlagBits::eIndexBuffer | vk::BufferUsageFlagBits::eTransferDst,
      vk::MemoryPropertyFlagBits::eDeviceLocal,
      mat.indexBuffer.first,
      mat.indexBuffer.second
    );

    CopyBuffer(stagingBuffer, mat.indexBuffer.first, bufferSize);
  }
}

void App::CreateDescriptorPools()
{
  std::array postprocessPoolSizes{
    vk::DescriptorPoolSize{vk::DescriptorType::eUniformBuffer, MAX_FRAMES_IN_FLIGHT}
  };

  vk::DescriptorPoolCreateInfo postprocessPoolInfo{
    .flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
    .maxSets = MAX_FRAMES_IN_FLIGHT,
    .poolSizeCount = static_cast<uint32_t>(postprocessPoolSizes.size()),
    .pPoolSizes = postprocessPoolSizes.data()
  };

  fullscreenQuad.descriptorPool = vk::raii::DescriptorPool(device, postprocessPoolInfo);

  std::array graphicsPoolSizes = {
    vk::DescriptorPoolSize(vk::DescriptorType::eUniformBuffer, MAX_FRAMES_IN_FLIGHT * static_cast<uint32_t>(mats.size())),
    vk::DescriptorPoolSize(vk::DescriptorType::eCombinedImageSampler, MAX_FRAMES_IN_FLIGHT * static_cast<uint32_t>(mats.size()))
  };

  vk::DescriptorPoolCreateInfo graphicsPoolInfo{
    .flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
    .maxSets = MAX_FRAMES_IN_FLIGHT * static_cast<uint32_t>(mats.size() + 1),
    .poolSizeCount = static_cast<uint32_t>(graphicsPoolSizes.size()),
    .pPoolSizes = graphicsPoolSizes.data()
  };

  graphicsDescriptorPool = vk::raii::DescriptorPool(device, graphicsPoolInfo);

  std::array computePoolSizes = {
    vk::DescriptorPoolSize(vk::DescriptorType::eUniformBuffer, MAX_FRAMES_IN_FLIGHT),
    vk::DescriptorPoolSize(vk::DescriptorType::eStorageBuffer, MAX_FRAMES_IN_FLIGHT * 2)
  };

  vk::DescriptorPoolCreateInfo computePoolInfo{
    .flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
    .maxSets = MAX_FRAMES_IN_FLIGHT,
    .poolSizeCount = static_cast<uint32_t>(computePoolSizes.size()),
    .pPoolSizes = computePoolSizes.data()
  };

  computeDescriptorPool = vk::raii::DescriptorPool(device, computePoolInfo);

  // Create descriptor pool for ImGui
  std::array imguiPoolSizes = {
    vk::DescriptorPoolSize(vk::DescriptorType::eSampler, 2),
    vk::DescriptorPoolSize(vk::DescriptorType::eCombinedImageSampler, 2),
    vk::DescriptorPoolSize(vk::DescriptorType::eSampledImage, 2),
    vk::DescriptorPoolSize(vk::DescriptorType::eStorageImage, 2),
    vk::DescriptorPoolSize(vk::DescriptorType::eUniformTexelBuffer, 2),
    vk::DescriptorPoolSize(vk::DescriptorType::eStorageTexelBuffer, 2),
    vk::DescriptorPoolSize(vk::DescriptorType::eUniformBuffer, 2),
    vk::DescriptorPoolSize(vk::DescriptorType::eStorageBuffer, 2),
    vk::DescriptorPoolSize(vk::DescriptorType::eUniformBufferDynamic, 2),
    vk::DescriptorPoolSize(vk::DescriptorType::eStorageBufferDynamic, 2),
    vk::DescriptorPoolSize(vk::DescriptorType::eInputAttachment, 2)
  };

  vk::DescriptorPoolCreateInfo imguiPoolInfo{
    .flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
    .maxSets = MAX_FRAMES_IN_FLIGHT * static_cast<uint32_t>(imguiPoolSizes.size()),
    .poolSizeCount = static_cast<uint32_t>(imguiPoolSizes.size()),
    .pPoolSizes = imguiPoolSizes.data()
  };

  imguiDescriptorPool = vk::raii::DescriptorPool(device, imguiPoolInfo);
}

void App::CreateDescriptorSets()
{
  {
    std::vector<vk::DescriptorSetLayout> layouts(MAX_FRAMES_IN_FLIGHT, *fullscreenQuad.descriptorSetLayout);
    vk::DescriptorSetAllocateInfo allocInfo{
      .descriptorPool = static_cast<vk::DescriptorPool>(fullscreenQuad.descriptorPool),
      .descriptorSetCount = static_cast<uint32_t>(layouts.size()),
      .pSetLayouts = layouts.data(),
    };
    fullscreenQuad.descriptorSets.clear();
    fullscreenQuad.descriptorSets = device.allocateDescriptorSets(allocInfo);

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
    {
      vk::DescriptorBufferInfo bufferInfo{
        .buffer = static_cast<vk::Buffer>(uniformBuffers[i].first),
        .offset = 0,
        .range = sizeof(UniformBuffer)
      };

      std::array descriptorWrites = {
        vk::WriteDescriptorSet{
          .dstSet = static_cast<vk::DescriptorSet>(fullscreenQuad.descriptorSets[i]),
          .dstBinding = 0,
          .dstArrayElement = 0,
          .descriptorCount = 1,
          .descriptorType = vk::DescriptorType::eUniformBuffer,
          .pBufferInfo = &bufferInfo
        },
      };

      device.updateDescriptorSets(descriptorWrites, {});
    }
  }
  for (auto& mat : mats)
  {
    std::vector<vk::DescriptorSetLayout> layouts(MAX_FRAMES_IN_FLIGHT, *descriptorSetLayout);
    vk::DescriptorSetAllocateInfo allocInfo{
      .descriptorPool = static_cast<vk::DescriptorPool>(graphicsDescriptorPool),
      .descriptorSetCount = static_cast<uint32_t>(layouts.size()),
      .pSetLayouts = layouts.data(),
    };
    mat.descriptorSets.clear();
    mat.descriptorSets = device.allocateDescriptorSets(allocInfo);

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
    {
      vk::DescriptorBufferInfo bufferInfo{
        .buffer = static_cast<vk::Buffer>(mvpBuffers[i].first),
        .offset = 0,
        .range = sizeof(MVP)
      };

      vk::DescriptorImageInfo imageInfo{
        .sampler = static_cast<vk::Sampler>(textureSampler),
        .imageView = static_cast<vk::ImageView>(textureImageViews[mat.imageViewIndex]),
        .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal
      };

      std::array descriptorWrites = {
        vk::WriteDescriptorSet{
          .dstSet = static_cast<vk::DescriptorSet>(mat.descriptorSets[i]),
          .dstBinding = 0,
          .dstArrayElement = 0,
          .descriptorCount = 1,
          .descriptorType = vk::DescriptorType::eUniformBuffer,
          .pBufferInfo = &bufferInfo
        },
        vk::WriteDescriptorSet{
          .dstSet = static_cast<vk::DescriptorSet>(mat.descriptorSets[i]),
          .dstBinding = 1,
          .dstArrayElement = 0,
          .descriptorCount = 1,
          .descriptorType = vk::DescriptorType::eCombinedImageSampler,
          .pImageInfo = &imageInfo
        },
      };

      device.updateDescriptorSets(descriptorWrites, {});
    }
  }
}

void App::CreateComputeDescriptorSets()
{
  std::vector<vk::DescriptorSetLayout> layouts(MAX_FRAMES_IN_FLIGHT, computeDescriptorSetLayout);
  vk::DescriptorSetAllocateInfo allocInfo{
    .descriptorPool = *computeDescriptorPool,
    .descriptorSetCount = MAX_FRAMES_IN_FLIGHT,
    .pSetLayouts = layouts.data()
  };
  computeDescriptorSets.clear();
  computeDescriptorSets = device.allocateDescriptorSets(allocInfo);

  for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
    vk::DescriptorBufferInfo bufferInfo{
      .buffer = uniformBuffers[i].first,
      .offset = 0,
      .range = sizeof(UniformBuffer)
    };

    vk::DescriptorBufferInfo storageBufferInfoLastFrame{
      .buffer = shaderStorageBuffers[(i - 1) % MAX_FRAMES_IN_FLIGHT].first,
      .offset = 0,
      .range = sizeof(Particle) * PARTICLE_COUNT
    };
    vk::DescriptorBufferInfo storageBufferInfoCurrentFrame{
      .buffer = shaderStorageBuffers[i].first,
      .offset = 0,
      .range = sizeof(Particle) * PARTICLE_COUNT
    };
    std::array descriptorWrites{
      vk::WriteDescriptorSet{
        .dstSet = *computeDescriptorSets[i],
        .dstBinding = 0, .dstArrayElement = 0, .descriptorCount = 1,
        .descriptorType = vk::DescriptorType::eUniformBuffer,
        .pImageInfo = nullptr,
        .pBufferInfo = &bufferInfo,
        .pTexelBufferView = nullptr
      },
      vk::WriteDescriptorSet{
        .dstSet = *computeDescriptorSets[i],
        .dstBinding = 1, .dstArrayElement = 0, .descriptorCount = 1,
        .descriptorType = vk::DescriptorType::eStorageBuffer,
        .pImageInfo = nullptr,
        .pBufferInfo = &storageBufferInfoLastFrame,
        .pTexelBufferView = nullptr
      },
      vk::WriteDescriptorSet{
        .dstSet = *computeDescriptorSets[i],
        .dstBinding = 2, .dstArrayElement = 0, .descriptorCount = 1,
        .descriptorType = vk::DescriptorType::eStorageBuffer,
        .pImageInfo = nullptr,
        .pBufferInfo = &storageBufferInfoCurrentFrame,
        .pTexelBufferView = nullptr
      }
    };
    device.updateDescriptorSets(descriptorWrites, {});
  }
}

void App::DrawFrame()
{
  auto [result, imageIndex] = swapChain.acquireNextImage(UINT64_MAX, *presentSemaphores[semaphoreIndex], *inFlightFences[currentFrame]);
  while (vk::Result::eTimeout == device.waitForFences(*inFlightFences[currentFrame], vk::True, UINT64_MAX))
    ;

  if (result == vk::Result::eErrorOutOfDateKHR || framebufferResized)
  {
    framebufferResized = false;
    RecreateSwapChain();
    return;
  }
  else if (result != vk::Result::eSuccess && result != vk::Result::eSuboptimalKHR)
  {
    throw std::runtime_error("failed to acquire swap chain image!");
  }
  device.resetFences(*inFlightFences[currentFrame]);
  UpdateUniformBuffer(currentFrame);
  UpdateModelViewProjection(currentFrame);

  //// Update timeline value for this frame
  uint64_t computeWaitValue = timelineValue;
  uint64_t computeSignalValue = ++timelineValue;
  uint64_t graphicsWaitValue = computeSignalValue;
  uint64_t graphicsSignalValue = ++timelineValue;
  {
    // COMPUTE
    RecordComputeCommandBuffer();

    vk::TimelineSemaphoreSubmitInfo computeTimelineInfo{
      .waitSemaphoreValueCount = 1,
      .pWaitSemaphoreValues = &computeWaitValue,
      .signalSemaphoreValueCount = 1,
      .pSignalSemaphoreValues = &computeSignalValue
    };

    vk::PipelineStageFlags waitStages[] = { vk::PipelineStageFlagBits::eComputeShader };

    vk::SubmitInfo computeSubmitInfo{
      .pNext = &computeTimelineInfo,
      .waitSemaphoreCount = 1,
      .pWaitSemaphores = &*timelineSemaphore,
      .pWaitDstStageMask = waitStages,
      .commandBufferCount = 1,
      .pCommandBuffers = &*computeCommandBuffers[currentFrame],
      .signalSemaphoreCount = 1,
      .pSignalSemaphores = &*timelineSemaphore
    };

    queue.submit(computeSubmitInfo, nullptr);
  }
  {
    // GRAPHICS
    RecordCommandBuffer(imageIndex);

    // Submit graphics work (waits for compute to finish)
    vk::PipelineStageFlags waitStage = vk::PipelineStageFlagBits::eVertexInput;
    vk::TimelineSemaphoreSubmitInfo graphicsTimelineInfo{
        .waitSemaphoreValueCount = 1,
        .pWaitSemaphoreValues = &graphicsWaitValue,
        .signalSemaphoreValueCount = 1,
        .pSignalSemaphoreValues = &graphicsSignalValue
    };

    vk::SubmitInfo graphicsSubmitInfo{
        .pNext = &graphicsTimelineInfo,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &*timelineSemaphore,
        .pWaitDstStageMask = &waitStage,
        .commandBufferCount = 1,
        .pCommandBuffers = &*commandBuffers[currentFrame],
        .signalSemaphoreCount = 1,
        .pSignalSemaphores = &*timelineSemaphore
    };

    queue.submit(graphicsSubmitInfo, nullptr);
  }

  vk::SemaphoreWaitInfo waitInfo{
    .semaphoreCount = 1,
    .pSemaphores = &*timelineSemaphore,
    .pValues = &graphicsSignalValue
  };

  while (vk::Result::eTimeout == device.waitSemaphores(waitInfo, UINT64_MAX))
    ;

  vk::PresentInfoKHR presentInfo{
      .waitSemaphoreCount = 1,
      .pWaitSemaphores = &*presentSemaphores[semaphoreIndex],
      .swapchainCount = 1,
      .pSwapchains = &*swapChain,
      .pImageIndices = &imageIndex
  };

  result = queue.presentKHR(presentInfo);
  if (result == vk::Result::eErrorOutOfDateKHR || result == vk::Result::eSuboptimalKHR || framebufferResized)
  {
    framebufferResized = false;
    RecreateSwapChain();
  }
  else if (result != vk::Result::eSuccess)
  {
    throw std::runtime_error("failed to present swap chain image!");
  }

  semaphoreIndex = (semaphoreIndex + 1) % presentSemaphores.size();
  currentFrame = (currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
}

void App::RecreateSwapChain()
{
  int width, height;
  glfwGetFramebufferSize(pWindow, &width, &height);
  while (width == 0 || height == 0)
  {
    glfwGetFramebufferSize(pWindow, &width, &height);
    glfwWaitEvents();
  }

  device.waitIdle();

  CleanupSwapChain();

  CreateSwapChain();
  CreateSwapChainImageViews();
  CreateDepthResources();
}

void App::CleanupSwapChain()
{
  swapChainImageViews.clear();
  swapChain = nullptr;
}

[[nodiscard]] vk::raii::ShaderModule App::CreateShaderModule(const std::vector<char>& code) const
{
  vk::ShaderModuleCreateInfo createInfo{
    .codeSize = code.size() * sizeof(char),
    .pCode = reinterpret_cast<const uint32_t*>(code.data())
  };

  vk::raii::ShaderModule shaderModule{ device, createInfo };

  return shaderModule;
}

void App::CreateGraphicsPipeline()
{
  graphicsPipeline = std::pair(nullptr, nullptr);
  auto shaderModule = CreateShaderModule(ReadFile(spirv_path));

  vk::PipelineShaderStageCreateInfo vertShaderStageCreateInfo{
    .stage = vk::ShaderStageFlagBits::eVertex,
    .module = shaderModule,
    .pName = "vertMain"
  };
  vk::PipelineShaderStageCreateInfo fragShaderStageCreateInfo{
    .stage = vk::ShaderStageFlagBits::eFragment,
    .module = shaderModule,
    .pName = "fragMain"
  };
  vk::PipelineShaderStageCreateInfo shaderStages[] = { vertShaderStageCreateInfo, fragShaderStageCreateInfo };

  auto bindingDescription = Vertex::getBindingDescription();
  auto attributesDescriptions = Vertex::getAttributeDescriptions();

  vk::PipelineVertexInputStateCreateInfo vertexInputInfo{
    .vertexBindingDescriptionCount = 1,
    .pVertexBindingDescriptions = &bindingDescription,
    .vertexAttributeDescriptionCount = static_cast<uint32_t>(attributesDescriptions.size()),
    .pVertexAttributeDescriptions = attributesDescriptions.data()
  };

  vk::PipelineInputAssemblyStateCreateInfo inputAssemblyInfo{
    .topology = vk::PrimitiveTopology::eTriangleList
  };

  vk::PipelineLayoutCreateInfo pipelineLayoutInfo{
    .setLayoutCount = 1,
    .pSetLayouts = &*descriptorSetLayout,
  };

  graphicsPipeline.first = vk::raii::PipelineLayout(device, pipelineLayoutInfo);

  vk::PipelineRenderingCreateInfo renderingInfo = {
    .colorAttachmentCount = 1,
    .pColorAttachmentFormats = &swapChainSurfaceFormat,
    .depthAttachmentFormat = FindDepthFormat()
  };

  const std::vector<vk::DynamicState> dynamicStates = {
  vk::DynamicState::eViewport,
  vk::DynamicState::eScissor
  };

  const vk::PipelineDynamicStateCreateInfo dynamicInfo{
    .dynamicStateCount = static_cast<uint32_t>(dynamicStates.size()),
    .pDynamicStates = dynamicStates.data()
  };

  const vk::PipelineViewportStateCreateInfo viewportInfo{
    .viewportCount = 1,
    .scissorCount = 1
  };

  const vk::PipelineRasterizationStateCreateInfo rasterizerInfo{
    .depthClampEnable = vk::False,
    .rasterizerDiscardEnable = vk::False,
    .polygonMode = vk::PolygonMode::eFill,
    .cullMode = vk::CullModeFlagBits::eBack,
    .frontFace = vk::FrontFace::eCounterClockwise,
    .depthBiasEnable = vk::False,
    .depthBiasConstantFactor = 0.0f,
    .depthBiasClamp = 0.0f,
    .depthBiasSlopeFactor = 1.0f,
    .lineWidth = 1.0f
  };

  const vk::PipelineMultisampleStateCreateInfo multisamplingInfo{
    .rasterizationSamples = vk::SampleCountFlagBits::e1,
    .sampleShadingEnable = vk::False,
  };

  const vk::PipelineDepthStencilStateCreateInfo depthStencil
  {
    .depthTestEnable = vk::True,
    .depthWriteEnable = vk::True,
    .depthCompareOp = vk::CompareOp::eLess,
    .depthBoundsTestEnable = vk::False,
    .stencilTestEnable = vk::False
  };

  const vk::PipelineColorBlendAttachmentState colorBlendAttachment{
    .blendEnable = vk::True,
    .srcColorBlendFactor = vk::BlendFactor::eSrcAlpha,
    .dstColorBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha,
    .colorBlendOp = vk::BlendOp::eAdd,
    .srcAlphaBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha,
    .dstAlphaBlendFactor = vk::BlendFactor::eZero,
    .alphaBlendOp = vk::BlendOp::eAdd,
    .colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA,
  };

  const vk::PipelineColorBlendStateCreateInfo colorBlendInfo{
    .logicOpEnable = vk::False,
    .logicOp = vk::LogicOp::eCopy,
    .attachmentCount = 1,
    .pAttachments = &colorBlendAttachment
  };

  vk::GraphicsPipelineCreateInfo pipelineInfo{
    .pNext = &renderingInfo,
    .stageCount = sizeof(shaderStages) / sizeof(shaderStages[0]),
    .pStages = shaderStages,
    .pVertexInputState = &vertexInputInfo,
    .pInputAssemblyState = &inputAssemblyInfo,
    .pViewportState = &viewportInfo,
    .pRasterizationState = &rasterizerInfo,
    .pMultisampleState = &multisamplingInfo,
    .pDepthStencilState = &depthStencil,
    .pColorBlendState = &colorBlendInfo,
    .pDynamicState = &dynamicInfo,
    .layout = graphicsPipeline.first,
    .renderPass = nullptr,
    .subpass = 0
  };

  graphicsPipeline.second = vk::raii::Pipeline(device, nullptr, pipelineInfo);
}

void App::CreatePostProcessPipeline()
{
  fullscreenQuad.graphicsPipeline = std::pair(nullptr, nullptr);
  auto shaderModule = CreateShaderModule(ReadFile(postprocess_spirv_path));

  vk::PipelineShaderStageCreateInfo vertShaderStageCreateInfo{
    .stage = vk::ShaderStageFlagBits::eVertex,
    .module = shaderModule,
    .pName = "vertMain"
  };
  vk::PipelineShaderStageCreateInfo fragShaderStageCreateInfo{
    .stage = vk::ShaderStageFlagBits::eFragment,
    .module = shaderModule,
    .pName = "fragMain"
  };
  vk::PipelineShaderStageCreateInfo shaderStages[] = { vertShaderStageCreateInfo, fragShaderStageCreateInfo };

  auto bindingDescription = Vertex::getBindingDescription();
  auto attributesDescriptions = Vertex::getAttributeDescriptions();

  vk::PipelineVertexInputStateCreateInfo vertexInputInfo{
    .vertexBindingDescriptionCount = 1,
    .pVertexBindingDescriptions = &bindingDescription,
    .vertexAttributeDescriptionCount = static_cast<uint32_t>(attributesDescriptions.size()),
    .pVertexAttributeDescriptions = attributesDescriptions.data()
  };

  vk::PipelineInputAssemblyStateCreateInfo inputAssemblyInfo{
    .topology = vk::PrimitiveTopology::eTriangleList
  };

  vk::PipelineLayoutCreateInfo pipelineLayoutInfo{
    .setLayoutCount = 1,
    .pSetLayouts = &*fullscreenQuad.descriptorSetLayout,
  };

  fullscreenQuad.graphicsPipeline.first = vk::raii::PipelineLayout(device, pipelineLayoutInfo);

  vk::PipelineRenderingCreateInfo renderingInfo = {
    .colorAttachmentCount = 1,
    .pColorAttachmentFormats = &swapChainSurfaceFormat,
    .depthAttachmentFormat = FindDepthFormat()
  };

  const std::vector<vk::DynamicState> dynamicStates = {
  vk::DynamicState::eViewport,
  vk::DynamicState::eScissor
  };

  const vk::PipelineDynamicStateCreateInfo dynamicInfo{
    .dynamicStateCount = static_cast<uint32_t>(dynamicStates.size()),
    .pDynamicStates = dynamicStates.data()
  };

  const vk::PipelineViewportStateCreateInfo viewportInfo{
    .viewportCount = 1,
    .scissorCount = 1
  };

  const vk::PipelineRasterizationStateCreateInfo rasterizerInfo{
    .depthClampEnable = vk::False,
    .rasterizerDiscardEnable = vk::False,
    .polygonMode = vk::PolygonMode::eFill,
    .cullMode = vk::CullModeFlagBits::eBack,
    .frontFace = vk::FrontFace::eCounterClockwise,
    .depthBiasEnable = vk::False,
    .depthBiasConstantFactor = 0.0f,
    .depthBiasClamp = 0.0f,
    .depthBiasSlopeFactor = 1.0f,
    .lineWidth = 1.0f
  };

  const vk::PipelineMultisampleStateCreateInfo multisamplingInfo{
    .rasterizationSamples = vk::SampleCountFlagBits::e1,
    .sampleShadingEnable = vk::False,
  };

  const vk::PipelineDepthStencilStateCreateInfo depthStencil
  {
    .depthTestEnable = vk::False,
    .depthWriteEnable = vk::False,
    .depthCompareOp = vk::CompareOp::eLess,
    .depthBoundsTestEnable = vk::False,
    .stencilTestEnable = vk::False
  };

  const vk::PipelineColorBlendAttachmentState colorBlendAttachment{
    .blendEnable = vk::True,
    .srcColorBlendFactor = vk::BlendFactor::eSrcAlpha,
    .dstColorBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha,
    .colorBlendOp = vk::BlendOp::eAdd,
    .srcAlphaBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha,
    .dstAlphaBlendFactor = vk::BlendFactor::eZero,
    .alphaBlendOp = vk::BlendOp::eAdd,
    .colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA,
  };

  const vk::PipelineColorBlendStateCreateInfo colorBlendInfo{
    .logicOpEnable = vk::False,
    .logicOp = vk::LogicOp::eCopy,
    .attachmentCount = 1,
    .pAttachments = &colorBlendAttachment
  };

  vk::GraphicsPipelineCreateInfo pipelineInfo{
    .pNext = &renderingInfo,
    .stageCount = sizeof(shaderStages) / sizeof(shaderStages[0]),
    .pStages = shaderStages,
    .pVertexInputState = &vertexInputInfo,
    .pInputAssemblyState = &inputAssemblyInfo,
    .pViewportState = &viewportInfo,
    .pRasterizationState = &rasterizerInfo,
    .pMultisampleState = &multisamplingInfo,
    .pDepthStencilState = &depthStencil,
    .pColorBlendState = &colorBlendInfo,
    .pDynamicState = &dynamicInfo,
    .layout = fullscreenQuad.graphicsPipeline.first,
    .renderPass = nullptr,
    .subpass = 0
  };

  fullscreenQuad.graphicsPipeline.second = vk::raii::Pipeline(device, nullptr, pipelineInfo);
}


void App::CreateComputeGraphicsPipeline()
{
  computeGraphicsPipeline = std::pair(nullptr, nullptr);
  auto shaderModule = CreateShaderModule(ReadFile(compute_spirv_path));

  vk::PipelineShaderStageCreateInfo vertShaderStageCreateInfo{
    .stage = vk::ShaderStageFlagBits::eVertex,
    .module = shaderModule,
    .pName = "vertMain"
  };
  vk::PipelineShaderStageCreateInfo fragShaderStageCreateInfo{
    .stage = vk::ShaderStageFlagBits::eFragment,
    .module = shaderModule,
    .pName = "fragMain"
  };
  vk::PipelineShaderStageCreateInfo shaderStages[] = { vertShaderStageCreateInfo, fragShaderStageCreateInfo };

  auto bindingDescription = Particle::getBindingDescription();
  auto attributesDescriptions = Particle::getAttributeDescriptions();

  vk::PipelineVertexInputStateCreateInfo vertexInputInfo{
    .vertexBindingDescriptionCount = 1,
    .pVertexBindingDescriptions = &bindingDescription,
    .vertexAttributeDescriptionCount = static_cast<uint32_t>(attributesDescriptions.size()),
    .pVertexAttributeDescriptions = attributesDescriptions.data()
  };

  vk::PipelineInputAssemblyStateCreateInfo inputAssemblyInfo{
    .topology = vk::PrimitiveTopology::ePointList,
    .primitiveRestartEnable = vk::False
  };

  std::vector<vk::DynamicState> dynamicStates = {
    vk::DynamicState::eViewport,
    vk::DynamicState::eScissor
  };

  vk::PipelineDynamicStateCreateInfo dynamicInfo{
    .dynamicStateCount = static_cast<uint32_t>(dynamicStates.size()),
    .pDynamicStates = dynamicStates.data()
  };

  vk::PipelineViewportStateCreateInfo viewportInfo{
    .viewportCount = 1,
    .scissorCount = 1
  };

  vk::PipelineRasterizationStateCreateInfo rasterizerInfo{
    .depthClampEnable = vk::False,
    .rasterizerDiscardEnable = vk::False,
    .polygonMode = vk::PolygonMode::eFill,
    .cullMode = vk::CullModeFlagBits::eBack,
    .frontFace = vk::FrontFace::eCounterClockwise,
    .depthBiasEnable = vk::False,
    .depthBiasConstantFactor = 0.0f,
    .depthBiasClamp = 0.0f,
    .depthBiasSlopeFactor = 1.0f,
    .lineWidth = 1.0f
  };

  vk::PipelineMultisampleStateCreateInfo multisamplingInfo{
    .rasterizationSamples = vk::SampleCountFlagBits::e1,
    .sampleShadingEnable = vk::False,
  };

  vk::PipelineDepthStencilStateCreateInfo depthStencil
  {
    .depthTestEnable = vk::True,
    .depthWriteEnable = vk::False,
    .depthCompareOp = vk::CompareOp::eLessOrEqual,
    .depthBoundsTestEnable = vk::False,
    .stencilTestEnable = vk::False
  };

  vk::PipelineColorBlendAttachmentState colorBlendAttachment{
    .blendEnable = vk::True,
    .srcColorBlendFactor = vk::BlendFactor::eSrcAlpha,
    .dstColorBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha,
    .colorBlendOp = vk::BlendOp::eAdd,
    .srcAlphaBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha,
    .dstAlphaBlendFactor = vk::BlendFactor::eZero,
    .alphaBlendOp = vk::BlendOp::eAdd,
    .colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA,
  };

  vk::PipelineColorBlendStateCreateInfo colorBlendInfo{
    .logicOpEnable = vk::False,
    .logicOp = vk::LogicOp::eCopy,
    .attachmentCount = 1,
    .pAttachments = &colorBlendAttachment
  };

  vk::PipelineLayoutCreateInfo pipelineLayoutInfo;

  computeGraphicsPipeline.first = vk::raii::PipelineLayout(device, pipelineLayoutInfo);

  vk::Format depthFormat = FindDepthFormat();
  vk::PipelineRenderingCreateInfo pipelineRenderingInfo = {
    .colorAttachmentCount = 1,
    .pColorAttachmentFormats = &swapChainSurfaceFormat,
    .depthAttachmentFormat = depthFormat
  };

  vk::GraphicsPipelineCreateInfo graphicsPipelineInfo{
    .pNext = &pipelineRenderingInfo,
    .stageCount = 2,
    .pStages = shaderStages,
    .pVertexInputState = &vertexInputInfo,
    .pInputAssemblyState = &inputAssemblyInfo,
    .pViewportState = &viewportInfo,
    .pRasterizationState = &rasterizerInfo,
    .pMultisampleState = &multisamplingInfo,
    .pDepthStencilState = &depthStencil,
    .pColorBlendState = &colorBlendInfo,
    .pDynamicState = &dynamicInfo,
    .layout = *computeGraphicsPipeline.first,
    .renderPass = nullptr,
    .subpass = 0
  };

  computeGraphicsPipeline.second = vk::raii::Pipeline(device, nullptr, graphicsPipelineInfo);
}

void App::CreateComputePipeline()
{
  computePipeline = std::pair(nullptr, nullptr);
  auto shaderModule = CreateShaderModule(ReadFile(compute_spirv_path));

  vk::PipelineShaderStageCreateInfo computeShaderStageInfo{
    .stage = vk::ShaderStageFlagBits::eCompute,
    .module = shaderModule,
    .pName = "compMain"
  };
  vk::PipelineLayoutCreateInfo pipelineLayoutInfo{
    .setLayoutCount = 1,
    .pSetLayouts = &*computeDescriptorSetLayout
  };
  computePipeline.first = vk::raii::PipelineLayout(device, pipelineLayoutInfo);
  vk::ComputePipelineCreateInfo pipelineInfo{
    .stage = computeShaderStageInfo,
    .layout = *computePipeline.first
  };
  computePipeline.second = vk::raii::Pipeline(device, nullptr, pipelineInfo);
}

void App::LoadAsset(const char* path)
{
  cgltf_options options = {};
  asset = NULL;
  if (cgltf_parse_file(&options, path, &asset) != cgltf_result_success)
  {
    throw std::runtime_error(std::string("failed to load ").append(path));
    cgltf_free(asset);
  }

  if (cgltf_validate(asset) != cgltf_result_success)
  {
    throw std::runtime_error(std::string("failed to validate ").append(path));
    cgltf_free(asset);
  }

  if (cgltf_load_buffers(&options, asset, path) != cgltf_result_success)
  {
    throw std::runtime_error(std::string("failed to load buffers for ").append(path));
    cgltf_free(asset);
  }
}

void App::LoadTextures(const std::filesystem::path& parent_path)
{
  textureImages.clear();
  textureImageViews.clear();

  for (size_t i = 0; i < asset->materials_count; i++)
  {
    textureImages.emplace_back(std::pair(nullptr, nullptr));
    textureImageViews.emplace_back(nullptr);
  }

  mats.clear();
  mats.resize(asset->materials_count);

  std::vector<std::future<void>> futures;
  futures.reserve(asset->materials_count);

  for (size_t i = 0; i < asset->materials_count; ++i) {
    futures.emplace_back(std::async(std::launch::async, [this, &parent_path, i]()
      {
        const char* uri = asset->materials[i].pbr_metallic_roughness.base_color_texture.texture->basisu_image->uri;
        textureImages[i] = CreateTextureImage((parent_path / uri).generic_string().c_str(), i);
        mats[i].imageViewIndex = i;
        mats[i].doubleSided = static_cast<bool>(asset->materials[i].double_sided);
      }
    ));
  }

  // Wait for futures
  for (auto& future : futures) {
    future.wait();
  }

  for (auto& mat : mats)
  {
    if (mat.doubleSided)
    {
      mats_DS.push_back(&mat);
    }
  }
}

[[nodiscard]] std::pair<vk::raii::Image, vk::raii::DeviceMemory> App::CreateTextureImage(const char* texturePath, size_t idx)
{
  ktxTexture2* kTexture;
  auto result = ktxTexture2_CreateFromNamedFile(texturePath, KTX_TEXTURE_CREATE_NO_FLAGS, &kTexture);

  if (result != KTX_SUCCESS)
  {
    throw std::runtime_error(std::string("failed to load ktx texture image: ").append(texturePath));
  }

  if (ktxTexture2_NeedsTranscoding(kTexture))
  {
    ktx_transcode_fmt_e tf = KTX_TTF_BC3_RGBA;
    auto deviceFeatures = physicalDevice.getFeatures();
    if (!deviceFeatures.textureCompressionBC)
    {
      throw std::runtime_error("device cannot transcode to BC");
    }
    result = ktxTexture2_TranscodeBasis(kTexture, tf, 0);
    if (result != KTX_SUCCESS)
    {
      throw std::runtime_error(std::string("failed to transcode ktx texture image: ").append(texturePath));
    }
  }

  auto texWidth = kTexture->baseWidth;
  auto texHeight = kTexture->baseHeight;
  auto mipLevels = kTexture->numLevels;
  auto imageSize = ktxTexture_GetDataSize(ktxTexture(kTexture));
  auto ktxTextureData = ktxTexture_GetData(ktxTexture(kTexture));


  vk::Format textureFormat = static_cast<vk::Format>(ktxTexture2_GetVkFormat(kTexture));

  vk::raii::Buffer stagingBuffer({});
  vk::raii::DeviceMemory stagingBufferMemory({});


  CreateBuffer(
    imageSize,
    vk::BufferUsageFlagBits::eTransferSrc,
    vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent,
    stagingBuffer,
    stagingBufferMemory
  );

  void* data = stagingBufferMemory.mapMemory(0, imageSize);
  memcpy(data, ktxTextureData, imageSize);
  stagingBufferMemory.unmapMemory();

  vk::raii::Image textureImage = nullptr;
  vk::raii::DeviceMemory textureImageMemory = nullptr;

  CreateImage(texWidth, texHeight, mipLevels, textureFormat, vk::ImageTiling::eOptimal, vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled, vk::MemoryPropertyFlagBits::eDeviceLocal, textureImage, textureImageMemory);

  while (!m.try_lock())
    ;
  TransitionImageLayout(textureImage, vk::ImageLayout::eUndefined, vk::ImageLayout::eTransferDstOptimal, mipLevels);
  CopyBufferToImage(stagingBuffer, textureImage, texWidth, texHeight, mipLevels, kTexture);
  TransitionImageLayout(textureImage, vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eShaderReadOnlyOptimal, mipLevels);
  m.unlock();

  ktxTexture2_Destroy(kTexture);

  textureImageViews[idx] = (CreateImageView(textureImage, textureFormat, vk::ImageAspectFlagBits::eColor, mipLevels));
  return std::pair(std::move(textureImage), std::move(textureImageMemory));
}

void App::TransitionImageLayout(
  const vk::raii::Image& image,
  vk::ImageLayout oldLayout, vk::ImageLayout newLayout,
  uint32_t mipLevels
)
{
  const auto commandBuffer = BeginSingleTimeCommands();

  vk::ImageMemoryBarrier barrier{
    .oldLayout = oldLayout,
    .newLayout = newLayout,
    .image = image,
    .subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, mipLevels, 0, 1}
  };

  vk::PipelineStageFlags srcStage;
  vk::PipelineStageFlags dstStage;

  if (oldLayout == vk::ImageLayout::eUndefined && newLayout == vk::ImageLayout::eTransferDstOptimal)
  {
    barrier.srcAccessMask = {};
    barrier.dstAccessMask = vk::AccessFlagBits::eTransferWrite;

    srcStage = vk::PipelineStageFlagBits::eTopOfPipe;
    dstStage = vk::PipelineStageFlagBits::eTransfer;
  }
  else if (oldLayout == vk::ImageLayout::eTransferDstOptimal && newLayout == vk::ImageLayout::eShaderReadOnlyOptimal)
  {
    barrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
    barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;

    srcStage = vk::PipelineStageFlagBits::eTransfer;
    dstStage = vk::PipelineStageFlagBits::eFragmentShader;
  }
  else
  {
    throw std::invalid_argument("unsupported layout transition!");
  }

  commandBuffer->pipelineBarrier(srcStage, dstStage, {}, {}, nullptr, barrier);
  EndSingleTimeCommands(*commandBuffer);
}

void App::CreateTextureSampler()
{
  vk::PhysicalDeviceProperties properties = physicalDevice.getProperties();
  vk::SamplerCreateInfo samplerInfo{
    .magFilter = vk::Filter::eLinear,
    .minFilter = vk::Filter::eLinear,
    .mipmapMode = vk::SamplerMipmapMode::eLinear,
    .addressModeU = vk::SamplerAddressMode::eRepeat,
    .addressModeV = vk::SamplerAddressMode::eRepeat,
    .addressModeW = vk::SamplerAddressMode::eRepeat,
    .mipLodBias = 0.0f,
    .anisotropyEnable = vk::True,
    .maxAnisotropy = properties.limits.maxSamplerAnisotropy,
    .compareEnable = vk::False,
    .compareOp = vk::CompareOp::eAlways,
    .minLod = 0.0f,
    .maxLod = vk::LodClampNone
  };

  textureSampler = vk::raii::Sampler(device, samplerInfo);
}

template<typename T>
std::vector<T> getAccessorData(const cgltf_accessor* accessor)
{
  cgltf_size num_comps = cgltf_num_components(accessor->type);
  cgltf_size num_elems = num_comps * accessor->count;
  cgltf_float* unpacked_data = static_cast<cgltf_float*>(malloc(num_elems * sizeof(cgltf_float)));

  std::vector<T> result;
  result.resize(accessor->count);

  if (unpacked_data)
  {
    cgltf_size written_elems = cgltf_accessor_unpack_floats(accessor, unpacked_data, num_elems);
    for (cgltf_size i = 0; i < written_elems; i += num_comps)
    {
      for (cgltf_size j = 0; j < num_comps; j++)
      {
        result[static_cast<size_t>(i / num_comps)][static_cast<glm::length_t>(j)] = unpacked_data[i + j];
      }
    }
  }

  free(unpacked_data);

  return result;
}

void App::LoadGeometry()
{
  vertices.clear();
  vertices.insert(vertices.end(), fullscreenQuad.vertices.begin(), fullscreenQuad.vertices.end());
  meshes.clear();
  meshes.resize(asset->meshes_count);

  std::vector<Primitive> prims;

  for (cgltf_size meshIt = 0; meshIt < asset->meshes_count; meshIt++)
  {
    auto m = &asset->meshes[meshIt];

    prims.resize(m->primitives_count);

    for (cgltf_size primIt = 0; primIt < m->primitives_count; primIt++)
    {
      auto p = &m->primitives[primIt];

      auto matIdxs = std::views::iota(cgltf_size{ 0 }, asset->materials_count);
      auto matIt = std::ranges::find_if(matIdxs, [&](cgltf_size i) {
        return strcmp(p->material->pbr_metallic_roughness.base_color_texture.texture->basisu_image->uri,
          asset->materials[i].pbr_metallic_roughness.base_color_texture.texture->basisu_image->uri) == 0;
        });

      if (matIt == matIdxs.end()) {
        throw std::runtime_error("failed to find material!");
      }

      mats[*matIt].doubleSided = static_cast<bool>(p->material->double_sided);

      uint32_t v_offset = static_cast<uint32_t>(vertices.size());

      auto indexAccessor = p->indices;

      cgltf_size num_idx_elems = cgltf_num_components(indexAccessor->type) * indexAccessor->count;
      uint32_t* unpacked_indices = static_cast<uint32_t*>(malloc(num_idx_elems * sizeof(uint32_t)));

      if (unpacked_indices)
      {
        cgltf_size written_uints = cgltf_accessor_unpack_indices(indexAccessor, unpacked_indices, static_cast<cgltf_size>(sizeof(uint32_t)), num_idx_elems);

        size_t i_offset = mats[*matIt].indices.size();
        mats[*matIt].indices.resize(mats[*matIt].indices.size() + indexAccessor->count);

        for (cgltf_size i = 0; i < written_uints; i++)
        {
          mats[*matIt].indices[i + i_offset] = unpacked_indices[i] + v_offset;
        }
        free(unpacked_indices);

        if (mats[*matIt].doubleSided)
        {
          mats[*matIt].indices.insert(mats[*matIt].indices.end(), mats[*matIt].indices.rbegin(), mats[*matIt].indices.rbegin() + written_uints);
        }
      }

      cgltf_accessor* posAccessor = NULL; cgltf_accessor* uvAccessor = NULL;
      for (cgltf_size attrIt = 0; attrIt < p->attributes_count; attrIt++)
      {
        auto attr = &p->attributes[attrIt];
        switch (attr->type)
        {
        case cgltf_attribute_type_position:
          posAccessor = attr->data;
          break;
        case cgltf_attribute_type_texcoord:
          uvAccessor = attr->data;
          break;
        default:
          break;
        }
      }

      std::vector<glm::vec3> positions = getAccessorData<glm::vec3>(posAccessor);
      std::vector<glm::vec2> uvs = getAccessorData<glm::vec2>(uvAccessor);

      glm::vec3 scale(1.0f);

      if (asset->nodes[0].has_scale)
      {
        scale.x = asset->nodes[0].scale[0];
        scale.y = asset->nodes[0].scale[1];
        scale.z = asset->nodes[0].scale[2];
      }

      if (posAccessor != NULL)
      {
        vertices.resize(vertices.size() + posAccessor->count);
        for (size_t i = 0; i < posAccessor->count; i++)
        {
          vertices[v_offset + i].pos = positions[i] * scale;
          vertices[v_offset + i].texCoord = uvs[i];
        }
      }
    }
  }
  cgltf_free(asset);
}

void App::CompileShader(const char* src, const char* dst, bool reload)
{
  Slang::ComPtr<slang::ISession> session;

  slang::SessionDesc sessionDesc = {};
  slang::TargetDesc targetDesc = {};
  targetDesc.format = SLANG_SPIRV;
  targetDesc.profile = globalSession->findProfile("spirv_1_4");
  targetDesc.flags = SLANG_TARGET_FLAG_GENERATE_SPIRV_DIRECTLY;
  sessionDesc.targets = &targetDesc;
  sessionDesc.targetCount = 1;
  std::array compilerOptionEntries = {
    slang::CompilerOptionEntry {
      .name = slang::CompilerOptionName::VulkanUseEntryPointName,
      .value = slang::CompilerOptionValue {.intValue0 = true }
    },
    slang::CompilerOptionEntry {
      .name = slang::CompilerOptionName::MatrixLayoutColumn,
      .value = slang::CompilerOptionValue {.intValue0 = true }
    },
    //slang::CompilerOptionEntry {
    //  .name = slang::CompilerOptionName::Capability,
    //  .value = slang::CompilerOptionValue { .kind = slang::CompilerOptionValueKind::String, .stringValue0 = "vk_mem_model" }
    //}
  };
  sessionDesc.compilerOptionEntries = compilerOptionEntries.data();
  sessionDesc.compilerOptionEntryCount = static_cast<uint32_t>(compilerOptionEntries.size());

  auto searchPath = std::filesystem::path(src).parent_path().string();
  const char* searchPaths[] = { searchPath.c_str() };
  sessionDesc.searchPaths = searchPaths;
  sessionDesc.searchPathCount = 1;

  globalSession->createSession(sessionDesc, session.writeRef());

  Slang::ComPtr<slang::IBlob> diagnostics;
  auto moduleName = std::filesystem::path(src).stem().string();
  Slang::ComPtr<slang::IModule> module = static_cast<Slang::ComPtr<slang::IModule>>(session->loadModule(moduleName.c_str(), diagnostics.writeRef()));
  if (diagnostics) std::cerr << (const char*)diagnostics->getBufferPointer() << std::endl;

  Slang::ComPtr<slang::IEntryPoint> vertexEntryPoint;
  module->findAndCheckEntryPoint("vertMain", SLANG_STAGE_VERTEX, vertexEntryPoint.writeRef(), nullptr);

  Slang::ComPtr<slang::IEntryPoint> fragmentEntryPoint;
  module->findAndCheckEntryPoint("fragMain", SLANG_STAGE_FRAGMENT, fragmentEntryPoint.writeRef(), nullptr);

  Slang::ComPtr<slang::IEntryPoint> computeEntryPoint;
  module->findAndCheckEntryPoint("compMain", SLANG_STAGE_COMPUTE, computeEntryPoint.writeRef(), nullptr);

  std::vector<slang::IComponentType*> componentTypes = { module };
  if (vertexEntryPoint) componentTypes.push_back(vertexEntryPoint);
  if (fragmentEntryPoint) componentTypes.push_back(fragmentEntryPoint);
  if (computeEntryPoint) componentTypes.push_back(computeEntryPoint);

  Slang::ComPtr<slang::IComponentType> composedProgram;
  {
    Slang::ComPtr<slang::IBlob> diagnosticsBlob;
    SlangResult result = session->createCompositeComponentType(
      componentTypes.data(),
      componentTypes.size(),
      composedProgram.writeRef(),
      diagnosticsBlob.writeRef());
    if (diagnosticsBlob) std::cerr << diagnosticsBlob << std::endl;

    if (SLANG_FAILED(result)) std::cerr << "failed to compose program" << std::endl;
  }

  {
    Slang::ComPtr<slang::IBlob> diagnosticsBlob;
    Slang::ComPtr<slang::IBlob> spirvCode;
    auto result = composedProgram->getTargetCode(0, spirvCode.writeRef(), diagnosticsBlob.writeRef());
    if (diagnosticsBlob)
      std::cerr << static_cast<const char*>(diagnosticsBlob->getBufferPointer()) << std::endl;
    if (SLANG_FAILED(result))
      std::cerr << "failed to get target code" << std::endl;
    WriteFile(dst, spirvCode->getBufferPointer(), spirvCode->getBufferSize());
  }

  if (reload) ReloadShaders();
}

void App::ReloadShaders()
{
  device.waitIdle();

  auto f = fopen(spirv_path, "r");
  if (f == NULL)
    return;
  fclose(f);

  CreatePipelines();
}

void App::UpdateUniformBuffer(uint32_t imageIndex)
{
  UniformBuffer ubo{};
  ubo.deltaTime = static_cast<float>(delta) / 1000.0f;
  memcpy(uniformBuffersMapped[imageIndex], &ubo, sizeof(ubo));
}

void App::UpdateModelViewProjection(uint32_t imageIndex)
{
  MVP mvp{};
  mvp.mvp = camera.getMVPMatrix();

  memcpy(mvpBuffersMapped[imageIndex], &mvp, sizeof(mvp));
}

void App::RecordComputeCommandBuffer()
{
  computeCommandBuffers[currentFrame].reset();
  computeCommandBuffers[currentFrame].begin({});
  computeCommandBuffers[currentFrame].bindPipeline(vk::PipelineBindPoint::eCompute, computePipeline.second);
  computeCommandBuffers[currentFrame].bindDescriptorSets(vk::PipelineBindPoint::eCompute, computePipeline.first, 0, { computeDescriptorSets[currentFrame] }, {});
  if constexpr (PARTICLE_COUNT >= 256)
    computeCommandBuffers[currentFrame].dispatch(PARTICLE_COUNT / 256, 1, 1);
  else
    computeCommandBuffers[currentFrame].dispatch(1, 1, 1);
  computeCommandBuffers[currentFrame].end();
}

void App::RecordCommandBuffer(uint32_t imageIndex)
{
  commandBuffers[currentFrame].reset();
  commandBuffers[currentFrame].begin({});

  TransitionImageLayout(
    imageIndex,
    vk::ImageLayout::eUndefined,
    vk::ImageLayout::eColorAttachmentOptimal,
    {},
    vk::AccessFlagBits2::eColorAttachmentWrite,
    vk::PipelineStageFlagBits2::eTopOfPipe,
    vk::PipelineStageFlagBits2::eColorAttachmentOutput
  );

  vk::ImageMemoryBarrier2 depthBarrier{
    .srcStageMask = vk::PipelineStageFlagBits2::eTopOfPipe,
    .srcAccessMask = {},
    .dstStageMask = vk::PipelineStageFlagBits2::eEarlyFragmentTests | vk::PipelineStageFlagBits2::eLateFragmentTests,
    .dstAccessMask = vk::AccessFlagBits2::eDepthStencilAttachmentRead | vk::AccessFlagBits2::eDepthStencilAttachmentWrite,
    .oldLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal,
    .newLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal,
    .srcQueueFamilyIndex = vk::QueueFamilyIgnored,
    .dstQueueFamilyIndex = vk::QueueFamilyIgnored,
    .image = depthImage.first,
    .subresourceRange = {
      .aspectMask = vk::ImageAspectFlagBits::eDepth,
      .baseMipLevel = 0,
      .levelCount = 1,
      .baseArrayLayer = 0,
      .layerCount = 1
    }
  };

  vk::DependencyInfo depthDependencyInfo{
    .dependencyFlags = {},
    .imageMemoryBarrierCount = 1,
    .pImageMemoryBarriers = &depthBarrier
  };
  commandBuffers[currentFrame].pipelineBarrier2(depthDependencyInfo);
  vk::ClearDepthStencilValue clearDepth = vk::ClearDepthStencilValue(1.0f, 0);

  vk::RenderingAttachmentInfo depthAttachmentInfo{
    .imageView = depthImageView,
    .imageLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal,
    .loadOp = vk::AttachmentLoadOp::eClear,
    .storeOp = vk::AttachmentStoreOp::eDontCare,
    .clearValue = clearDepth,
  };

  vk::ClearValue clearColour = vk::ClearColorValue(0.0f, 0.0f, 0.0f, 1.0f);
  vk::RenderingAttachmentInfo colourAttachmentInfo{
    .imageView = swapChainImageViews[imageIndex],
    .imageLayout = vk::ImageLayout::eAttachmentOptimal,
    .loadOp = vk::AttachmentLoadOp::eClear,
    .storeOp = vk::AttachmentStoreOp::eStore,
    .clearValue = clearColour,
  };

  vk::RenderingInfo renderingInfo{
    .renderArea = {
      .offset = {0, 0},
      .extent = swapChainExtent
    },
    .layerCount = 1,
    .colorAttachmentCount = 1,
    .pColorAttachments = &colourAttachmentInfo,
    .pDepthAttachment = &depthAttachmentInfo
  };

  commandBuffers[currentFrame].beginRendering(renderingInfo);

  commandBuffers[currentFrame].setViewport(0, vk::Viewport(0.0f, 0.0f, static_cast<float>(swapChainExtent.width), static_cast<float>(swapChainExtent.height), 0.0f, 1.0f));
  commandBuffers[currentFrame].setScissor(0, vk::Rect2D(vk::Offset2D(0, 0), swapChainExtent));

  // STATIC MODELS
  {
    commandBuffers[currentFrame].bindPipeline(
      vk::PipelineBindPoint::eGraphics,
      *graphicsPipeline.second
    );

    commandBuffers[currentFrame].bindVertexBuffers(0, *vertexBuffer.first, { 0 });

    for (auto& mat : mats)
    {
      if (mat.doubleSided) continue; // all the double sided materials have alpha
      commandBuffers[currentFrame].bindIndexBuffer(*mat.indexBuffer.first, 0, vk::IndexType::eUint32);
      commandBuffers[currentFrame].bindDescriptorSets(
        vk::PipelineBindPoint::eGraphics,
        graphicsPipeline.first,
        0,
        *mat.descriptorSets[currentFrame],
        nullptr
      );
      commandBuffers[currentFrame].drawIndexed(
        static_cast<uint32_t>(mat.indices.size()),
        1, 0, 0, 0
      );
    }
    // Draw transparent materials last
    for (auto& mat : mats_DS)
    {
      commandBuffers[currentFrame].bindIndexBuffer(*mat->indexBuffer.first, 0, vk::IndexType::eUint32);
      commandBuffers[currentFrame].bindDescriptorSets(
        vk::PipelineBindPoint::eGraphics,
        graphicsPipeline.first,
        0,
        *mat->descriptorSets[currentFrame],
        nullptr
      );
      commandBuffers[currentFrame].drawIndexed(
        static_cast<uint32_t>(mat->indices.size()),
        1, 0, 0, 0
      );
    }
  }

  // PARTICLES
  // render these after model as we want them in front without writing to the depth buffer, but testing depth
  {
    commandBuffers[currentFrame].bindPipeline(
      vk::PipelineBindPoint::eGraphics,
      *computeGraphicsPipeline.second
    );

    commandBuffers[currentFrame].bindVertexBuffers(0, { shaderStorageBuffers[currentFrame].first }, { 0 });
    commandBuffers[currentFrame].draw(PARTICLE_COUNT, 1, 0, 0);
  }

  // FULLSCREEN EFFECTS
  {
    commandBuffers[currentFrame].bindPipeline(vk::PipelineBindPoint::eGraphics, fullscreenQuad.graphicsPipeline.second);
    // rebind vertex buffer, otherwise it copies the first six particles of one of the compute threads
    commandBuffers[currentFrame].bindVertexBuffers(0, *vertexBuffer.first, { 0 });
    commandBuffers[currentFrame].bindIndexBuffer(*fullscreenQuad.indexBuffer.first, 0, vk::IndexType::eUint16);
    commandBuffers[currentFrame].bindDescriptorSets(
      vk::PipelineBindPoint::eGraphics,
      fullscreenQuad.graphicsPipeline.first,
      0,
      *fullscreenQuad.descriptorSets[currentFrame],
      nullptr
    );
    commandBuffers[currentFrame].drawIndexed(
      static_cast<uint32_t>(fullscreenQuad.indices.size()),
      1, 0, 0, 0
    );
  }

  ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), static_cast<VkCommandBuffer>(*commandBuffers[currentFrame]));

  commandBuffers[currentFrame].endRendering();


  TransitionImageLayout(
    imageIndex,
    vk::ImageLayout::eColorAttachmentOptimal,
    vk::ImageLayout::ePresentSrcKHR,
    vk::AccessFlagBits2::eColorAttachmentWrite,
    {},
    vk::PipelineStageFlagBits2::eColorAttachmentOutput,
    vk::PipelineStageFlagBits2::eBottomOfPipe
  );
  commandBuffers[currentFrame].end();
}

void App::TransitionImageLayout(
  uint32_t imageIndex,
  vk::ImageLayout oldLayout,
  vk::ImageLayout newLayout,
  vk::AccessFlags2 srcAccessMask,
  vk::AccessFlags2 dstAccessMask,
  vk::PipelineStageFlags2 srcStageMask,
  vk::PipelineStageFlags2 dstStageMask
)
{
  vk::ImageMemoryBarrier2 barrier = {
    .srcStageMask = srcStageMask,
    .srcAccessMask = srcAccessMask,
    .dstStageMask = dstStageMask,
    .dstAccessMask = dstAccessMask,
    .oldLayout = oldLayout,
    .newLayout = newLayout,
    .srcQueueFamilyIndex = vk::QueueFamilyIgnored,
    .dstQueueFamilyIndex = vk::QueueFamilyIgnored,
    .image = swapChainImages[imageIndex],
    .subresourceRange = {
      .aspectMask = vk::ImageAspectFlagBits::eColor,
      .baseMipLevel = 0,
      .levelCount = 1,
      .baseArrayLayer = 0,
      .layerCount = 1
    }
  };

  vk::DependencyInfo dependencyInfo{
    .dependencyFlags = {},
    .imageMemoryBarrierCount = 1,
    .pImageMemoryBarriers = &barrier
  };

  commandBuffers[currentFrame].pipelineBarrier2(dependencyInfo);
}

[[nodiscard]] vk::Format App::FindDepthFormat() const
{
  return FindSupportedFormat(
    { vk::Format::eD32Sfloat, vk::Format::eD32SfloatS8Uint, vk::Format::eD24UnormS8Uint },
    vk::ImageTiling::eOptimal,
    vk::FormatFeatureFlagBits::eDepthStencilAttachment
  );
}

vk::Format App::FindSupportedFormat(
  const std::vector<vk::Format>& candidates,
  vk::ImageTiling tiling,
  vk::FormatFeatureFlags features
) const
{
  auto formatIter = std::ranges::find_if(candidates, [&](auto const format)
    {
      vk::FormatProperties props = physicalDevice.getFormatProperties(format);
      return (((tiling == vk::ImageTiling::eLinear) && ((props.linearTilingFeatures & features) == features)) ||
        ((tiling == vk::ImageTiling::eOptimal) && ((props.optimalTilingFeatures & features) == features))
        );
    }
  );
  if (formatIter == candidates.end())
  {
    throw std::runtime_error("failed to find supported format!");
  }
  return *formatIter;
}

uint32_t App::FindMemoryType(uint32_t typeFilter, vk::MemoryPropertyFlags properties)
{
  // typeFilter is a bitmask, and we iterate over it by shifting 1 by i
  // then we check if it has the same properties as properties
  vk::PhysicalDeviceMemoryProperties memProperties = physicalDevice.getMemoryProperties();
  for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++)
  {
    if ((typeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties)
    {
      return i;
    }
  }

  throw std::runtime_error("failed to find suitable memory type!");
}

void App::CreateImage(
  uint32_t width, uint32_t height, uint32_t mipLevels,
  vk::Format format,
  vk::ImageTiling tiling,
  vk::ImageUsageFlags usage,
  vk::MemoryPropertyFlags properties,
  vk::raii::Image& image,
  vk::raii::DeviceMemory& imageMemory
)
{
  vk::ImageCreateInfo imageInfo{
    .imageType = vk::ImageType::e2D,
    .format = format,
    .extent = {width, height, 1},
    .mipLevels = mipLevels,
    .arrayLayers = 1,
    .samples = vk::SampleCountFlagBits::e1,
    .tiling = tiling,
    .usage = usage,
    .sharingMode = vk::SharingMode::eExclusive,
    .queueFamilyIndexCount = 0
  };

  image = vk::raii::Image(device, imageInfo);

  vk::MemoryRequirements memRequirements = image.getMemoryRequirements();
  vk::MemoryAllocateInfo allocInfo{
    .allocationSize = memRequirements.size,
    .memoryTypeIndex = FindMemoryType(memRequirements.memoryTypeBits, properties)
  };
  imageMemory = vk::raii::DeviceMemory(device, allocInfo);
  image.bindMemory(imageMemory, 0);
}

[[nodiscard]] vk::raii::ImageView App::CreateImageView(
  const vk::raii::Image& image,
  vk::Format format,
  vk::ImageAspectFlags aspectFlags,
  uint32_t mipLevels
) const
{
  vk::ImageViewCreateInfo viewInfo{
    .image = image,
    .viewType = vk::ImageViewType::e2D,
    .format = format,
    .subresourceRange = {
      .aspectMask = aspectFlags,
      .baseMipLevel = 0,
      .levelCount = mipLevels,
      .baseArrayLayer = 0,
      .layerCount = 1
    }
  };

  return vk::raii::ImageView(device, viewInfo);
}

std::unique_ptr<vk::raii::CommandBuffer> App::BeginSingleTimeCommands() const
{
  vk::CommandBufferAllocateInfo allocInfo{
    .commandPool = commandPool,
    .level = vk::CommandBufferLevel::ePrimary,
    .commandBufferCount = 1
  };
  std::unique_ptr<vk::raii::CommandBuffer> commandBuffer = std::make_unique<vk::raii::CommandBuffer>(std::move(device.allocateCommandBuffers(allocInfo).front()));

  vk::CommandBufferBeginInfo beginInfo{
    .flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit
  };
  commandBuffer->begin(beginInfo);

  return commandBuffer;
}

void App::EndSingleTimeCommands(const vk::raii::CommandBuffer& commandBuffer) const
{
  commandBuffer.end();

  vk::SubmitInfo submitInfo{
    .commandBufferCount = 1,
    .pCommandBuffers = &*commandBuffer
  };
  queue.submit(submitInfo, nullptr);
  queue.waitIdle();
}

void App::CreateBuffer(
  vk::DeviceSize size,
  vk::BufferUsageFlags usage,
  vk::MemoryPropertyFlags properties,
  vk::raii::Buffer& buffer,
  vk::raii::DeviceMemory& bufferMemory
)
{
  vk::BufferCreateInfo bufferInfo{
    .size = size,
    .usage = usage,
    .sharingMode = vk::SharingMode::eExclusive
  };
  buffer = vk::raii::Buffer(device, bufferInfo);
  vk::MemoryRequirements memRequirements = buffer.getMemoryRequirements();
  vk::MemoryAllocateInfo allocInfo{
    .allocationSize = memRequirements.size,
    .memoryTypeIndex = FindMemoryType(memRequirements.memoryTypeBits, properties)
  };
  bufferMemory = vk::raii::DeviceMemory(device, allocInfo);
  buffer.bindMemory(*bufferMemory, 0);
}

void App::CopyBuffer(
  const vk::raii::Buffer& srcBuffer,
  const vk::raii::Buffer& dstBuffer,
  vk::DeviceSize size
)
{
  vk::CommandBufferAllocateInfo allocInfo{
    .commandPool = commandPool,
    .level = vk::CommandBufferLevel::ePrimary,
    .commandBufferCount = 1
  };
  vk::raii::CommandBuffer commandCopyBuffer = std::move(device.allocateCommandBuffers(allocInfo).front());
  commandCopyBuffer.begin(vk::CommandBufferBeginInfo{ .flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit });
  commandCopyBuffer.copyBuffer(*srcBuffer, *dstBuffer, vk::BufferCopy{ .size = size });
  commandCopyBuffer.end();
  queue.submit(vk::SubmitInfo{ .commandBufferCount = 1, .pCommandBuffers = &*commandCopyBuffer }, nullptr);
  queue.waitIdle();
}

void App::CopyBufferToImage(
  const vk::raii::Buffer& buffer,
  const vk::raii::Image& image,
  uint32_t width, uint32_t height, uint32_t mipLevels,
  ktxTexture2* kTexture
)
{
  std::unique_ptr<vk::raii::CommandBuffer> commandBuffer = BeginSingleTimeCommands();

  std::vector<vk::BufferImageCopy> regions;

  for (uint32_t level = 0; level < mipLevels; level++)
  {
    ktx_size_t offset;
    ktxTexture2_GetImageOffset(kTexture, level, 0, 0, &offset);

    uint32_t mipWidth = std::max(1u, width >> level);
    uint32_t mipHeight = std::max(1u, height >> level);

    vk::BufferImageCopy region {
      .bufferOffset = offset,
      .bufferRowLength = 0,
      .bufferImageHeight = 0,
      .imageSubresource = {
        .aspectMask = vk::ImageAspectFlagBits::eColor,
        .mipLevel = level,
        .baseArrayLayer = 0,
        .layerCount = 1
      },
      .imageOffset = { 0, 0, 0 },
      .imageExtent = {
        .width = mipWidth,
        .height = mipHeight,
        .depth = 1
      }
    };

    regions.push_back(region);
  }

  commandBuffer->copyBufferToImage(buffer, image, vk::ImageLayout::eTransferDstOptimal, regions);
  EndSingleTimeCommands(*commandBuffer);
}

static const std::vector<char> ReadFile(const char* fileName)
{
  std::ifstream file(fileName, std::ios::ate | std::ios::binary);

  if (!file.is_open())
  {
    throw std::runtime_error("failed to open file!");
  }

  std::vector<char> buffer(file.tellg());
  file.seekg(0, std::ios::beg);
  file.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
  file.close();
  return buffer;
}

static void WriteFile(const char* fileName, void const* code, size_t bytes)
{
  std::ofstream file;
  file = std::ofstream(fileName, std::ios::binary);

  if (!file.is_open())
  {
    throw std::runtime_error("failed to open file!");
  }
  file.write(static_cast<const char*>(code), bytes);
  file.close();
}
