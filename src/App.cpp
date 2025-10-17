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
          On new line for user-defined functions, same line for struct initialisation, lambda and control flow
          functions. Curly braces are also used within functions where code sequences are repeated, to separate the
          segments operating on different data e.g. recording graphics and compute command buffers sequentially in
          the same DrawFrame function. They may be used to scope variables whose names will be reused within the
          same function for a near-identical purpose. Example:
          
            void UserFunc()
            {  
              {
                UserStruct init { .member = value };
                a = api::FunctionName(init);
              }
              {
                UserStruct init { .member = other_val };
                b = api::FunctionName(init);
              }
            }

          The name "init" is used as input for identical operations in both scopes that are in the same function.
          This communicates the intent of the variable, and recognises its shared purpose.

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
            for (const auto& element : container) {
              if (!!(element.flags & flag)) {
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

    - vectors vs arrays:
        std:vector is often used identically to an array, but with the benefit of returning with a number of
        elements unknown at time of call. <vector>.resize(n) is often used to avoid the dynamic growing performed by
        vectors when emplacing/pushing new elements. It is especially crucial in the case of vectors of 3D data as
        they can have thousands to millions of elements and automatic vector growing is often implemented as 1.5x to
        2x current size.

    - runtime_errors:
        If something goes wrong application-side (not API-side, that is handled through Vulkan validation layers)
        the application will throw an error and close. Some of these could be replaced by logging and reattempting
        until success, as in the case of file writing.

    - <vector member>.clear():
        Anytime a vector class member is initialised in the implementation it is cleared right before elements are
        inserted. This ensures that the member is empty.

    - auto p = static_cast<T>(q):
        It may seem odd to pair inferred typing with explicit casting as the result would often be the same as
        explicitly typing p and implicitly casting q: T p = q. However, implicit casts are susceptible to data loss.
        static_cast ensures no data loss during the cast, and since the type is enforced through the static_cast
        auto will infer that type. You can completely trust in the static_cast for the type.
*/

// The only function called by main, executes the whole app
void App::Run()
{
  InitWindow();
  InitVulkan();
#ifdef _DEBUG
  InitImGui();
#endif
  MainLoop();
  Cleanup();
}

// Create a Window capable of a Vulkan-addressable surface
void App::InitWindow()
{
#ifndef _WIN32
  if (!useWayland){
    std::clog << "falling back to x11" << std::endl;
    glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_X11);
  } 
    
#endif
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
  glfwSetFramebufferSizeCallback(pWindow, [](GLFWwindow* _pWindow, int width, int height) { 
    static_cast<App*>(glfwGetWindowUserPointer(_pWindow))->framebufferResized = true;
  });
  
  // when user presses key
  glfwSetKeyCallback(pWindow, [](GLFWwindow* _pWindow, int key, int scancode, int action, int mods) {
      auto app = static_cast<App*>(glfwGetWindowUserPointer(_pWindow));

      // the camera handles input internally, pass the arguments along
      app->camera.KeyHandler(_pWindow, key, scancode, action, mods);

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
              CompileShader(compute_slang_path, compute_spirv_path);
          else {
            static_cast<App*>(glfwGetWindowUserPointer(_pWindow))->
              CompileShader(slang_path, spirv_path);
          }
          static_cast<App*>(glfwGetWindowUserPointer(_pWindow))->ReloadShaders();
        }
        break;
      }
  });

  // when user moves mouse
  glfwSetCursorPosCallback(pWindow, [](GLFWwindow* _pWindow, double xpos, double ypos) {
      // Application uses two input modes: raw motion and cursor position
      // We only want to check cursor position when raw motion is enabled (requires cursor to be disabled)
      if (glfwGetInputMode(_pWindow, GLFW_CURSOR) == GLFW_CURSOR_DISABLED)
        static_cast<App*>(glfwGetWindowUserPointer(_pWindow))->camera.CursorHandler(xpos, ypos);
  });
}

// initialise Vulkan backend and struct members
void App::InitVulkan()
{
  auto timer_start = std::chrono::system_clock::now();
  //=========== Abstractions =============//
  CreateInstance();
  SetupDebugMessenger();
  
  CreateSurface();
  
  PickPhysicalDevice();
  CreateLogicalDevice();

  //===== Framebuffers ======//
  CreateSwapChain();
  CreateSwapChainImageViews();
  CreateDepthResources(); // Optional

  //===== Command Buffers =====//
  CreateCommandPool();
  CreateCommandBuffers();
  CreateComputeCommandBuffers();
  CreateSyncObjects();
  
  // We need to know the number of textures BEFORE we create the descriptor set layouts
  LoadGLTF(std::filesystem::path(model_path).make_preferred());
  CreateDescriptorSetLayouts(); // Define how data is organised in descriptor sets
  
  InitSlang();// Compile shaders
  CreatePipelines(); // Define how data is passed through stages, with an attached descriptor set layout and shader
  
  //======== Transform external data into usable data =========//
  CreateUniformBuffers();
  CreateVertexBuffers();
  CreateIndexBuffers();
  CreateUVBuffer();
#ifdef REFERENCE
  CreateAccelerationStructures();
  //CreateInstanceLUTBuffer();
#endif

  CreatePathTracingTexture();
  
  //========== Associate external data with defined members =========//
  CreateDescriptorPools();
  CreateDescriptorSets();
  auto timer_end = std::chrono::system_clock::now();
  std::clog << std::chrono::duration_cast<std::chrono::milliseconds>(timer_end - timer_start) << std::endl;
}

// Setup ImGui for use with an already instantiated GLFW Vulkan application
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

  // ImGui does not require the user to explicitly record its commands, just sneak in ImGui_ImplVulkan_RenderDrawData
  // right before you end recording the final command buffer
  
  // Setup ImGui with its own pipeline
  vk::PipelineRenderingCreateInfo imguiPipelineInfo{
    .colorAttachmentCount = 1,
    .pColorAttachmentFormats = reinterpret_cast<const vk::Format*>(&swapChainSurfaceFormat),
    .depthAttachmentFormat = FindDepthFormat()
  };
  initInfo.PipelineRenderingCreateInfo = imguiPipelineInfo;

  if (ImGui_ImplVulkan_Init(&initInfo) != true) throw std::runtime_error("failed to initialise ImGuiImplVulkan!");
}

// Runtime logic
void App::MainLoop()
{
  bool showControls = true, showPaths = true; // ImGui window expansion/collapse

  camera.viewportWidth = static_cast<float>(swapChainExtent.width);
  camera.viewportHeight = static_cast<float>(swapChainExtent.height);
 
  // delta is too large as is, so delta / 2^(deltaExp)
  int deltaExp = 19;

  // This is not great logic, but at the moment the glTF assets are loaded as triangle lists meaning every 3 indices is 
  // a triangle (as opposed to triangle strips which is indices.size - 2 as each new index uses the previous two)
  stats.tris = vertices.size() / 3; // total number of indices / 3 should give tri count according to triangle list

  double xpos, ypos; // cursor position

  auto frameStart = std::chrono::system_clock::now(); // time at start of loop (including input polling, ui updating)
  auto frameEnd = std::chrono::system_clock::time_point::max(); // time at end of loop

  while (glfwWindowShouldClose(pWindow) != GLFW_TRUE) { // while user has not quit
    frameStart = std::chrono::system_clock::now();
    glfwPollEvents(); // check user input

    // if the window was resized, resize framebuffers to match
    if (framebufferResized) {
      framebufferResized = false;
      RecreateSwapChain();
    }

#ifdef _DEBUG
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
      ImGui::Text("%.2fms", delta / 1000.0f);
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
      ImGui::End();
    }

    ImGui::Render(); // Prepares the UI for rendering, DOES NOT RENDER ANYTHING
#endif

    auto drawStart = std::chrono::system_clock::now(); // timing specifically how long all of the graphics stuff takes
    DrawFrame();

    // Updating variables for next frame (everything is delta-dependent, so effects won't take impact until next frame)
    glfwGetCursorPos(pWindow, &xpos, &ypos);

    // A straight >> on delta would not allow for decimals
    camera.Update(static_cast<double>(delta) / static_cast<double>(2 << deltaExp));

    // Lock to 60fps, wait for previous frame to finish                                           1/60.0
    while (std::chrono::duration_cast<std::chrono::microseconds>(frameEnd - frameStart).count() < 16667)
      frameEnd = std::chrono::system_clock::now();
    
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
#ifdef _DEBUG
  // ImGui helpers
  ImGui_ImplVulkan_Shutdown();
  ImGui_ImplGlfw_Shutdown();
  ImGui::DestroyContext();
#endif

  submeshes.clear();
  
  {
    radianceCascadesOutput.graphicsPipeline = std::pair(nullptr, nullptr);
    radianceCascadesOutput.descriptorSets.clear();
    radianceCascadesOutput.descriptorSetLayout = nullptr;
    radianceCascadesOutput.descriptorPool = nullptr;
  }

  globalDescriptorSets.clear();
  materialDescriptorSets.clear();
  computeDescriptorSets.clear();

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
  for (auto const& requiredLayer : requiredLayers) {
      // if none of the available layers match the required layer, throw error
      if (std::ranges::none_of(layerProperties, [requiredLayer](auto const& layerProperty) {
        return strcmp(layerProperty.layerName, requiredLayer) == 0;
      }))
          throw std::runtime_error("Required layer not supported: " + std::string(requiredLayer));
  }

  // Construct a vector from the names of the instance extensions required by GLFW
  uint32_t glfwExtensionCount; auto glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);
  std::vector requiredExtensions(glfwExtensions, glfwExtensions + glfwExtensionCount);

  // Add the extension required by debugMessenger
  if (enableValidationLayers) requiredExtensions.push_back(vk::EXTDebugUtilsExtensionName);
   
  // get available instance extensions
  auto extensionProperties = context.enumerateInstanceExtensionProperties();
  for (auto const& requiredExtension : requiredExtensions) {
    // if none of the available extensions match the required extension, throw error
    if (std::ranges::none_of(extensionProperties, [requiredExtension](auto const& extensionProperty) {
      return strcmp(extensionProperty.extensionName, requiredExtension) == 0;
    }))
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
    throw std::runtime_error("failed to create window surface!");

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
  const auto device = std::ranges::find_if(physicalDevices, [&]( auto const& _physicalDevice) {
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
      bool supportsGraphicsCompute = std::ranges::any_of(queueFamilies, [](auto const& qfp) {
          return !!(qfp.queueFlags & (vk::QueueFlagBits::eGraphics | vk::QueueFlagBits::eCompute));
        });

      // Get the device extensions available on this physical device
      auto availableDeviceExtensions = _physicalDevice.enumerateDeviceExtensionProperties();
      // Check if ALL required device extensions are available
      // Each call to the lambda must return true
      bool supportsAllRequiredExtensions = std::ranges::all_of(requiredDeviceExtensions,
        [&availableDeviceExtensions](auto const& requiredDeviceExtension) {
          // Check if the current requiredDeviceExtension is offered by the physical device
          return std::ranges::any_of(availableDeviceExtensions,
            [requiredDeviceExtension](auto const& availableDeviceExtension) {
            return strcmp(availableDeviceExtension.extensionName, requiredDeviceExtension) == 0;
          });
        });

      // Vulkan has structs available as templates populated by the driver
      // Each of these structs' members have been set by the device, and we can query the members' availability through
      // them. I don't know why we use a .template getFeatures2, but it works and is visually comprehensible
      auto features = _physicalDevice.template getFeatures2<vk::PhysicalDeviceFeatures2,
                                                            vk::PhysicalDeviceVulkan12Features,
                                                            vk::PhysicalDeviceVulkan13Features,
                                                            vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT,
                                                            vk::PhysicalDeviceAccelerationStructureFeaturesKHR,
                                                            vk::PhysicalDeviceRayQueryFeaturesKHR,
                                                            vk::PhysicalDeviceTimelineSemaphoreFeaturesKHR>();
      // Query those specific features against the available implementation (the device's Vulkan driver)
      bool supportsRequiredFeatures = 
        // allows anisotropic sampling to some degree
        features.template get<vk::PhysicalDeviceFeatures2>().features.samplerAnisotropy &&
        // simplified API for Vulkan synchronization objects e.g. semaphores, fences
        features.template get<vk::PhysicalDeviceVulkan13Features>().synchronization2 &&
        // allows for implicit render passes
        features.template get<vk::PhysicalDeviceVulkan13Features>().dynamicRendering &&
        features.template get<vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT>().extendedDynamicState &&
        features.template get<vk::PhysicalDeviceVulkan12Features>().descriptorBindingSampledImageUpdateAfterBind &&
        features.template get<vk::PhysicalDeviceVulkan12Features>().descriptorBindingPartiallyBound &&
        features.template get<vk::PhysicalDeviceVulkan12Features>().descriptorBindingVariableDescriptorCount &&
        features.template get<vk::PhysicalDeviceVulkan12Features>().runtimeDescriptorArray &&
        features.template get<vk::PhysicalDeviceVulkan12Features>().shaderSampledImageArrayNonUniformIndexing &&
        features.template get<vk::PhysicalDeviceVulkan12Features>().bufferDeviceAddress &&
        // makes timeline semaphores available for synchronisation
        features.template get<vk::PhysicalDeviceVulkan12Features>().timelineSemaphore &&
        features.template get<vk::PhysicalDeviceAccelerationStructureFeaturesKHR>().accelerationStructure &&
        features.template get<vk::PhysicalDeviceRayQueryFeaturesKHR>().rayQuery;
        
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
  if (device != physicalDevices.end()) physicalDevice = *device;
  // Otherwise, we cannot continue without a device
  else throw std::runtime_error( "failed to find a suitable GPU!" );
}

// Set up as single queue for all needs
// Technically, we checked for this when we found a suitable device and just need to get the index of the suitable
// queue family HOWEVER we are able to double check that the queue does exist as a side effect of looking
uint32_t FindQueueFamilies(const vk::raii::PhysicalDevice& _physicalDevice, const vk::SurfaceKHR& _surface)
{  
  /* Example of how to get a potentially separate queue. For specific queues change the QueueFlagBits and variable names
  auto graphicsQueueFamilyProperties = std::find_if(queueFamilyProperties.begin(), queueFamilyProperties.end(),
    [](const auto& qfp) {
      return !!(qfp.queueFlags & vk::QueueFlagBits::eGraphics);
    });
  auto graphicsIndex = 
    static_cast<uint32_t>(std::distance(queueFamilyProperties.begin(), graphicsQueueFamilyProperties));
  */

  // The properties of each queue family available on the physical device
  std::vector<vk::QueueFamilyProperties> queueFamilyProperties = _physicalDevice.getQueueFamilyProperties();

  // Find the queue family that supports queues capable of Graphics AND Compute
  auto queueFamilyIndex = std::find_if(queueFamilyProperties.begin(), queueFamilyProperties.end(), [](const auto& qfp) {
      // See "Double Exclamation Marks" in PickPhysicalDevice for explanation of !!
      return !!(qfp.queueFlags & (vk::QueueFlagBits::eGraphics | vk::QueueFlagBits::eCompute));
    });

  // Check that the queue exists by seeing if the queueFamilyIndex value changed
  if (queueFamilyIndex == queueFamilyProperties.end()) 
    throw std::runtime_error("could not find a queue for graphics AND compute AND present!");
  
  // return the index of the queue with a graphics queue family
  return static_cast<uint32_t>(std::distance(queueFamilyProperties.begin(), queueFamilyIndex));
}

// A Device is an instance of a PhysicalDevice's Vulkan implementation with its own state and resources
void App::CreateLogicalDevice()
{
  // Find the index of the Graphics Compute queue family that we know exists on the physical device
  queueFamilyIndex = FindQueueFamilies(physicalDevice, surface);

  // A StructureChain populates its structs with the next in the chain It's a shorthand for writing .pNext = &nextStruct
  // in each struct. These features match the ones queried for in PickPhysicalDevice. We have checked for support, now
  // we enable.
  vk::StructureChain<vk::PhysicalDeviceFeatures2, 
                     vk::PhysicalDeviceVulkan12Features,
                     vk::PhysicalDeviceVulkan13Features, 
                     vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT,
                     vk::PhysicalDeviceAccelerationStructureFeaturesKHR, 
                     vk::PhysicalDeviceRayQueryFeaturesKHR>
  featureChain = {
    {.features = {.samplerAnisotropy = true } },                       // vk::PhysicalDeviceFeatures2
    { 
      .shaderSampledImageArrayNonUniformIndexing = true, 
      .descriptorBindingSampledImageUpdateAfterBind = true,
      .descriptorBindingPartiallyBound = true, 
      .descriptorBindingVariableDescriptorCount = true,
      .runtimeDescriptorArray = true, 
      .timelineSemaphore = true,
      .bufferDeviceAddress = true,
    },    // vk::PhysicalDeviceVulkan12Features
    {.synchronization2 = true, .dynamicRendering = true },             // vk::PhysicalDeviceVulkan13Features
    {.extendedDynamicState = true },                                   // vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT
    {.accelerationStructure = true },                                  // vk::PhysicalDeviceAccelerationStructureFeaturesKHR
    {.rayQuery = true },                                                // vk::PhysicalDeviceRayQueryFeaturesKHR
  };
  
  //=============================================== Devices and Queues ===============================================//

  // Useful for multi-queue operation (which we're not doing but we need memory initialised for such purpose)
  float queuePriority = 0.0f;

  // The logical Device must be created with information about which type and how many queues will be used by the app
  vk::DeviceQueueCreateInfo deviceQueueCreateInfo {
    .queueFamilyIndex = queueFamilyIndex,
    .queueCount = 1,
    .pQueuePriorities = &queuePriority
  };

  // collate all the information needed to create the Device
  vk::DeviceCreateInfo deviceCreateInfo {
    .pNext = &featureChain.get<vk::PhysicalDeviceFeatures2>(), // point to the first struct in the StructureChain
    .queueCreateInfoCount = 1,
    .pQueueCreateInfos = &deviceQueueCreateInfo,
    .enabledExtensionCount = static_cast<uint32_t>(requiredDeviceExtensions.size()),
    .ppEnabledExtensionNames = requiredDeviceExtensions.data()
  };

  // Create the Device, then create the Queue from the Device
  device = vk::raii::Device(physicalDevice, deviceCreateInfo);
  queue = vk::raii::Queue(device, queueFamilyIndex, 0);
  
  // Load the Device-related functions
  volkLoadDevice(static_cast<VkDevice>(*device));
}

/*================================== Helper Functions for Swap Chain Initialisation ==================================*/
// Choose B8G8R8A8_SRGB and SRGB Non Linear colour space if available, else fallback to the first available format
// BGRA seems to be preferred by older drivers and hardware, newer are ambivalent
vk::SurfaceFormatKHR chooseSwapSurfaceFormat(const std::vector<vk::SurfaceFormatKHR>& availableFormats)
{
  const auto formIter = std::find_if(availableFormats.begin(), availableFormats.end(), [](const auto& availableFormat) {
      return (availableFormat.format == vk::Format::eB8G8R8A8Srgb &&
        availableFormat.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear);
    });

  return formIter != availableFormats.end() ? *formIter : availableFormats[0];
}

// Prefer Mailbox present mode, fallback to Fifo (always supported). Both are Vsync present modes: Mailbox has a
// single-entry wait queue replacing entry with newest, Fifo can have multiple entries (consumes each in order).
vk::PresentModeKHR chooseSwapPresentMode(const std::vector<vk::PresentModeKHR>& availablePresentModes)
{
  // Check the surface is Mailbox-capable
  const auto presIter = std::find_if(availablePresentModes.begin(), availablePresentModes.end(),
    [](const auto& availablePresentMode) {
      return availablePresentMode == vk::PresentModeKHR::eMailbox;
    });

  // Prefer Mailbox, fallback Fifo
  return presIter != availablePresentModes.end() ? vk::PresentModeKHR::eMailbox : vk::PresentModeKHR::eFifo;
}

// Match the extent of the surface. If the surface is acting up, match the extent of the framebuffer
vk::Extent2D chooseSwapExtent(const vk::SurfaceCapabilitiesKHR& capabilities, GLFWwindow* const _pWindow)
{
  // So long as the surface's extent is valid, match it
  // This is an example of readability vs minor possible performance gain. If this code were running every single frame
  // I would likely move this return statement outside of this function as into a ternary statement using this condition
  // to decide between directly returning capabilities.currentExtent and calling this function at all. Having the choice
  // contained within a single function call with an appropriate name is far more readable, but always results in a
  // function call.
  if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max()) return capabilities.currentExtent;

  // Surface extent was not valid, get framebuffer extent
  int width, height; glfwGetFramebufferSize(_pWindow, &width, &height);

  // Match the framebuffer's extent as closely as the surface is capable
  return vk::Extent2D {
    std::clamp<uint32_t>(width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width),
    std::clamp<uint32_t>(height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height)
  };
}

// Get the minimum image count necessary (we assume 3, could be more, could be less)
uint32_t chooseSwapMinImageCount(const vk::SurfaceCapabilitiesKHR& capabilities)
{
  // There is a minimum count of images required for the swap chain to function
  uint32_t minImageCount = std::max(3u, capabilities.minImageCount);

  // Clamp to the maxImageCount so long as maxImageCount has a maximum and is < than minImageCount
  // Why don't we just use std::clamp? Because if the surface had no maxImageCount the result would always clamp to 0
  return (capabilities.maxImageCount > 0 && minImageCount > capabilities.maxImageCount) ?
    capabilities.maxImageCount : minImageCount;
}
/*============================================= END SWAP CHAIN HELPERS ===============================================*/

// The chain of framebuffers, alternated between which is being written to and which is being presented
void App::CreateSwapChain()
{
  // See what the surface created by GLFW is capable of
  auto surfaceCapabilities = physicalDevice.getSurfaceCapabilitiesKHR(surface);
  // Try to get the preferred format, or fallback
  auto surfaceFormat = chooseSwapSurfaceFormat(physicalDevice.getSurfaceFormatsKHR(surface));
  swapChainSurfaceFormat = surfaceFormat.format; // We will use this in other places
  // Try to get the preferred present mode, or fallback
  auto swapChainPresentMode = chooseSwapPresentMode(physicalDevice.getSurfacePresentModesKHR(surface));
  // Try to get the surface's extent, or the framebuffer's extent if surface extent is invalid
  swapChainExtent = chooseSwapExtent(surfaceCapabilities, pWindow);

  // Collate the swap chain information
  vk::SwapchainCreateInfoKHR swapChainCreateInfo {
    .flags = {},
    .surface = surface,
    .minImageCount = chooseSwapMinImageCount(surfaceCapabilities),
    .imageFormat = surfaceFormat.format,
    .imageColorSpace = surfaceFormat.colorSpace,
    .imageExtent = swapChainExtent, // like resolution (will likely match resolution)
    .imageArrayLayers = 1,
    .imageUsage = vk::ImageUsageFlagBits::eColorAttachment, // We're just writing colours
    .imageSharingMode = vk::SharingMode::eExclusive, // Only belongs to one queue
    .preTransform = surfaceCapabilities.currentTransform,
    .compositeAlpha = vk::CompositeAlphaFlagBitsKHR::eOpaque, // We're not doing transparent framebuffers
    .presentMode = swapChainPresentMode,
    .clipped = vk::True, // do we wait for V-Sync?
    .oldSwapchain = VK_NULL_HANDLE
  };

  swapChain = vk::raii::SwapchainKHR(device, swapChainCreateInfo, nullptr);
  swapChainImages = swapChain.getImages(); // Helper function that creates images for the swap chain
}

// The swap chain images are accessed just like any other images, they just serve a specific purpose
void App::CreateSwapChainImageViews()
{
  // The swap chain may be recreated at runtime, ensure the views are replaced
  swapChainImageViews.clear();

  // Create identical views, one for each image
  for (auto image : swapChainImages) {
    swapChainImageViews.emplace_back(CreateImageView(image, swapChainSurfaceFormat, vk::ImageAspectFlagBits::eColor, 1));
  }
}

// Create the depth buffer, shared between all framebuffers
// The depth buffer is not used during presentation - it can be safely accessed without affecting the colour results
void App::CreateDepthResources()
{
  // Get an available depth format (from a selection made within FindDepthFormat)
  vk::Format depthFormat = FindDepthFormat();
  
  // Create a depth image
  CreateImage(
    swapChainExtent.width, swapChainExtent.height, 1,
    depthFormat, vk::ImageTiling::eOptimal,
    vk::ImageUsageFlagBits::eDepthStencilAttachment,
    vk::MemoryPropertyFlagBits::eDeviceLocal,
    depthImage.first, depthImage.second
  );
  
  // Create the view
  depthImageView = CreateImageView(depthImage.first, depthFormat, vk::ImageAspectFlagBits::eDepth, 1);
}

// All command buffers are allocated from a pool
// We need to know from which queue family the pool is connected
void App::CreateCommandPool()
{
  // How will we use the command buffers from this pool, and which queue family will they be from
  vk::CommandPoolCreateInfo commandPoolInfo {
    .flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer, // required to allow command buffer resetting
    .queueFamilyIndex = queueFamilyIndex,
  };
  commandPool = vk::raii::CommandPool(device, commandPoolInfo);
}

/*========================================== Why multiple command buffers? ===========================================//
    1. We want to record both compute and graphics in the same frame
    2. Some commands in graphics rely on compute being completed before they operate. Once a command buffer has been
       submitted it cannot be reused that cycle (command buffer cannot be in pending state while recording)
    
    Multiple submits requires multiple command buffers.
*/

// Allocate the command buffers from the pool and device
void App::CreateCommandBuffers()
{
  commandBuffers.clear();

  vk::CommandBufferAllocateInfo allocInfo {
    .commandPool = commandPool, // the pool to allocate from
    .level = vk::CommandBufferLevel::ePrimary, // will be submitted directly to queue
    .commandBufferCount = MAX_FRAMES_IN_FLIGHT // how many buffers to allocate for
  };

  commandBuffers = vk::raii::CommandBuffers(device, allocInfo);
}

// Same as above
void App::CreateComputeCommandBuffers()
{
  computeCommandBuffers.clear();
  
  vk::CommandBufferAllocateInfo allocInfo {
    .commandPool = commandPool, // the pool to allocate from
    .level = vk::CommandBufferLevel::ePrimary, // will be submitted directly to queue
    .commandBufferCount = MAX_FRAMES_IN_FLIGHT, // how many buffers to allocate for
  };
  
  computeCommandBuffers = vk::raii::CommandBuffers(device, allocInfo);
}

// We need a way to synchronise queue operations (submit, present, dispatch)
// The CPU and GPU can work on things in parallel, but some are dependent on others and need to wait
void App::CreateSyncObjects()
{
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
  vk::SemaphoreTypeCreateInfo semaphoreType { .semaphoreType = vk::SemaphoreType::eTimeline, .initialValue = 0 };
  timelineSemaphore = vk::raii::Semaphore(device, { .pNext = &semaphoreType });
  
  // Fences for swapping between swap chain images
  inFlightFences.clear();
  for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) inFlightFences.emplace_back(device, vk::FenceCreateInfo{});
}

// Load the glTF data at path into class members
void App::LoadGLTF(const std::filesystem::path& path)
{
  // Parse the glTF file into a cgltf_asset, a usable container of all the glTF pointers to the companion bin 
  LoadAsset(path.generic_string().c_str()); // passing c_str as cgltf is a C library, not C++
  // Load asset textures. The paths of the textures will be relative to the asset's directory, so pass parent path.
  LoadTextures(path.parent_path());
  // Create a universal sampler (like a set of rules to follow if the image doesn't match up 1-to-1 with a surface)
  CreateTextureSampler();
  // Load vertex data, grouped by material
  LoadGeometry();
}

// Each pipeline needs to know what structures will be passed to the GPU during its lifetime. Not specific data, but
// just the expected layout of the data once it exists
void App::CreateDescriptorSetLayouts()
{
  // STANDARD 3D MODELS
  {
    // Descriptor bindings are like slots in descriptor sets
    std::array globalBindings = {
      // Binding for the Model View Projection matrix from the Camera, used exclusively by the vertex shader
      vk::DescriptorSetLayoutBinding(
        0,                                    // Binding
        vk::DescriptorType::eUniformBuffer,   // Descriptor Type
        1,                                    // Descriptors Count
        vk::ShaderStageFlagBits::eVertex | 
        vk::ShaderStageFlagBits::eFragment,   // Stage Flags
        nullptr                               // pImmutableSamplers
      ),
#ifdef REFERENCE
      vk::DescriptorSetLayoutBinding(
        1,                                    // Binding
        vk::DescriptorType::eAccelerationStructureKHR,   // Descriptor Type
        1,                                    // Descriptors Count
        vk::ShaderStageFlagBits::eFragment,   // Stage Flags
        nullptr                               // pImmutableSamplers
      ),
#endif
      vk::DescriptorSetLayoutBinding(
        2,                                    // Binding
        vk::DescriptorType::eStorageBuffer,   // Descriptor Type
        1,                                    // Descriptors Count
        vk::ShaderStageFlagBits::eFragment,   // Stage Flags
        nullptr                               // pImmutableSamplers
      ),
      vk::DescriptorSetLayoutBinding(
        3,                                    // Binding
        vk::DescriptorType::eStorageBuffer,   // Descriptor Type
        1,                                    // Descriptors Count
        vk::ShaderStageFlagBits::eFragment,   // Stage Flags
        nullptr                               // pImmutableSamplers
      ),
      // vk::DescriptorSetLayoutBinding(
      //   4,                                    // Binding
      //   vk::DescriptorType::eStorageBuffer,   // Descriptor Type
      //   1,                                    // Descriptors Count
      //   vk::ShaderStageFlagBits::eFragment,   // Stage Flags
      //   nullptr                               // pImmutableSamplers
      // ),
    };

    // Copy the bindings into the layout info
    vk::DescriptorSetLayoutCreateInfo globalLayoutInfo{
      .bindingCount = static_cast<uint32_t>(globalBindings.size()),
      .pBindings = globalBindings.data()
    };

    // Initialise
    descriptorSetLayoutGlobal = vk::raii::DescriptorSetLayout(device, globalLayoutInfo);
    
    uint32_t textureCount = static_cast<uint32_t>(textureImageViews.size());

    std::array materialBindings = {
      // Binding for a texture (colloquial), used exclusively by the fragment shader
      vk::DescriptorSetLayoutBinding(
        0,
        vk::DescriptorType::eSampler,
        1,
        vk::ShaderStageFlagBits::eFragment,
        nullptr
      ),
      vk::DescriptorSetLayoutBinding(
        1,
        vk::DescriptorType::eSampledImage,
        textureCount,
        vk::ShaderStageFlagBits::eFragment,
        nullptr
      ),
    };

    std::vector<vk::DescriptorBindingFlags> bindingFlags = {
      vk::DescriptorBindingFlagBits::eUpdateAfterBind,
      vk::DescriptorBindingFlagBits::ePartiallyBound | 
        vk::DescriptorBindingFlagBits::eVariableDescriptorCount | 
        vk::DescriptorBindingFlagBits::eUpdateAfterBind
    };

    vk::DescriptorSetLayoutBindingFlagsCreateInfo flagsCreateInfo {
      .bindingCount = static_cast<uint32_t>(bindingFlags.size()),
      .pBindingFlags = bindingFlags.data()
    };

    vk::DescriptorSetLayoutCreateInfo matLayoutInfo = {
      .pNext = &flagsCreateInfo,
      .flags = vk::DescriptorSetLayoutCreateFlagBits::eUpdateAfterBindPool,
      .bindingCount = static_cast<uint32_t>(materialBindings.size()),
      .pBindings = materialBindings.data()
    };

    descriptorSetLayoutMaterial = vk::raii::DescriptorSetLayout(device, matLayoutInfo);
  }

  // PATH TRACING
  {
    std::array radianceBindings = {
      // The bindings will actually use the same Image, but with different access types
      // Writing access in the compute stage
      vk::DescriptorSetLayoutBinding(
        0,
        vk::DescriptorType::eStorageImage,
        1,
        vk::ShaderStageFlagBits::eCompute,
        nullptr
      ),
      // Sampling access in the fragment stage
      vk::DescriptorSetLayoutBinding(
        1,
        vk::DescriptorType::eCombinedImageSampler,
        1,
        vk::ShaderStageFlagBits::eFragment,
        nullptr
      ),
    };

    // Copy the bindings to layout info
    vk::DescriptorSetLayoutCreateInfo radianceLayoutInfo{
      .bindingCount = static_cast<uint32_t>(radianceBindings.size()),
      .pBindings = radianceBindings.data()
    };

    // Initialise
    radianceCascadesOutput.descriptorSetLayout = vk::raii::DescriptorSetLayout(device, radianceLayoutInfo);
  }
}

// Initialise a Global Slang Session, allowing Slang to SPIR-V compilation in-app at runtime
// Shader compilation to SPIR-V creates a session from the global session
void App::InitSlang()
{
  // Create a default global session that following sessions will be created from
  if (!SLANG_SUCCEEDED(slang::createGlobalSession(globalSession.writeRef())))
    throw std::runtime_error("failed to create slang globalsession");

  // We manually call CompileShader for all shaders on start, ensuring SPIR-V exists by the time pipelines are created
  CompileShader(slang_path, spirv_path);
  CompileShader(compute_slang_path, compute_spirv_path);
}

// Purely for readability, call all the CreateXPipeline functions
void App::CreatePipelines()
{
  CreateGraphicsPipeline();
  CreateComputeGraphicsPipeline();
  CreateComputePipeline();
}

/*============================================== HOW DOES MEMORY WORK? ===============================================//
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
    order, but the page numbers let you know the order in which they logically exist. The page number is its virtual
    address, and its physical order is its physical address.
    Mapping is like including the pages of another book in a way that maintains the logical flow of the page
    numbers. This saves you from going over to the other book each time you want to see those pages.
    So if the host has mapped the DEVICE_LOCAL|HOST_VISIBLE|HOST_COHERENT memory it is able to update that memory
    with the CPU and it will change on the GPU.
*/
// Create and Map the Uniform Buffers that will be passed to shaders
// We create as many of each uniform buffer as there are frames in flight, as data may change between calls while the
// data is still being read for the previous frame
void App::CreateUniformBuffers()
{
  mvpBuffers.clear();
  mvpBuffersMapped.clear();
  
  // uniformBuffers.clear();
  // uniformBuffersMapped.clear();

  // General breakdown:
  // 1. Create a buffer with sizeof(UserStruct) in DeviceMemory with accessor userStructBuffer
  // 2. Persist the created objects as class members
  // 3. Map the memory and store as a void*
  for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
    vk::DeviceSize mvpBufferSize = sizeof(MVP);
    vk::raii::Buffer mvpBuffer({});
    vk::raii::DeviceMemory mvpBufferMemory({});
    // Create the buffer in DEVICE_LOCAL memory
    CreateBuffer(
      mvpBufferSize,
      vk::BufferUsageFlagBits::eUniformBuffer,
      // We will be continuously updating this data, make sure it is easy to access for the host as possible
      vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent,
      mvpBuffer, mvpBufferMemory
    );
    // Persist the objects by binding their lifetime to a class member
    mvpBuffers.emplace_back(std::pair(std::move(mvpBuffer), std::move(mvpBufferMemory)));
    // Map the DEVICE_LOCAL memory
    mvpBuffersMapped.emplace_back(mvpBuffers.back().second.mapMemory(0, mvpBufferSize));
  }
}

// For Path-Tracing Reference, create a read-write Image of the same resolution as the initial framebuffers
void App::CreatePathTracingTexture()
{
  // Create the Image on the GPU
  CreateImage(
    WIDTH, HEIGHT, 1,
    vk::Format::eR16G16B16A16Sfloat, vk::ImageTiling::eOptimal,
    vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eSampled,
    vk::MemoryPropertyFlagBits::eDeviceLocal,
    pathTracingTexture.first, pathTracingTexture.second
  );

  // We want the image in the General layout for read-write operations
  TransitionImageLayout(pathTracingTexture.first, vk::ImageLayout::eUndefined, vk::ImageLayout::eGeneral, 1);
  
  // Create a view for the Image
  pathTracingTextureView = CreateImageView(
    pathTracingTexture.first,
    vk::Format::eR16G16B16A16Sfloat,
    vk::ImageAspectFlagBits::eColor,
    1
  );
}

// Create the vertex buffers for the scene AND the fullscreen triangle (for postprocessing effects)
void App::CreateVertexBuffers()
{
  vertexBuffer = CreateVertexBuffer(vertices);

  // These coordinates will always create a triangle that completely covers the screen.
  // Double the width and double the height of the viewport
  const std::vector<Vertex> triangle = {
    {{-1.0f, -1.0f, -0.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f}},
    {{-1.0f, 3.0f, -0.0f}, {1.0f, 1.0f, 1.0f}, {0.0f, 2.0f}},
    {{3.0f, -1.0f, -0.0f}, {0.0f, 1.0f, 0.0f}, {2.0f, 0.0f}},
  };
  triangleBuffer = CreateVertexBuffer(triangle);
}

// We just need the indices stored on the GPU. We will instruct the GPU on how to interpret the data later.
void App::CreateIndexBuffers()
{
  // SCENE INDEX BUFFER
  {
    // The exact same as CreateVertexBuffers, but the second buffer has IndexBuffer usage
    // indices are all the same type
    vk::DeviceSize bufferSize = sizeof(indices[0]) * indices.size();
    vk::raii::Buffer stagingBuffer({});
    vk::raii::DeviceMemory stagingBufferMemory({});

    // We create a CPU-editable buffer, insert the data, then copy that buffer into one that does not require CPU access
    // Notice the buffer usage flag TransferSrc. This lets the GPU know we will be copying this buffer at some point
    CreateBuffer(
      bufferSize,
      vk::BufferUsageFlagBits::eTransferSrc,
      vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent,
      stagingBuffer, stagingBufferMemory
    );

    // Map and copy our loaded vertices data to the GPU host-visible memory
    void* dataStaging = stagingBufferMemory.mapMemory(0, bufferSize);
    memcpy(dataStaging, indices.data(), (size_t)bufferSize);
    // We don't need to access this buffer anymore, unmap
    stagingBufferMemory.unmapMemory();

    // Create an Index Buffer in DEVICE_LOCAL memory not necessarily visible to host
    // Notice the buffer usage flag TransferDst. We will be copying to this buffer.
    CreateBuffer(
      bufferSize,
      vk::BufferUsageFlagBits::eIndexBuffer | 
      vk::BufferUsageFlagBits::eTransferDst | 
      vk::BufferUsageFlagBits::eShaderDeviceAddress |
#ifdef REFERENCE
      vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR |
#endif
      vk::BufferUsageFlagBits::eStorageBuffer,
      vk::MemoryPropertyFlagBits::eDeviceLocal,
      indexBuffer.first, indexBuffer.second
    );

    // Copy the host-visible buffer to the non-host-visible buffer
    CopyBuffer(stagingBuffer, indexBuffer.first, bufferSize);
  }
}

void App::CreateUVBuffer()
{
  // Extract the texCoords
  std::vector<glm::vec2> uvs;
  uvs.reserve(vertices.size());
  for (auto& v : vertices) uvs.push_back(v.texCoord);
  
  vk::DeviceSize bufferSize = sizeof(uvs[0]) * uvs.size();

  vk::raii::Buffer stagingBuffer({});
  vk::raii::DeviceMemory stagingBufferMemory({});

  // We create a CPU-editable buffer, insert the data, then copy that buffer into one that does not require CPU access
  // Notice the buffer usage flag TransferSrc. This lets the GPU know we will be copying this buffer at some point
  CreateBuffer(
    bufferSize,
    vk::BufferUsageFlagBits::eTransferSrc,
    vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent,
    stagingBuffer, stagingBufferMemory
  );

  // Map and copy our loaded vertices data to the GPU host-visible memory
  void* dataStaging = stagingBufferMemory.mapMemory(0, bufferSize);
  memcpy(dataStaging, uvs.data(), (size_t)bufferSize);
  // We don't need to access this buffer anymore, unmap
  stagingBufferMemory.unmapMemory();

  // Create an Index Buffer in DEVICE_LOCAL memory not necessarily visible to host
  // Notice the buffer usage flag TransferDst. We will be copying to this buffer.
  CreateBuffer(
    bufferSize,
    vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst,
    vk::MemoryPropertyFlagBits::eDeviceLocal,
    uvBuffer.first, uvBuffer.second
  );

  // Copy the host-visible buffer to the non-host-visible buffer
  CopyBuffer(stagingBuffer, uvBuffer.first, bufferSize);
}

void App::CreateAccelerationStructures()
{
  vk::BufferDeviceAddressInfo vertAddrInfo{ .buffer = *vertexBuffer.first };
  vk::DeviceAddress vertAddr = device.getBufferAddressKHR(vertAddrInfo);
  vk::BufferDeviceAddressInfo idxAddrInfo{ .buffer = *indexBuffer.first };
  vk::DeviceAddress idxAddr = device.getBufferAddressKHR(idxAddrInfo);

  instances.reserve(submeshes.size());
  blasBuffers.reserve(submeshes.size());
  blasHandles.reserve(submeshes.size());
  
  vk::TransformMatrixKHR identity{};
  identity.matrix = std::array<std::array<float, 4>, 3>{ {
      std::array<float, 4> {1.0f, 0.0f, 0.0f, 0.0f},
      std::array<float, 4> {0.0f, 1.0f, 0.0f, 0.0f},
      std::array<float, 4> {0.0f, 0.0f, 1.0f, 0.0f},
  } };

  for (size_t i = 0; i < submeshes.size(); i++) {
    auto& submesh = submeshes[i];

    // Prepare the geometry data
    auto trianglesData = vk::AccelerationStructureGeometryTrianglesDataKHR{
      .vertexFormat = vk::Format::eR32G32B32Sfloat,
      .vertexData = vertAddr,
      .vertexStride = sizeof(Vertex),
      .maxVertex = submesh.maxVertex,
      .indexType = vk::IndexType::eUint32,
      .indexData = idxAddr + submesh.indexOffset * sizeof(uint32_t)
    };

    vk::AccelerationStructureGeometryDataKHR geometryData(trianglesData);

    vk::AccelerationStructureGeometryKHR blasGeometry{
      .geometryType = vk::GeometryTypeKHR::eTriangles,
      .geometry = geometryData,
      .flags = vk::GeometryFlagBitsKHR::eOpaque
    };

    vk::AccelerationStructureBuildGeometryInfoKHR blasBuildGeometryInfo{
      .type = vk::AccelerationStructureTypeKHR::eBottomLevel,
      .mode = vk::BuildAccelerationStructureModeKHR::eBuild,
      .geometryCount = 1,
      .pGeometries = &blasGeometry
    };

    auto primitiveCount = static_cast<uint32_t>(submesh.indexCount / 3);

    vk::AccelerationStructureBuildSizesInfoKHR blasBuildSizes =
      device.getAccelerationStructureBuildSizesKHR(
        vk::AccelerationStructureBuildTypeKHR::eDevice, 
        blasBuildGeometryInfo, 
        { primitiveCount }
      );

    // Create a scratch buffer for the BLAS, this will hold temporary data
    // during the build process
    std::pair<vk::raii::Buffer, vk::raii::DeviceMemory> scratchBuffer = std::pair(nullptr, nullptr);
    CreateBuffer(
      blasBuildSizes.buildScratchSize,
      vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eShaderDeviceAddress,
      vk::MemoryPropertyFlagBits::eDeviceLocal,
      scratchBuffer.first, scratchBuffer.second
    );

    // Save the scratch buffer address in the build info structure
    vk::BufferDeviceAddressInfo scratchAddressInfo{ .buffer = *scratchBuffer.first };
    vk::DeviceAddress scratchAddr = device.getBufferAddressKHR(scratchAddressInfo);
    blasBuildGeometryInfo.scratchData.deviceAddress = scratchAddr;

    // Create a buffer for the BLAS itself now that we now the required size
    std::pair<vk::raii::Buffer, vk::raii::DeviceMemory> blasBuffer = std::pair(nullptr, nullptr);
    blasBuffers.emplace_back(std::move(blasBuffer));
    CreateBuffer(
      blasBuildSizes.accelerationStructureSize,
      vk::BufferUsageFlagBits::eAccelerationStructureStorageKHR |
      vk::BufferUsageFlagBits::eShaderDeviceAddress |
      vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR,
      vk::MemoryPropertyFlagBits::eDeviceLocal,
      blasBuffers[i].first, blasBuffers[i].second
    );

    // Create and store the BLAS handle
    vk::AccelerationStructureCreateInfoKHR blasCreateInfo{
        .buffer = blasBuffers[i].first,
        .offset = 0,
        .size = blasBuildSizes.accelerationStructureSize,
        .type = vk::AccelerationStructureTypeKHR::eBottomLevel,
    };

    blasHandles.emplace_back(device.createAccelerationStructureKHR(blasCreateInfo));

    // Save the BLAS handle in the build info structure
    blasBuildGeometryInfo.dstAccelerationStructure = blasHandles[i];

    // Prepare the build range for the BLAS
    vk::AccelerationStructureBuildRangeInfoKHR blasRangeInfo{
        .primitiveCount = primitiveCount,
        .primitiveOffset = 0,
        .firstVertex = submesh.firstVertex,
        .transformOffset = 0
    };

    // Build the BLAS
    auto cmd = BeginSingleTimeCommands();
    cmd.buildAccelerationStructuresKHR({ blasBuildGeometryInfo }, { &blasRangeInfo });
    EndSingleTimeCommands(cmd);

    vk::AccelerationStructureDeviceAddressInfoKHR addrInfo {
        .accelerationStructure = *blasHandles[i]
    };
    vk::DeviceAddress blasDeviceAddr = device.getAccelerationStructureAddressKHR(addrInfo);

    vk::AccelerationStructureInstanceKHR instance {
        .transform = identity,
        .mask = 0xFF,
        .accelerationStructureReference = blasDeviceAddr
    };

    instances.push_back(instance);
  }

  vk::DeviceSize instBufferSize = sizeof(instances[0]) * instances.size();
  CreateBuffer(
    instBufferSize,
    vk::BufferUsageFlagBits::eShaderDeviceAddress |
    vk::BufferUsageFlagBits::eTransferDst |
    vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR,
    vk::MemoryPropertyFlagBits::eHostVisible |
    vk::MemoryPropertyFlagBits::eHostCoherent,
    instanceBuffer.first, instanceBuffer.second
  );

  void* ptr = instanceBuffer.second.mapMemory(0, instBufferSize);
  memcpy(ptr, instances.data(), instBufferSize);
  instanceBuffer.second.unmapMemory();

  vk::BufferDeviceAddressInfo instanceAddrInfo{ .buffer = instanceBuffer.first };
  vk::DeviceAddress instanceAddr = device.getBufferAddressKHR(instanceAddrInfo);

  // Prepare the geometry (instance) data
  auto instancesData = vk::AccelerationStructureGeometryInstancesDataKHR{
      .arrayOfPointers = vk::False,
      .data = instanceAddr
  };

  vk::AccelerationStructureGeometryDataKHR geometryData(instancesData);

  vk::AccelerationStructureGeometryKHR tlasGeometry{
      .geometryType = vk::GeometryTypeKHR::eInstances,
      .geometry = geometryData
  };

  vk::AccelerationStructureBuildGeometryInfoKHR tlasBuildGeometryInfo{
      .type = vk::AccelerationStructureTypeKHR::eTopLevel,
      .mode = vk::BuildAccelerationStructureModeKHR::eBuild,
      .geometryCount = 1,
      .pGeometries = &tlasGeometry
  };

  // Query the memory sizes that will be needed for this TLAS
  auto primitiveCount = static_cast<uint32_t>(instances.size());

  vk::AccelerationStructureBuildSizesInfoKHR tlasBuildSizes =
    device.getAccelerationStructureBuildSizesKHR(
      vk::AccelerationStructureBuildTypeKHR::eDevice,
      tlasBuildGeometryInfo,
      { primitiveCount }
    );

  // Create a scratch buffer for the TLAS, this will hold temporary data
  // during the build process
  CreateBuffer(
    tlasBuildSizes.buildScratchSize,
    vk::BufferUsageFlagBits::eStorageBuffer |
    vk::BufferUsageFlagBits::eShaderDeviceAddress,
    vk::MemoryPropertyFlagBits::eDeviceLocal,
    tlasScratchBuffer.first, tlasScratchBuffer.second
  );

  // Save the scratch buffer address in the build info structure
  vk::BufferDeviceAddressInfo scratchAddressInfo{ .buffer = *tlasScratchBuffer.first };
  vk::DeviceAddress scratchAddr = device.getBufferAddressKHR(scratchAddressInfo);
  tlasBuildGeometryInfo.scratchData.deviceAddress = scratchAddr;

  // Create a buffer for the TLAS itself now that we now the required size
  CreateBuffer(
    tlasBuildSizes.accelerationStructureSize,
    vk::BufferUsageFlagBits::eAccelerationStructureStorageKHR |
    vk::BufferUsageFlagBits::eShaderDeviceAddress |
    vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR,
    vk::MemoryPropertyFlagBits::eDeviceLocal,
    tlasBuffer.first, tlasBuffer.second
  );

  // Create and store the TLAS handle
  vk::AccelerationStructureCreateInfoKHR tlasCreateInfo{
      .buffer = tlasBuffer.first,
      .offset = 0,
      .size = tlasBuildSizes.accelerationStructureSize,
      .type = vk::AccelerationStructureTypeKHR::eTopLevel,
  };

  tlas = device.createAccelerationStructureKHR(tlasCreateInfo);

  // Save the TLAS handle in the build info structure
  tlasBuildGeometryInfo.dstAccelerationStructure = tlas;

  // Prepare the build range for the TLAS
  vk::AccelerationStructureBuildRangeInfoKHR tlasRangeInfo{
      .primitiveCount = primitiveCount,
      .primitiveOffset = 0,
      .firstVertex = 0,
      .transformOffset = 0
  };

  // Build the TLAS
  auto cmd = BeginSingleTimeCommands();
  cmd.buildAccelerationStructuresKHR({ tlasBuildGeometryInfo }, { &tlasRangeInfo });
  EndSingleTimeCommands(cmd);
}

void App::CreateInstanceLUTBuffer()
{
  vk::DeviceSize bufferSize = sizeof(InstanceLUT) * instanceLUTs.size();

  vk::raii::Buffer stagingBuffer({});
  vk::raii::DeviceMemory stagingBufferMemory({});

  // We create a CPU-editable buffer, insert the data, then copy that buffer into one that does not require CPU access
  // Notice the buffer usage flag TransferSrc. This lets the GPU know we will be copying this buffer at some point
  CreateBuffer(
    bufferSize,
    vk::BufferUsageFlagBits::eTransferSrc,
    vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent,
    stagingBuffer, stagingBufferMemory
  );

  // Map and copy our loaded vertices data to the GPU host-visible memory
  void* dataStaging = stagingBufferMemory.mapMemory(0, bufferSize);
  memcpy(dataStaging, instanceLUTs.data(), (size_t)bufferSize);
  // We don't need to access this buffer anymore, unmap
  stagingBufferMemory.unmapMemory();

  // Create an Index Buffer in DEVICE_LOCAL memory not necessarily visible to host
  // Notice the buffer usage flag TransferDst. We will be copying to this buffer.
  CreateBuffer(
    bufferSize,
    vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst,
    vk::MemoryPropertyFlagBits::eDeviceLocal,
    instanceLUTBuffer.first, instanceLUTBuffer.second
  );

  // Copy the host-visible buffer to the non-host-visible buffer
  CopyBuffer(stagingBuffer, instanceLUTBuffer.first, bufferSize);
}

// Create DescriptorPools that can allocate DescriptorSets. It's like a check making sure not too many descriptors of
// some type are allocated, as it does not take layouts into account
void App::CreateDescriptorPools()
{
  // It's possible that a driver allows overallocation from pool, avoiding VK_ERROR_OUT_OF_POOL_MEMORY when allocating
  // for more descriptor sets than the descriptor pool sizes "allow". In these cases it may not seem like the
  // descriptorCount member has any effect, but it will for some other drivers.
  // STANDARD 3D MODELS
  {
    // We need at least 1 Uniform Buffer and 1 CombinedImageSampler per material group per frame in flight
    std::array graphicsPoolSizes = {
      vk::DescriptorPoolSize(vk::DescriptorType::eUniformBuffer, MAX_FRAMES_IN_FLIGHT),
#ifdef REFERENCE
      vk::DescriptorPoolSize(vk::DescriptorType::eAccelerationStructureKHR, MAX_FRAMES_IN_FLIGHT),
#endif
      vk::DescriptorPoolSize(vk::DescriptorType::eStorageBuffer, MAX_FRAMES_IN_FLIGHT),
      vk::DescriptorPoolSize(vk::DescriptorType::eSampler, MAX_FRAMES_IN_FLIGHT),
      vk::DescriptorPoolSize(vk::DescriptorType::eSampledImage, static_cast<uint32_t>(mats.size())),
    };

    vk::DescriptorPoolCreateInfo graphicsPoolInfo{
      // DescriptorSets can be individually freed
      .flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet | 
                 vk::DescriptorPoolCreateFlagBits::eUpdateAfterBind,
      // We need at least one descriptor set per material group per frame in flight
      .maxSets = MAX_FRAMES_IN_FLIGHT + 1,
      // Attach the DescriptorPoolSizes
      .poolSizeCount = static_cast<uint32_t>(graphicsPoolSizes.size()),
      .pPoolSizes = graphicsPoolSizes.data()
    };

    // Initialise pool
    graphicsDescriptorPool = vk::raii::DescriptorPool(device, graphicsPoolInfo);
  }
  // PATH TRACING
  {
    // We need at least 1 Uniform Buffer, 1 StorageImage and 1 CombinedImageSampler per frame in flight
    std::array computePoolSizes = {
      vk::DescriptorPoolSize(vk::DescriptorType::eStorageImage, MAX_FRAMES_IN_FLIGHT),
      vk::DescriptorPoolSize(vk::DescriptorType::eCombinedImageSampler, MAX_FRAMES_IN_FLIGHT),
    };

    vk::DescriptorPoolCreateInfo computePoolInfo {
      // DescriptorSets can be individually freed
      .flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
      // We need at least one descriptor set per frame in flight
      .maxSets = MAX_FRAMES_IN_FLIGHT,
      // Attach the DescriptorPoolSizes
      .poolSizeCount = static_cast<uint32_t>(computePoolSizes.size()),
      .pPoolSizes = computePoolSizes.data()
    };

    // Initialise pool
    radianceCascadesOutput.descriptorPool = vk::raii::DescriptorPool(device, computePoolInfo);
  }
#ifdef _DEBUG
  // IMGUI DEBUG PANELS
  {
    // It's recommended to have large pool counts (but it may be too many for devices, keep it low until there's an
    // issue
    std::array imguiPoolSizes = {
      vk::DescriptorPoolSize(vk::DescriptorType::eSampler, MAX_FRAMES_IN_FLIGHT),
      vk::DescriptorPoolSize(vk::DescriptorType::eCombinedImageSampler, MAX_FRAMES_IN_FLIGHT),
      vk::DescriptorPoolSize(vk::DescriptorType::eSampledImage, MAX_FRAMES_IN_FLIGHT),
      vk::DescriptorPoolSize(vk::DescriptorType::eStorageImage, MAX_FRAMES_IN_FLIGHT),
      vk::DescriptorPoolSize(vk::DescriptorType::eUniformTexelBuffer, MAX_FRAMES_IN_FLIGHT),
      vk::DescriptorPoolSize(vk::DescriptorType::eStorageTexelBuffer, MAX_FRAMES_IN_FLIGHT),
      vk::DescriptorPoolSize(vk::DescriptorType::eUniformBuffer, MAX_FRAMES_IN_FLIGHT),
      vk::DescriptorPoolSize(vk::DescriptorType::eStorageBuffer, MAX_FRAMES_IN_FLIGHT),
      vk::DescriptorPoolSize(vk::DescriptorType::eUniformBufferDynamic, MAX_FRAMES_IN_FLIGHT),
      vk::DescriptorPoolSize(vk::DescriptorType::eStorageBufferDynamic, MAX_FRAMES_IN_FLIGHT),
      vk::DescriptorPoolSize(vk::DescriptorType::eInputAttachment, MAX_FRAMES_IN_FLIGHT)
    };

    vk::DescriptorPoolCreateInfo imguiPoolInfo {
      // DescriptorSets can be individually freed
      .flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
      // Change if allocate error
      .maxSets = 1,
      // Attach the DescriptorPoolSizes
      .poolSizeCount = static_cast<uint32_t>(imguiPoolSizes.size()),
      .pPoolSizes = imguiPoolSizes.data()
    };

    // Initialise pool
    imguiDescriptorPool = vk::raii::DescriptorPool(device, imguiPoolInfo);
  }
#endif
}

// Collation of Descriptors for shaders
void App::CreateDescriptorSets()
{
  // PATH TRACING (Vertex and Fragment Stages)
  {
    // MAX_FRAMES_IN_FLIGHT copies of DescriptorSetLayout
    std::vector<vk::DescriptorSetLayout> layouts(MAX_FRAMES_IN_FLIGHT, *radianceCascadesOutput.descriptorSetLayout);

    // Collate the relevant info (DescriptorPool and DescriptorSetLayouts)
    vk::DescriptorSetAllocateInfo allocInfo {
      .descriptorPool = static_cast<vk::DescriptorPool>(radianceCascadesOutput.descriptorPool),
      .descriptorSetCount = static_cast<uint32_t>(layouts.size()),
      .pSetLayouts = layouts.data(),
    };
    
    radianceCascadesOutput.descriptorSets.clear();
    radianceCascadesOutput.descriptorSets = device.allocateDescriptorSets(allocInfo);

    // Fill in the descriptor sets
    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
      // Writable interface for image
      vk::DescriptorImageInfo imageInfo {
        .imageView = static_cast<vk::ImageView>(pathTracingTextureView),
        .imageLayout = vk::ImageLayout::eGeneral,
      };
      // Samplable interface for image
      vk::DescriptorImageInfo samplerInfo {
        .sampler = textureSampler,
        .imageView = static_cast<vk::ImageView>(pathTracingTextureView),
        .imageLayout = vk::ImageLayout::eGeneral,
      };

      // Link descriptor set, binding and resource together
      std::array descriptorWrites = {
        // Image as StorageImage
        vk::WriteDescriptorSet{
          .dstSet = static_cast<vk::DescriptorSet>(radianceCascadesOutput.descriptorSets[i]),
          .dstBinding = 0, .dstArrayElement = 0, .descriptorCount = 1,
          .descriptorType = vk::DescriptorType::eStorageImage, .pImageInfo = &imageInfo
        },
        // Image as CombinedImageSampler
        vk::WriteDescriptorSet{
          .dstSet = static_cast<vk::DescriptorSet>(radianceCascadesOutput.descriptorSets[i]),
          .dstBinding = 1, .dstArrayElement = 0, .descriptorCount = 1,
          .descriptorType = vk::DescriptorType::eCombinedImageSampler, .pImageInfo = &samplerInfo
        },
      };
      // Write the descriptor sets to the GPU
      device.updateDescriptorSets(descriptorWrites, {});
    }
  }
  // STANDARD 3D MODELS
  {
    // MAX_FRAMES_IN_FLIGHT copies of DescriptorSetLayout
    std::vector<vk::DescriptorSetLayout> globalLayouts(MAX_FRAMES_IN_FLIGHT, *descriptorSetLayoutGlobal);

    // Collate the relevant info (DescriptorPool and DescriptorSetLayouts)
    vk::DescriptorSetAllocateInfo globalAllocInfo{
      .descriptorPool = static_cast<vk::DescriptorPool>(graphicsDescriptorPool),
      .descriptorSetCount = static_cast<uint32_t>(globalLayouts.size()),
      .pSetLayouts = globalLayouts.data(),
    };

    globalDescriptorSets.clear();
    globalDescriptorSets = device.allocateDescriptorSets(globalAllocInfo);

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
      // MVP Buffer
      vk::DescriptorBufferInfo bufferInfo{
        .buffer = *mvpBuffers[i].first,
        .offset = 0,
        .range = sizeof(MVP)
      };

      vk::WriteDescriptorSet bufferWrite {
        .dstSet = globalDescriptorSets[i],
        .dstBinding = 0,
        .dstArrayElement = 0,
        .descriptorCount = 1,
        .descriptorType = vk::DescriptorType::eUniformBuffer,
        .pBufferInfo = &bufferInfo
      };

#ifdef REFERENCE
      vk::WriteDescriptorSetAccelerationStructureKHR asInfo {
        .accelerationStructureCount = 1,
        .pAccelerationStructures = &*tlas
      };

      vk::WriteDescriptorSet asWrite {
        .pNext = &asInfo,
        .dstSet = globalDescriptorSets[i],
        .dstBinding = 1,
        .dstArrayElement = 0, 
        .descriptorCount = 1,
        .descriptorType = vk::DescriptorType::eAccelerationStructureKHR
      };
#endif

      vk::DescriptorBufferInfo indexBufferInfo {
        .buffer = *indexBuffer.first,
        .offset = 0,
        .range = sizeof(uint32_t) * indices.size()
      };

      vk::WriteDescriptorSet indexBufferWrite {
        .dstSet = globalDescriptorSets[i],
        .dstBinding = 2,
        .dstArrayElement = 0,
        .descriptorCount = 1,
        .descriptorType = vk::DescriptorType::eStorageBuffer,
        .pBufferInfo = &indexBufferInfo
      };

      vk::DescriptorBufferInfo uvBufferInfo {
        .buffer = *uvBuffer.first,
        .offset = 0,
        .range = sizeof(glm::vec2) * vertices.size()
      };

      vk::WriteDescriptorSet uvBufferWrite {
        .dstSet = globalDescriptorSets[i],
        .dstBinding = 3,
        .dstArrayElement = 0,
        .descriptorCount = 1,
        .descriptorType = vk::DescriptorType::eStorageBuffer,
        .pBufferInfo = &uvBufferInfo
      };

      // vk::DescriptorBufferInfo instanceLUTBufferInfo {
      //   .buffer = *instanceLUTBuffer.first,
      //   .offset = 0,
      //   .range = sizeof(InstanceLUT) * instanceLUTs.size()
      // };

      // vk::WriteDescriptorSet instanceLUTBufferWrite {
      //   .dstSet = globalDescriptorSets[i],
      //   .dstBinding = 4,
      //   .dstArrayElement = 0,
      //   .descriptorCount = 1,
      //   .descriptorType = vk::DescriptorType::eStorageBuffer,
      //   .pBufferInfo = &instanceLUTBufferInfo
      // };

      // std::array<vk::WriteDescriptorSet, 5> descriptorWrites = 
      //   { bufferWrite, asWrite, indexBufferWrite, uvBufferWrite, instanceLUTBufferWrite };

      std::array descriptorWrites = {
        bufferWrite, 
#ifdef REFERENCE
        asWrite,
#endif 
        indexBufferWrite, 
        uvBufferWrite 
      };


      // Write the descriptor sets to the GPU
      device.updateDescriptorSets(descriptorWrites, {});
    }

    std::vector<uint32_t> variableCounts = { static_cast<uint32_t>(textureImageViews.size()) };
    vk::DescriptorSetVariableDescriptorCountAllocateInfo variableCountInfo = {
      .descriptorSetCount = 1,
      .pDescriptorCounts = variableCounts.data()
    };

    std::vector<vk::DescriptorSetLayout> layouts { *descriptorSetLayoutMaterial };

    vk::DescriptorSetAllocateInfo matAllocInfo {
      .pNext = &variableCountInfo,
      .descriptorPool = *graphicsDescriptorPool,
      .descriptorSetCount = static_cast<uint32_t>(layouts.size()),
      .pSetLayouts = layouts.data(),
    };

    materialDescriptorSets = device.allocateDescriptorSets(matAllocInfo);

    vk::DescriptorImageInfo samplerInfo {
      .sampler = textureSampler
    };

    vk::WriteDescriptorSet samplerWrite {
      .dstSet = materialDescriptorSets[0],
      .dstBinding = 0,
      .dstArrayElement = 0,
      .descriptorCount = 1,
      .descriptorType = vk::DescriptorType::eSampler,
      .pImageInfo = &samplerInfo
    };

    device.updateDescriptorSets({samplerWrite}, {});

    std::vector<vk::DescriptorImageInfo> imageInfos;
    imageInfos.reserve(textureImageViews.size());
    for (auto& imageView: textureImageViews) {
      vk::DescriptorImageInfo imageInfo {
        .imageView = imageView,
        .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal
      };
      imageInfos.push_back(imageInfo);
    }

    vk::WriteDescriptorSet materialWrite {
      .dstSet = materialDescriptorSets[0],
      .dstBinding = 1,
      .dstArrayElement = 0,
      .descriptorCount = static_cast<uint32_t>(imageInfos.size()),
      .descriptorType = vk::DescriptorType::eSampledImage,
      .pImageInfo = imageInfos.data()
    };

    device.updateDescriptorSets({materialWrite}, {});
  }
}

// High-level Vulkan frame logic
void App::DrawFrame()
{
  // Try to acquire the next swap chain image.
  auto [result, imageIndex] = swapChain.acquireNextImage(
    UINT64_MAX,                         // Timeout
    nullptr,                            // Semaphore to signal
    *inFlightFences[currentFrame]       // Fence to signal
  );
  
  // Wait until next image is acquired
  while (vk::Result::eTimeout == device.waitForFences(*inFlightFences[currentFrame], vk::True, UINT64_MAX))
    ;

  // unsignal this fence, ready to be signalled again
  device.resetFences(*inFlightFences[currentFrame]);

  // Check if the swap chain needs to be recreated or if some other error occured
  if (result == vk::Result::eErrorOutOfDateKHR || framebufferResized) {
    framebufferResized = false; RecreateSwapChain();
  } else if (result != vk::Result::eSuccess && result != vk::Result::eSuboptimalKHR)
    throw std::runtime_error("failed to acquire swap chain image!");

  // Update uniform buffers
  UpdateModelViewProjection(currentFrame);
  
  // A command buffer needs to be in the initial state to record. Resetting the command pool resets all of the buffers
  // allocated in that pool. Command buffers can also be reset individually (begin() has an implicit reset if the buffer
  // is not in the initial state).
  commandPool.reset();

  //// Update timeline value for this frame
  uint64_t computeWaitValue = timelineValue, computeSignalValue = ++timelineValue;
  uint64_t graphicsWaitValue = computeSignalValue, graphicsSignalValue = ++timelineValue;
  
  // COMPUTE
  {
    RecordComputeCommandBuffer();

    // Submission will wait for computeWaitValue
    // Submission will signal to computeSignalValue upon completion of queue.submit()
    vk::TimelineSemaphoreSubmitInfo computeTimelineInfo {
      .waitSemaphoreValueCount = 1, .pWaitSemaphoreValues = &computeWaitValue,
      .signalSemaphoreValueCount = 1, .pSignalSemaphoreValues = &computeSignalValue
    };

    // Pipeline stage to wait at
    vk::PipelineStageFlags waitStages[] = { vk::PipelineStageFlagBits::eComputeShader };

    vk::SubmitInfo computeSubmitInfo {
      .pNext = &computeTimelineInfo, // the wait and signal values for the timelineSemaphore
      .waitSemaphoreCount = 1, .pWaitSemaphores = &*timelineSemaphore,
      .pWaitDstStageMask = waitStages,  // the shader stages to wait on
      .commandBufferCount = 1, .pCommandBuffers = &*computeCommandBuffers[currentFrame], // the command buffer to submit
      .signalSemaphoreCount = 1, .pSignalSemaphores = &*timelineSemaphore
    };

    // submit the recorded command buffer to the GPU so it can start working
    queue.submit(computeSubmitInfo, nullptr);
  }
  
  // GRAPHICS
  {
    RecordCommandBuffer(imageIndex);

    // Submission will wait for graphicsWaitValue
    // Submission will signal to graphicsSignalValue upon completion of queue.submit()
    vk::TimelineSemaphoreSubmitInfo graphicsTimelineInfo {
        .waitSemaphoreValueCount = 1, .pWaitSemaphoreValues = &graphicsWaitValue,
        .signalSemaphoreValueCount = 1, .pSignalSemaphoreValues = &graphicsSignalValue
    };

    // Pipeline stage to wait at
    vk::PipelineStageFlags waitStage = vk::PipelineStageFlagBits::eColorAttachmentOutput;
    
    vk::SubmitInfo graphicsSubmitInfo {
        .pNext = &graphicsTimelineInfo, // the wait and signal values for the timelineSemaphore
        .waitSemaphoreCount = 1, .pWaitSemaphores = &*timelineSemaphore,
        .pWaitDstStageMask = &waitStage,
        .commandBufferCount = 1, .pCommandBuffers = &*commandBuffers[currentFrame], // the command buffer to submit
        .signalSemaphoreCount = 1, .pSignalSemaphores = &*timelineSemaphore
    };

    // submit the recorded command buffer to the GPU so it can start working
    // the Vertex stage will be completed in parallel with the previously submitted compute work
    queue.submit(graphicsSubmitInfo, nullptr);
  }

  // explicitly wait on the timeline semaphore, PresentInfoKHR only accepts binary semaphores
  vk::SemaphoreWaitInfo waitInfo {
    .semaphoreCount = 1, .pSemaphores = &*timelineSemaphore,
    .pValues = &graphicsSignalValue
  };
  while (vk::Result::eTimeout == device.waitSemaphores(waitInfo, UINT64_MAX))
    ;

  // PresentInfo without wait semaphores
  vk::PresentInfoKHR presentInfo {
    .swapchainCount = 1, .pSwapchains = &*swapChain,
    .pImageIndices = &imageIndex
  };

  // Present the swap chain image to the surface
  result = queue.presentKHR(presentInfo);
  
  // Double check that the framebuffer hasn't been resized during the frame
  if (result == vk::Result::eErrorOutOfDateKHR || result == vk::Result::eSuboptimalKHR || framebufferResized) {
    framebufferResized = false; RecreateSwapChain();
  }
  else if (result != vk::Result::eSuccess) throw std::runtime_error("failed to present swap chain image!");

  // move on to the next frame
  currentFrame = (currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
}

// When the surface's resolution is updated/the swap chain has gone bad
void App::RecreateSwapChain()
{
  // Get the new resolution
  int width, height; glfwGetFramebufferSize(pWindow, &width, &height);

  // if minimised, don't bother checking again until another resize
  while (width == 0 || height == 0) {
    glfwGetFramebufferSize(pWindow, &width, &height);
    glfwWaitEvents();
  }

  // Don't mess with the resources until you're sure they're not being used
  device.waitIdle();

  // Reset swap chain class members
  CleanupSwapChain();

  // Re-initialise swap chain class members i.e. colour attachment
  CreateSwapChain();
  CreateSwapChainImageViews();

  // Recreate the depth attachment
  CreateDepthResources();

  // Update the camera to reflect the new resolution
  camera.viewportWidth = static_cast<float>(swapChainExtent.width);
  camera.viewportHeight = static_cast<float>(swapChainExtent.height);
}

// The swap chain needs to be cleaned up in two places: RecreateSwapChain() and Cleanup()
void App::CleanupSwapChain()
{
  // End the lifetime of the swap chain class members
  swapChainImageViews.clear();
  swapChain = nullptr;
}

// We compile from SPIR-V to a GPU kernel
// nodiscard means the compiler makes sure that you handle the returned value
[[nodiscard]] vk::raii::ShaderModule App::CreateShaderModule(const std::vector<char>& code) const
{
  // take in SPIR-V
  vk::ShaderModuleCreateInfo createInfo {
    .codeSize = code.size() * sizeof(char),
    .pCode = reinterpret_cast<const uint32_t*>(code.data())
  };
  // compile to GPU-compatible code
  vk::raii::ShaderModule shaderModule(device, createInfo);

  return shaderModule;
}

// Pipelines are almost identical, refer to CreateGraphicsPipeline for a complete rundown
// Define exactly what data and how to process it on the GPU for a specific shader
void App::CreateGraphicsPipeline()
{
  // Reset the class member
  graphicsPipeline = std::pair(nullptr, nullptr);

  // The GPU-ready compiled shader code
  auto shaderModule = CreateShaderModule(ReadFile(spirv_path));

  // All the stages come from the same SPIR-V file, so same shaderModule
  // Need to identify the entrypoint names: "vertMain", "fragMain"
  vk::PipelineShaderStageCreateInfo vertShaderStageCreateInfo {
    .stage = vk::ShaderStageFlagBits::eVertex,
    .module = shaderModule,
    .pName = "vertMain"
  };
  vk::PipelineShaderStageCreateInfo fragShaderStageCreateInfo {
    .stage = vk::ShaderStageFlagBits::eFragment,
    .module = shaderModule,
    .pName = "fragMain"
  };
  std::array shaderStages = { vertShaderStageCreateInfo, fragShaderStageCreateInfo };

  // Get the user-defined format of vertices. Ours is each entry is a vertex, not an instance
  auto bindingDescription = Vertex::getBindingDescription();
  // Vertices are comprised of float3 pos, float3 colour, float2 texCoord
  auto attributesDescriptions = Vertex::getAttributeDescriptions();

  // Combine all that vertex info
  vk::PipelineVertexInputStateCreateInfo vertexInputInfo {
    .vertexBindingDescriptionCount = 1, .pVertexBindingDescriptions = &bindingDescription,
    .vertexAttributeDescriptionCount = static_cast<uint32_t>(attributesDescriptions.size()),
    .pVertexAttributeDescriptions = attributesDescriptions.data()
  };

  // We access vertices in groups of 3 indices
  // Example: { 0, 1, 2, 2, 3, 0 }, { 0, 1, 2 } is one triangle, { 2, 3, 0 } is another
  // whereas TriangleStrip would be { 0, 1, 2, 3 }, { 0, 1, 2 } is one triangle, { 1, 2, 3 } is another
  vk::PipelineInputAssemblyStateCreateInfo inputAssemblyInfo { .topology = vk::PrimitiveTopology::eTriangleList };

  const vk::PipelineDepthStencilStateCreateInfo depthStencil{
    // We need to be able to read from and write to the depthStencil
    .depthTestEnable = vk::True, .depthWriteEnable = vk::True,
    .depthCompareOp = vk::CompareOp::eLess,
    .depthBoundsTestEnable = vk::False,
    .stencilTestEnable = vk::False
  };

  std::array descriptorSetLayouts = { *descriptorSetLayoutGlobal, *descriptorSetLayoutMaterial };

  vk::PushConstantRange pushConstantRange {
    .stageFlags = vk::ShaderStageFlagBits::eFragment,
    .offset = 0,
    .size = sizeof(PushConstant)
  };

  // Which DescriptorSetLayouts will be used by this pipeline
  vk::PipelineLayoutCreateInfo pipelineLayoutInfo { 
    .setLayoutCount = 2, .pSetLayouts = descriptorSetLayouts.data(),
    .pushConstantRangeCount = 1, .pPushConstantRanges = &pushConstantRange
  };
  graphicsPipeline.first = vk::raii::PipelineLayout(device, pipelineLayoutInfo);

  // Which attachments are involved
  vk::PipelineRenderingCreateInfo renderingInfo = {
    .colorAttachmentCount = 1, .pColorAttachmentFormats = &swapChainSurfaceFormat,
    .depthAttachmentFormat = FindDepthFormat()
  };

  // Collate all the info
  vk::GraphicsPipelineCreateInfo pipelineInfo {
    .pNext = &renderingInfo,
    .stageCount = static_cast<uint32_t>(shaderStages.size()),
    .pStages = shaderStages.data(),
    .pVertexInputState = &vertexInputInfo,
    .pInputAssemblyState = &inputAssemblyInfo,
    .pViewportState = &viewportInfo,
    .pRasterizationState = &rasterizerInfo,
    .pMultisampleState = &multisamplingInfo,
    .pDepthStencilState = &depthStencil,
    .pColorBlendState = &colorBlendInfo,
    .pDynamicState = &dynamicInfo,
    .layout = graphicsPipeline.first
  };

  graphicsPipeline.second = vk::raii::Pipeline(device, nullptr, pipelineInfo);
}


void App::CreateComputeGraphicsPipeline()
{
  // Reset the class member
  radianceCascadesOutput.graphicsPipeline = std::pair(nullptr, nullptr);

  // The GPU-ready compiled shader code
  auto shaderModule = CreateShaderModule(ReadFile(compute_spirv_path));

  // All the stages come from the same SPIR-V file, so same shaderModule
  // Need to identify the entrypoint names: "vertMain", "fragMain"
  vk::PipelineShaderStageCreateInfo vertShaderStageCreateInfo {
    .stage = vk::ShaderStageFlagBits::eVertex,
    .module = shaderModule,
    .pName = "vertMain"
  };
  vk::PipelineShaderStageCreateInfo fragShaderStageCreateInfo {
    .stage = vk::ShaderStageFlagBits::eFragment,
    .module = shaderModule,
    .pName = "fragMain"
  };
  std::array shaderStages = { vertShaderStageCreateInfo, fragShaderStageCreateInfo };

  // Get the user-defined format of vertices. Ours is each entry is a vertex, not an instance
  auto bindingDescription = Vertex::getBindingDescription();
  // Vertices are comprised of float3 pos, float3 colour, float2 texCoord
  auto attributesDescriptions = Vertex::getAttributeDescriptions();

  // Combine all that vertex info
  vk::PipelineVertexInputStateCreateInfo vertexInputInfo {
    .vertexBindingDescriptionCount = 1,
    .pVertexBindingDescriptions = &bindingDescription,
    .vertexAttributeDescriptionCount = static_cast<uint32_t>(attributesDescriptions.size()),
    .pVertexAttributeDescriptions = attributesDescriptions.data()
  };

  // Parse indices as triangles by groups of 3 elements
  vk::PipelineInputAssemblyStateCreateInfo inputAssemblyInfo { .topology = vk::PrimitiveTopology::eTriangleList };

  const vk::PipelineDepthStencilStateCreateInfo depthStencil {
    // We're not using the depthStencil in this pipeline
    .depthTestEnable = vk::False, .depthWriteEnable = vk::False,
    .depthCompareOp = vk::CompareOp::eLess,
    .depthBoundsTestEnable = vk::False,
    .stencilTestEnable = vk::False
  };

  // Which DescriptorSetLayouts will be used by this pipeline
  vk::PipelineLayoutCreateInfo pipelineLayoutInfo {
    .setLayoutCount = 1, .pSetLayouts = &*radianceCascadesOutput.descriptorSetLayout
  };
  radianceCascadesOutput.graphicsPipeline.first = vk::raii::PipelineLayout(device, pipelineLayoutInfo);

  // Which attachments are involved
  vk::PipelineRenderingCreateInfo pipelineRenderingInfo = {
    .colorAttachmentCount = 1, .pColorAttachmentFormats = &swapChainSurfaceFormat,
    .depthAttachmentFormat = FindDepthFormat()
  };

  // Collate all the info
  vk::GraphicsPipelineCreateInfo graphicsPipelineInfo {
    .pNext = &pipelineRenderingInfo,
    .stageCount = static_cast<uint32_t>(shaderStages.size()),
    .pStages = shaderStages.data(),
    .pVertexInputState = &vertexInputInfo,
    .pInputAssemblyState = &inputAssemblyInfo,
    .pViewportState = &viewportInfo,
    .pRasterizationState = &rasterizerInfo,
    .pMultisampleState = &multisamplingInfo,
    .pDepthStencilState = &depthStencil,
    .pColorBlendState = &colorBlendInfo,
    .pDynamicState = &dynamicInfo,
    .layout = *radianceCascadesOutput.graphicsPipeline.first
  };

  radianceCascadesOutput.graphicsPipeline.second = vk::raii::Pipeline(device, nullptr, graphicsPipelineInfo);
}

void App::CreateComputePipeline()
{
  // Reset the class member
  computePipeline = std::pair(nullptr, nullptr);
  
  // The GPU-ready compiled shader code
  auto shaderModule = CreateShaderModule(ReadFile(compute_spirv_path));

  // All the stages come from the same SPIR-V file, so same shaderModule
  // Need to identify the entrypoint names: "compMain"
  vk::PipelineShaderStageCreateInfo computeShaderStageInfo {
    .stage = vk::ShaderStageFlagBits::eCompute,
    .module = shaderModule,
    .pName = "compMain"
  };

  // We supply the compute stage with new time information each frame using the TimePC struct for push constants
  vk::PushConstantRange pushConstantRange {
    .stageFlags = vk::ShaderStageFlagBits::eCompute,
    .offset = 0,
    .size = sizeof(TimePC)
  };

  // Which DescriptorSetLayouts will be used by this pipeline
  vk::PipelineLayoutCreateInfo pipelineLayoutInfo {
    .setLayoutCount = 1, .pSetLayouts = &*radianceCascadesOutput.descriptorSetLayout,
    .pushConstantRangeCount = 1, .pPushConstantRanges = &pushConstantRange
  };
  computePipeline.first = vk::raii::PipelineLayout(device, pipelineLayoutInfo);

  // Collate all the info
  vk::ComputePipelineCreateInfo pipelineInfo {
    .stage = computeShaderStageInfo,
    .layout = *computePipeline.first
  };
  computePipeline.second = vk::raii::Pipeline(device, nullptr, pipelineInfo);
}

// Load a glTF file into the "asset" class member
void App::LoadAsset(const char* path)
{
  // Reset the class member to a known value we can check against
  asset = NULL;
  cgltf_options options = {};

  // Read in and parse the glTF into asset
  if (cgltf_parse_file(&options, path, &asset) != cgltf_result_success) {
    throw std::runtime_error(std::string("failed to load ").append(path)); cgltf_free(asset);
  }
  // Check that the parsed data is valid
  if (cgltf_validate(asset) != cgltf_result_success) {
    throw std::runtime_error(std::string("failed to validate ").append(path)); cgltf_free(asset);
  }
  // Load the binary data pointed to by the glTF file
  if (cgltf_load_buffers(&options, asset, path) != cgltf_result_success) {
    throw std::runtime_error(std::string("failed to load buffers for ").append(path)); cgltf_free(asset);
  }

  // Initialise relevant class members as arrays of size materials_count
  mats.clear();
  mats.resize(asset->materials_count);

  // Reset and rebuild arrays of textures and texture views for indexed access
  textureImages.clear();
  textureImages.reserve(static_cast<size_t>(asset->materials_count));

  textureImageViews.clear();
  textureImageViews.reserve(static_cast<size_t>(asset->materials_count));

  for (size_t i = 0; i < asset->materials_count; i++) {
    textureImages.emplace_back(std::pair(nullptr, nullptr));
    textureImageViews.emplace_back(nullptr);
  }

}

// Load all the textures present in the glTF asset
void App::LoadTextures(const std::filesystem::path& parent_path)
{
  // Futures are an asynchronous technique often seen in Javascript. A future promises that they will have a value at
  // some point which we can wait for later
  std::vector<std::future<void>> futures;
  // use reserve when you want to do a single memory allocation and use emplace_back
  futures.reserve(mats.size());

  // Load the albedo texture of each material
  for (size_t i = 0; i < mats.size(); ++i) {
    futures.emplace_back(std::async(std::launch::async, [this, &parent_path, i]() {
        // Get the path of the texture relative to the glTF file
        const char* uri = asset->materials[i].pbr_metallic_roughness.base_color_texture.texture->basisu_image->uri;
        // Adjust the path to be relative to the executable, load the texture at that combined uri
        // The textureImage's and textureImageView's index corresponds to the material's index
        CreateTextureImage((parent_path / uri).generic_string().c_str(), i);
    }));
  }

  // Wait for futures to deliver on their promise (nothing will be returned, wait for completion)
  for (auto& future : futures) future.wait();
}

// Load KTX texture from path into textureImages[idx]
void App::CreateTextureImage(const char* texturePath, size_t idx)
{
  // Load the texture into kTexture
  ktxTexture2* kTexture;
  if (ktxTexture2_CreateFromNamedFile(texturePath, KTX_TEXTURE_CREATE_NO_FLAGS, &kTexture) != KTX_SUCCESS)
    throw std::runtime_error(std::string("failed to load ktx texture image: ").append(texturePath));

  // Transcode the stored KTX texture to a compressed format supported by the GPU
  // This avoids uploading decompressed textures to the GPU
  if (ktxTexture2_NeedsTranscoding(kTexture)) {
    if (!physicalDevice.getFeatures().textureCompressionBC)
      throw std::runtime_error("device cannot transcode to BC");

    if (ktxTexture2_TranscodeBasis(kTexture, KTX_TTF_BC3_RGBA, {}) != KTX_SUCCESS)
      throw std::runtime_error(std::string("failed to transcode ktx texture image: ").append(texturePath));
  }

  // Basic texture info
  auto texWidth = kTexture->baseWidth, texHeight = kTexture->baseHeight, mipLevels = kTexture->numLevels;
  auto textureFormat = static_cast<vk::Format>(ktxTexture2_GetVkFormat(kTexture));
  // Data and data size of the loaded texture
  auto ktxTextureData = ktxTexture_GetData(ktxTexture(kTexture));
  auto imageDataSize = ktxTexture_GetDataSize(ktxTexture(kTexture));

  // We don't need host visibility once it's written to the GPU, copy into memory heap 0 after creation
  vk::raii::Buffer stagingBuffer({});
  vk::raii::DeviceMemory stagingBufferMemory({});

  // Only creates a buffer, data inside is uninitialised
  CreateBuffer(
    imageDataSize,
    vk::BufferUsageFlagBits::eTransferSrc,
    vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent,
    stagingBuffer, stagingBufferMemory
  );

  // Map the required host-visible GPU memory, copy ktxTextureData in, then unmap
  void* data = stagingBufferMemory.mapMemory(0, imageDataSize);
  memcpy(data, ktxTextureData, imageDataSize);
  stagingBufferMemory.unmapMemory();

  // Initialise Image
  CreateImage(
    texWidth, texHeight, mipLevels, textureFormat,
    vk::ImageTiling::eOptimal,
    vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled,
    vk::MemoryPropertyFlagBits::eDeviceLocal,
    textureImages[idx].first, textureImages[idx].second
  );

  // CRITICAL SECTION: each of the following functions involves recording a command buffer
  while (!m.try_lock());
  // Get the image ready for copying to
  TransitionImageLayout(
    textureImages[idx].first, vk::ImageLayout::eUndefined, vk::ImageLayout::eTransferDstOptimal, mipLevels);
  // Copy the host-visible buffer to somewhere else in GPU memory
  CopyBufferToImage(stagingBuffer, textureImages[idx].first, texWidth, texHeight, mipLevels, kTexture);
  // Get the image ready for sampling in the shader
  TransitionImageLayout(
    textureImages[idx].first, vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eShaderReadOnlyOptimal, mipLevels);
  // CRITICAL SECTION OVER
  m.unlock();
  
  // Finished with the data on the CPU
  ktxTexture2_Destroy(kTexture);

  // Create the corresponding View
  textureImageViews[idx] =
    CreateImageView(textureImages[idx].first, textureFormat, vk::ImageAspectFlagBits::eColor, mipLevels);
}

// Transition-only command buffer submission
void App::TransitionImageLayout(
  const vk::raii::Image& image,
  vk::ImageLayout oldLayout, vk::ImageLayout newLayout,
  uint32_t mipLevels
)
{
  // Image layout transitions are single-time commands submitted to the GPU
  const auto commandBuffer = BeginSingleTimeCommands();

  // An ImageMemoryBarrier is like a critical section for image memory operations. When we hit the srcStage we check how
  // we were accessing and define the next stage the Image will be used in and how it will be accessed
  vk::ImageMemoryBarrier barrier {
    .oldLayout = oldLayout,
    .newLayout = newLayout,
    .image = image,
    .subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, mipLevels, 0, 1}
  };

  // The stage at which to begin barricading, and when to end. As in where the last write took place -> where we pick up
  vk::PipelineStageFlags srcStage, dstStage;

  // We intentionally support only the following transitions
  // Texture of some material getting ready to be copied to
  if (oldLayout == vk::ImageLayout::eUndefined && newLayout == vk::ImageLayout::eTransferDstOptimal) {
    // How the subresources have/will be accessed at the stages
    barrier.srcAccessMask = {};
    barrier.dstAccessMask = vk::AccessFlagBits::eTransferWrite;

    srcStage = vk::PipelineStageFlagBits::eTopOfPipe;
    dstStage = vk::PipelineStageFlagBits::eTransfer;
  }
  // Texture of some model after being copied to from host-visible memory
  else if (oldLayout == vk::ImageLayout::eTransferDstOptimal && newLayout == vk::ImageLayout::eShaderReadOnlyOptimal) {
    // Once transferred, we will only be using this for sampling in the Fragment stage
    barrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
    barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;

    srcStage = vk::PipelineStageFlagBits::eTransfer;
    dstStage = vk::PipelineStageFlagBits::eFragmentShader;
  }
  // The render target for the path tracing shader
  else if (oldLayout == vk::ImageLayout::eUndefined && newLayout == vk::ImageLayout::eGeneral) {
    // Not confident this is correct
    barrier.srcAccessMask = vk::AccessFlagBits::eShaderWrite;
    barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;

    srcStage = vk::PipelineStageFlagBits::eComputeShader;
    dstStage = vk::PipelineStageFlagBits::eFragmentShader;
  }
  else throw std::invalid_argument("unsupported layout transition!");

  // Attach the barrier to the command buffer
  commandBuffer.pipelineBarrier(srcStage, dstStage, {}, {}, nullptr, barrier);
  
  // Submit the transition
  EndSingleTimeCommands(commandBuffer);
}

// Specifies how a shader retrieves texture information
void App::CreateTextureSampler()
{
  vk::PhysicalDeviceProperties properties = physicalDevice.getProperties();
  // eLinear interpolates based on subtexel coordinates
  // eRepeat tiles the image for texture coordinates outside of [float2(0.0), float2(1.0)]
  // mipLodBias is like an offset for mip levels
  // Anisotropy preserves parallel lines in textures when viewed from oblique angles by sampling a kernel of texels
  // with a bias reflecting the shape of the surface after the MVP matrix is applied (there are great visualisations
  // online)
  // Changing minLod will change which mipLevel to start from
  // Mess around with the options, see what happens
  vk::SamplerCreateInfo samplerInfo {
    .magFilter = vk::Filter::eLinear, .minFilter = vk::Filter::eLinear,
    .mipmapMode = vk::SamplerMipmapMode::eLinear,
    .addressModeU = vk::SamplerAddressMode::eRepeat,
    .addressModeV = vk::SamplerAddressMode::eRepeat,
    .addressModeW = vk::SamplerAddressMode::eRepeat,
    .mipLodBias = 0.0f,
    .anisotropyEnable = vk::True,
    .maxAnisotropy = properties.limits.maxSamplerAnisotropy,
    .compareEnable = vk::False, .compareOp = vk::CompareOp::eAlways,
    .minLod = 0.0f, .maxLod = vk::LodClampNone
  };

  textureSampler = vk::raii::Sampler(device, samplerInfo);
}

/*=============================================== USING TEMPLATE TYPES ===============================================//
    Template types can be used when the operations within a function are type-agnostic within appropriate use cases
    but the return type must be defined when called.
    In our case, we know that we are operating on data structures of known size solely containing floats. Execution
    logic of getAccessorData is identical for all cases but working on varying numbers of components.
*/
template<typename T>
std::vector<T> GetAccessorData(const cgltf_accessor* accessor) // implementation-only helper function, not a member
{
  auto num_comps = cgltf_num_components(accessor->type); // 2 == glm::vec2, 3 == glm::vec3
  // Make sure the expected type matches actual type
  if (num_comps != sizeof(T) / sizeof(float)) throw std::runtime_error("accessor component count does not match T!");
  auto num_elems = num_comps * accessor->count; // how many vectors this accessor points to

  // Allocate room for the raw floats
  auto unpacked_data = static_cast<cgltf_float*>(malloc(num_elems * sizeof(cgltf_float)));
  if (!unpacked_data) throw std::runtime_error("failed to allocate memory for cgltf_accessor!");

  std::vector<T> output(accessor->count);

  // cgltf is a C library, just hope that they have sufficient bounds-checking
  auto written_elems = cgltf_accessor_unpack_floats(accessor, unpacked_data, num_elems);
  for (cgltf_size i = 0; i < written_elems; i += num_comps) // Loop over elements (groups of num_comps)
    for (cgltf_size j = 0; j < num_comps; j++)              // Loop over the components
      // Fill in the output glm vector
      output[static_cast<size_t>(i / num_comps)][static_cast<glm::length_t>(j)] = unpacked_data[i + j];

  // All stored, no need for unpacked_data anymore
  free(unpacked_data);

  return output;
}

// Parse the glTF data as user-defined Vertex structs
void App::LoadGeometry()
{
  vertices.clear();
  
  meshes.clear();
  meshes.resize(asset->meshes_count); // We know how many meshes there are

  uint32_t indexOffset = 0;

  for (cgltf_size meshIt = 0; meshIt < asset->meshes_count; meshIt++) {
    auto m = &asset->meshes[meshIt];

    for (cgltf_size primIt = 0; primIt < m->primitives_count; primIt++) {
      auto p = &m->primitives[primIt];

      uint32_t startOffset = indexOffset;

      // An array where each index's value is its index e.g. { 0, 1, 2, 3 }
      auto matIdxs = std::views::iota(cgltf_size{ 0 }, asset->materials_count);
      // Get the mats index of the primitive's identical material by comparing the uris
      // We derived the order of mats from the order of asset->materials, so they are identical
      auto matIt = std::ranges::find_if(matIdxs, [&](cgltf_size i) {
        return strcmp(
                 p->material->pbr_metallic_roughness.base_color_texture.texture->basisu_image->uri,
          asset->materials[i].pbr_metallic_roughness.base_color_texture.texture->basisu_image->uri
        ) == 0;
      });
      if (matIt == matIdxs.end()) throw std::runtime_error("failed to find material!");
      mats[*matIt].id = *matIt;

      // v_offset will help in evaluating the absolute value of this primitives indices so they match up with the
      // correct vertices in the vertex buffer
      uint32_t v_offset = static_cast<uint32_t>(vertices.size());

      {
        // Get the accessor for where the primitive stores its indices
        auto indexAccessor = p->indices;
        // This should look reminiscent of GetAccessorData
        auto unpacked_indices = static_cast<uint32_t*>(malloc(indexAccessor->count * sizeof(uint32_t)));
        if (!unpacked_indices) throw std::runtime_error("failed to allocate for indices");

        // Get the ints from the bin
        cgltf_size written_uints = cgltf_accessor_unpack_indices(
          indexAccessor, unpacked_indices,
          static_cast<cgltf_size>(sizeof(uint32_t)),
          indexAccessor->count
        );

        // v_offset is a value offset, i_offset is an index offset
        // We need to append the indices to the materials indices vector
        size_t i_offset = indices.size();
        indices.resize(indices.size() + indexAccessor->count);

        // write in values
        for (cgltf_size i = 0; i < written_uints; i++) {
          indices[i + i_offset] = unpacked_indices[i] + v_offset;
        }

        // Insert the indices in reverse order if the material is double-sided (triggers a redraw of the backface as a 
        // frontface, using a reverse iterator and offsets from rbegin (which is the end in the direction of begin)
        if (p->material->double_sided)
          indices.insert(indices.end(), indices.rbegin(), indices.rbegin() + written_uints);
        
        indexOffset = indices.size();

        // finished with the unpacked_indices
        free(unpacked_indices);
      }

      // There isn't a defined order that the attributes must be listed in to be valid glTF entries. Because of this, we
      // have no idea which attribute represent position and which is texcoords without asking
      cgltf_accessor* posAccessor = NULL; cgltf_accessor* uvAccessor = NULL; cgltf_accessor* normAccessor = NULL;
      // Check each attribute until both pos and uv found
      for (cgltf_size attrIt = 0; attrIt < p->attributes_count; attrIt++) {
        // Check this attribute
        auto attr = &p->attributes[attrIt];

        if (attr->type == cgltf_attribute_type_position)
          posAccessor = attr->data;
        else if (attr->type == cgltf_attribute_type_texcoord)
          uvAccessor = attr->data;
        else if (attr->type == cgltf_attribute_type_normal)
          normAccessor = attr->data;

        // if both have been found, exit
        if (posAccessor && uvAccessor && normAccessor) break;
      }
      if (!posAccessor) throw std::runtime_error("failed to get positions!");
      if (!uvAccessor) throw std::runtime_error("failed to get texcoords!");
      if (!normAccessor) throw std::runtime_error("failed to get normal!");

      // Unpack vertex positions into glm::vec3 vector
      auto positions = GetAccessorData<glm::vec3>(posAccessor);
      // Unpack vertex texcoords into glm::vec2 vector
      auto uvs = GetAccessorData<glm::vec2>(uvAccessor);
      // Unpack vertex normals into glm::vec3 vector
      auto norms = GetAccessorData<glm::vec3>(normAccessor);

      // Get the model's scale
      glm::vec3 scale(1.0f);
      if (asset->nodes[0].has_scale) {
        scale.x = asset->nodes[0].scale[0];
        scale.y = asset->nodes[0].scale[1];
        scale.z = asset->nodes[0].scale[2];
      }

      // Instantiate new default vertices
      vertices.resize(vertices.size() + posAccessor->count);
      for (size_t i = 0; i < posAccessor->count; i++) {
        vertices[v_offset + i].pos = positions[i] * scale; // positions adjusted for model scale
        vertices[v_offset + i].texCoord = uvs[i];
        vertices[v_offset + i].norm = norms[i];
      }

      submeshes.push_back({
        .indexOffset = startOffset,
        .indexCount = indexOffset - startOffset,
        .materialID = static_cast<int>(*matIt),
        .firstVertex = 0u,
        .maxVertex = static_cast<uint32_t>(vertices.size()),
        .alphaCut = p->material->alpha_mode > cgltf_alpha_mode_opaque,
        .reflective = p->material->has_specular == 1
      });
    }
  }

  // We're done with asset, free it now
  cgltf_free(asset);
}

// We just need the vertices stored on the GPU. We will instruct the GPU on how to interpret the data later.
std::pair<vk::raii::Buffer, vk::raii::DeviceMemory> App::CreateVertexBuffer(const std::vector<Vertex>& verts)
{
  // Our user-defined Vertex struct is of uniform size for all instantiations
  vk::DeviceSize bufferSize = sizeof(Vertex) * verts.size();
  vk::raii::Buffer stagingBuffer({});
  vk::raii::DeviceMemory stagingBufferMemory({});

  // We create a CPU-editable buffer, insert the data, then copy that buffer into one that does not require CPU access
  // Notice the buffer usage flag TransferSrc. This lets the GPU know we will be copying this buffer at some point
  CreateBuffer(
    bufferSize,
    vk::BufferUsageFlagBits::eTransferSrc,
    vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent,
    stagingBuffer, stagingBufferMemory
  );

  // Map and copy our loaded vertices data to the GPU host-visible memory
  void* dataStaging = stagingBufferMemory.mapMemory(0, bufferSize);
  memcpy(dataStaging, verts.data(), (size_t)bufferSize);
  // We don't need to access this buffer anymore, unmap
  stagingBufferMemory.unmapMemory();

  std::pair<vk::raii::Buffer, vk::raii::DeviceMemory> copyBuffer = std::pair(nullptr, nullptr);

  // Create a Vertex Buffer in DEVICE_LOCAL memory not necessarily visible to host
  // Notice the buffer usage flag TransferDst. We will be copying to this buffer.
  CreateBuffer(
    bufferSize,
    vk::BufferUsageFlagBits::eVertexBuffer | 
    vk::BufferUsageFlagBits::eTransferDst | 
    vk::BufferUsageFlagBits::eShaderDeviceAddress |
    vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR,
    vk::MemoryPropertyFlagBits::eDeviceLocal,
    copyBuffer.first, copyBuffer.second
  );

  // Copy the host-visible buffer to the non-host-visible buffer
  CopyBuffer(stagingBuffer, copyBuffer.first, bufferSize);

  return copyBuffer;
}

// Use the Slang Compilation API to compile slang shaders to SPIR-V during and by the application
void App::CompileShader(const char* src, const char* dst)
{
  // We need to establish what this compilation session can and will do
  slang::SessionDesc sessionDesc = {};
  // Targeting SPIR-V v1.4 and writing it straight to a file
  slang::TargetDesc targetDesc = {};
  targetDesc.format = SLANG_SPIRV;
  targetDesc.profile = globalSession->findProfile("spirv_1_4");
  targetDesc.flags = SLANG_TARGET_FLAG_GENERATE_SPIRV_DIRECTLY;

  // Add the target to the session info
  sessionDesc.targetCount = 1;
  sessionDesc.targets = &targetDesc;

  // Some options that ensure proper output
  std::array compilerOptionEntries = {
    // We identify stages by entrypoint names
    slang::CompilerOptionEntry {
      .name = slang::CompilerOptionName::VulkanUseEntryPointName,
      .value = slang::CompilerOptionValue {.intValue0 = true }
    },
    // I think Vulkan must be column-major (columns are contiguous), this doesn't work without it
    slang::CompilerOptionEntry {
      .name = slang::CompilerOptionName::MatrixLayoutColumn,
      .value = slang::CompilerOptionValue {.intValue0 = true }
    }
  };
  sessionDesc.compilerOptionEntries = compilerOptionEntries.data();
  sessionDesc.compilerOptionEntryCount = static_cast<uint32_t>(compilerOptionEntries.size());

  // Slang likes to look for the files by itself, even if you pass in an absolute path, so direct it to look in the
  // parent directory of src
  auto searchPath = std::filesystem::path(src).parent_path().string();
  const char* searchPaths[] = { searchPath.c_str() };
  sessionDesc.searchPathCount = 1;
  sessionDesc.searchPaths = searchPaths;

  // Create this session from the global session
  // Notice writeRef(). ComPtr is not directly interfaceable, but comes with helper functions like writeRef for passing
  // by reference
  Slang::ComPtr<slang::ISession> session;
  globalSession->createSession(sessionDesc, session.writeRef());

  // Works like debugMessenger, but for Slang
  // A Blob is a Binary Large Object, an ambiguous chunk of data ready for interpreting
  Slang::ComPtr<slang::IBlob> diagnostics;
  // Slang does not expect the entirety of a shader to be in one file. It treats each file like a module, links all the
  // modules together and then outputs the SPIR-V.
  auto moduleName = std::filesystem::path(src).stem().string(); // just the filename, no path (Slang will look)
  auto module = 
    static_cast<Slang::ComPtr<slang::IModule>>(session->loadModule(moduleName.c_str(), diagnostics.writeRef()));
  // Error messaging
  if (diagnostics) std::cerr << (const char*)diagnostics->getBufferPointer() << std::endl;

  // Each of these entrypoints may exist, the IEntryPoint will simply contain nothing if it does not
  Slang::ComPtr<slang::IEntryPoint> vertexEntryPoint;
  module->findAndCheckEntryPoint("vertMain", SLANG_STAGE_VERTEX, vertexEntryPoint.writeRef(), nullptr);

  Slang::ComPtr<slang::IEntryPoint> fragmentEntryPoint;
  module->findAndCheckEntryPoint("fragMain", SLANG_STAGE_FRAGMENT, fragmentEntryPoint.writeRef(), nullptr);

  Slang::ComPtr<slang::IEntryPoint> computeEntryPoint;
  module->findAndCheckEntryPoint("compMain", SLANG_STAGE_COMPUTE, computeEntryPoint.writeRef(), nullptr);

  // Get all existing entrypoints and the module ready for assembling
  std::vector<slang::IComponentType*> componentTypes = { module };
  if (vertexEntryPoint) componentTypes.push_back(vertexEntryPoint);
  if (fragmentEntryPoint) componentTypes.push_back(fragmentEntryPoint);
  if (computeEntryPoint) componentTypes.push_back(computeEntryPoint);

  // Compose/Assemble a program from the module and entrypoints
  Slang::ComPtr<slang::IComponentType> composedProgram;
  {
    SlangResult result = session->createCompositeComponentType(
      componentTypes.data(),
      componentTypes.size(),
      composedProgram.writeRef(),
      diagnostics.writeRef());
    if (diagnostics) std::cerr << diagnostics << std::endl;

    if (SLANG_FAILED(result)) std::cerr << "failed to compose program" << std::endl;
  }

  // Convert the composed program into target-compatible bytecode
  {
    Slang::ComPtr<slang::IBlob> spirvCode; // chunk of bytecode
    auto result = composedProgram->getTargetCode(0, spirvCode.writeRef(), diagnostics.writeRef());
    
    if (diagnostics) std::cerr << static_cast<const char*>(diagnostics->getBufferPointer()) << std::endl;
    
    if (SLANG_SUCCEEDED(result))
      // Write the bytecode to a file for later. You could also store the compiled code in memory, saving on IO later
      WriteFile(dst, spirvCode->getBufferPointer(), spirvCode->getBufferSize());
    else
      std::cerr << "failed to get target code" << std::endl;
  }
}

// Recreate all the pipelines if the SPIR-V is valid
void App::ReloadShaders()
{
  // wait until queues are empty
  device.waitIdle();

  // Check that the SPIR-V files exist before continuing
  auto spirv_paths = {&spirv_path, &compute_spirv_path};
  for (const auto& path : spirv_paths) {
    auto f = fopen(*path, "r");
    // if the SPIR-V does not exist, abort reloading
    if (f == NULL) {
      std::clog << "failed to open " << path << std::endl;
      return;
    }
    fclose(f);
  }

  // After the initial creation, this acts more like RecreatePipelines
  CreatePipelines();
}

// For an explanation of memory mapping, see the comment "HOW DOES MEMORY WORK?" above CreateUniformBuffers
// Update the uniform buffer containing the Model View Projection matrix from the Camera
void App::UpdateModelViewProjection(uint32_t imageIndex)
{
  MVP mvp{};
  mvp.model = camera.GetModelMatrix();
  mvp.view = camera.GetViewMatrix();
  mvp.proj = camera.GetProjMatrix();
  mvp.pos = camera.GetPos();
  // Copy the new data to the mapped data
  memcpy(mvpBuffersMapped[imageIndex], &mvp, sizeof(mvp));
}

// Record commands for compute (dispatching)
void App::RecordComputeCommandBuffer()
{
  // Fairly simple, just dispatch a number of threads to work on a compute shader
  computeCommandBuffers[currentFrame].begin({});
  computeCommandBuffers[currentFrame].bindPipeline(vk::PipelineBindPoint::eCompute, computePipeline.second);
  computeCommandBuffers[currentFrame].bindDescriptorSets(
    vk::PipelineBindPoint::eCompute, computePipeline.first, 0,
    // DescriptorSets (plural, so spoof multiple with initializer list)
    { radianceCascadesOutput.descriptorSets[currentFrame] },
    {}
  );
  TimePC pushConstant {
    .deltaTime = static_cast<float>(delta) / 1000.0f,
    .accumTime = static_cast<float>(
      std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now() - start).count()
    ) / 1024.0f
  };
  computeCommandBuffers[currentFrame].pushConstants<TimePC>(
    *computePipeline.first, vk::ShaderStageFlagBits::eCompute, 0, pushConstant);
  // For path tracing, dispatch(WIDTH, HEIGHT, 1) lets the shader use the threadID as pixel coordinates for writing
  computeCommandBuffers[currentFrame].dispatch(WIDTH, HEIGHT, 1);  
  computeCommandBuffers[currentFrame].end();
}

// The graphics command buffer
void App::RecordCommandBuffer(uint32_t imageIndex)
{
  commandBuffers[currentFrame].begin({});

  // Transition the swap chain image so its optimal for writing to the colour attachment of the framebuffer
  TransitionImageLayout(
    imageIndex,
    vk::ImageLayout::eUndefined,
    vk::ImageLayout::eColorAttachmentOptimal,
    {},
    vk::AccessFlagBits2::eColorAttachmentWrite,
    vk::PipelineStageFlagBits2::eTopOfPipe,
    vk::PipelineStageFlagBits2::eColorAttachmentOutput
  );

  vk::ImageMemoryBarrier2 depthBarrier {
    .srcStageMask = vk::PipelineStageFlagBits2::eTopOfPipe,
    .srcAccessMask = {},
    .dstStageMask = vk::PipelineStageFlagBits2::eEarlyFragmentTests |
                    vk::PipelineStageFlagBits2::eLateFragmentTests,
    .dstAccessMask = vk::AccessFlagBits2::eDepthStencilAttachmentRead | 
                     vk::AccessFlagBits2::eDepthStencilAttachmentWrite,
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

  vk::DependencyInfo depthDependencyInfo {
    .dependencyFlags = {},
    .imageMemoryBarrierCount = 1,
    .pImageMemoryBarriers = &depthBarrier
  };
  commandBuffers[currentFrame].pipelineBarrier2(depthDependencyInfo);
  vk::ClearDepthStencilValue clearDepth = vk::ClearDepthStencilValue(1.0f, 0);

  vk::RenderingAttachmentInfo depthAttachmentInfo {
    .imageView = depthImageView,
    .imageLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal,
    .loadOp = vk::AttachmentLoadOp::eClear,
    .storeOp = vk::AttachmentStoreOp::eDontCare,
    .clearValue = clearDepth,
  };

  vk::ClearValue clearColour = vk::ClearColorValue(0.0f, 0.0f, 0.0f, 1.0f);

  vk::RenderingAttachmentInfo colourAttachmentInfo {
    .imageView = swapChainImageViews[imageIndex],
    .imageLayout = vk::ImageLayout::eAttachmentOptimal,
    .loadOp = vk::AttachmentLoadOp::eClear,
    .storeOp = vk::AttachmentStoreOp::eStore,
    .clearValue = clearColour,
  };

  vk::RenderingInfo renderingInfo {
    .renderArea = {.offset = {0, 0}, .extent = swapChainExtent},
    .layerCount = 1,
    .colorAttachmentCount = 1,
    .pColorAttachments = &colourAttachmentInfo,
    .pDepthAttachment = &depthAttachmentInfo
  };

  commandBuffers[currentFrame].beginRendering(renderingInfo);

  commandBuffers[currentFrame].setViewport(0, vk::Viewport(
      0.0f, 0.0f,
      static_cast<float>(swapChainExtent.width), static_cast<float>(swapChainExtent.height),
      0.0f, 1.0f
   ));
  commandBuffers[currentFrame].setScissor(0, vk::Rect2D(vk::Offset2D(0, 0), swapChainExtent));

  // STATIC MODELS
  {
    commandBuffers[currentFrame].bindPipeline(vk::PipelineBindPoint::eGraphics, *graphicsPipeline.second);
    commandBuffers[currentFrame].bindVertexBuffers(0, *vertexBuffer.first, { 0 });
    commandBuffers[currentFrame].bindIndexBuffer(*indexBuffer.first, 0, vk::IndexType::eUint32);
    commandBuffers[currentFrame].bindDescriptorSets(
      vk::PipelineBindPoint::eGraphics, *graphicsPipeline.first, 0, *globalDescriptorSets[currentFrame], nullptr);
    commandBuffers[currentFrame].bindDescriptorSets(
      vk::PipelineBindPoint::eGraphics, *graphicsPipeline.first, 1, *materialDescriptorSets[0], nullptr);

    for (auto& submesh: submeshes) {
      PushConstant pushConstant {
        .materialIndex = static_cast<uint32_t>(submesh.materialID),
        .reflective = submesh.reflective
      };
      commandBuffers[currentFrame].pushConstants<PushConstant>(
        *graphicsPipeline.first, vk::ShaderStageFlagBits::eFragment, 0, pushConstant);
      commandBuffers[currentFrame].drawIndexed(submesh.indexCount, 1, submesh.indexOffset, 0, 0);
    }
  }

  // COMPUTE RESULTS
  // render these after model as we want them in front without needing the depth buffer
  {
    commandBuffers[currentFrame].bindPipeline(
      vk::PipelineBindPoint::eGraphics, *radianceCascadesOutput.graphicsPipeline.second);
    commandBuffers[currentFrame].bindVertexBuffers(0, *triangleBuffer.first, { 0 });
    commandBuffers[currentFrame].bindDescriptorSets(
      vk::PipelineBindPoint::eGraphics,
      radianceCascadesOutput.graphicsPipeline.first,
      0,
      *radianceCascadesOutput.descriptorSets[currentFrame],
      nullptr
    );
    commandBuffers[currentFrame].draw(3, 1, 0, 0);
  }

#ifdef _DEBUG
  // Record all the ImGui commands at the end so they appear on top
  ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), static_cast<VkCommandBuffer>(*commandBuffers[currentFrame]));
#endif

  // All done dealing with rendering
  commandBuffers[currentFrame].endRendering();

  // Transition the swap chain image, ready for presenting
  TransitionImageLayout(
    imageIndex,
    vk::ImageLayout::eColorAttachmentOptimal,
    vk::ImageLayout::ePresentSrcKHR,
    vk::AccessFlagBits2::eColorAttachmentWrite,
    {},
    vk::PipelineStageFlagBits2::eColorAttachmentOutput,
    vk::PipelineStageFlagBits2::eBottomOfPipe
  );

  // All done recording
  commandBuffers[currentFrame].end();
}

// Specify access changes for the swap chain images during command buffer recording
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
  // See the other TransitionImageLayout for information ImageMemoryBarrier
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

  vk::DependencyInfo dependencyInfo {
    .dependencyFlags = {},
    .imageMemoryBarrierCount = 1,
    .pImageMemoryBarriers = &barrier
  };

  commandBuffers[currentFrame].pipelineBarrier2(dependencyInfo);
}

// Retrieve a usable depth format, not especially important it just needs to work
[[nodiscard]] vk::Format App::FindDepthFormat() const
{
  // We don't require a stencil, but if a format containing a stencil is the first available it is welcome
  return FindSupportedFormat(
    { vk::Format::eD32Sfloat, vk::Format::eD32SfloatS8Uint, vk::Format::eD24UnormS8Uint },
    vk::ImageTiling::eOptimal,
    vk::FormatFeatureFlagBits::eDepthStencilAttachment
  );
}

// Check for some desired format-capabilities against those available
vk::Format App::FindSupportedFormat(
  const std::vector<vk::Format>& candidates,
  vk::ImageTiling tiling,
  vk::FormatFeatureFlags features
) const
{
  // Iterate through candidate formats until one possesses the desired capabilities
  auto formatIter = std::ranges::find_if(candidates, [&](auto const format) {
      vk::FormatProperties props = physicalDevice.getFormatProperties(format);
      return (((tiling == vk::ImageTiling::eLinear)  && ((props.linearTilingFeatures & features)  == features)) ||
              ((tiling == vk::ImageTiling::eOptimal) && ((props.optimalTilingFeatures & features) == features)));
    });
  if (formatIter == candidates.end()) throw std::runtime_error("failed to find supported format!");

  return *formatIter;
}

uint32_t App::FindMemoryType(uint32_t typeFilter, vk::MemoryPropertyFlags properties)
{
  // typeFilter is a bitmask, and we iterate over it by shifting 1 by i
  // then we check if it has the same properties as properties
  vk::PhysicalDeviceMemoryProperties memProperties = physicalDevice.getMemoryProperties();
  for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
    if ((typeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties)
      return i;
  }

  // if you got here, it's wrong
  throw std::runtime_error("failed to find suitable memory type!");
}

// Allocate DeviceMemory for an image, return handles to the objects
void App::CreateImage(
  uint32_t width, uint32_t height, uint32_t mipLevels,
  vk::Format format,
  vk::ImageTiling tiling, vk::ImageUsageFlags usage,
  vk::MemoryPropertyFlags properties,
  vk::raii::Image& image, vk::raii::DeviceMemory& imageMemory
)
{
  vk::ImageCreateInfo imageInfo {
    .imageType = vk::ImageType::e2D,
    .format = format,
    .extent = {width, height, 1},
    .mipLevels = mipLevels,
    .arrayLayers = 1,
    .samples = vk::SampleCountFlagBits::e1,
    .tiling = tiling, .usage = usage,
    .sharingMode = vk::SharingMode::eExclusive,
    .queueFamilyIndexCount = 0
  };
  image = vk::raii::Image(device, imageInfo);

  // Back up the image on DEVICE_LOCAL memory
  vk::MemoryRequirements memRequirements = image.getMemoryRequirements();
  vk::MemoryAllocateInfo allocInfo {
    .allocationSize = memRequirements.size,
    .memoryTypeIndex = FindMemoryType(memRequirements.memoryTypeBits, properties)
  };
  imageMemory = vk::raii::DeviceMemory(device, allocInfo);
  image.bindMemory(imageMemory, 0);
}

// Might be the most straight-forward function, simple input-output
[[nodiscard]] vk::raii::ImageView App::CreateImageView(
  const vk::Image& image, vk::Format format,
  vk::ImageAspectFlags aspectFlags,
  uint32_t mipLevels
) const
{
  vk::ImageViewCreateInfo viewInfo {
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

// Allocate and start recording a one-time command buffer
vk::raii::CommandBuffer App::BeginSingleTimeCommands() const
{
  // Same allocInfo as computeCommandBuffers and commandBuffers
  vk::CommandBufferAllocateInfo allocInfo {
    .commandPool = commandPool,
    .level = vk::CommandBufferLevel::ePrimary,
    .commandBufferCount = 1
  };

  // We only need one command buffer but there is no suitable constructor for a single buffer, so grab the first
  vk::raii::CommandBuffer commandBuffer = std::move(vk::raii::CommandBuffers(device, allocInfo).front());

  // Start recording the command buffer (with the understanding that it will only been used once)
  vk::CommandBufferBeginInfo beginInfo {
    .flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit
  };
  commandBuffer.begin(beginInfo);

  return commandBuffer;
}

// Stop recording, submit and wait for completion of commandBuffer
void App::EndSingleTimeCommands(const vk::raii::CommandBuffer& commandBuffer) const
{
  // Stop recording
  commandBuffer.end();

  // Submit to queue
  vk::SubmitInfo submitInfo {
    .commandBufferCount = 1,
    .pCommandBuffers = &*commandBuffer
  };
  queue.submit(submitInfo, nullptr);

  // Wait until submission completed
  queue.waitIdle();
}

// Initialise device space for a Buffer
void App::CreateBuffer(
  vk::DeviceSize size,
  vk::BufferUsageFlags usage,
  vk::MemoryPropertyFlags properties,
  vk::raii::Buffer& buffer,
  vk::raii::DeviceMemory& bufferMemory
)
{
  vk::BufferCreateInfo bufferInfo {
    .size = size,
    .usage = usage,
    .sharingMode = vk::SharingMode::eExclusive
  };
  buffer = vk::raii::Buffer(device, bufferInfo);
  
  // Back up the buffer with DeviceMemory
  vk::MemoryRequirements memRequirements = buffer.getMemoryRequirements();
  vk::MemoryAllocateInfo allocInfo {
    .allocationSize = memRequirements.size,
    .memoryTypeIndex = FindMemoryType(memRequirements.memoryTypeBits, properties)
  };

  // if the buffer shares a shader device address we need space in the buffer for those addresses
  vk::MemoryAllocateFlagsInfo allocFlagsInfo{};
  if (usage & vk::BufferUsageFlagBits::eShaderDeviceAddress) {
    allocFlagsInfo.flags = vk::MemoryAllocateFlagBits::eDeviceAddress;
    allocInfo.pNext = &allocFlagsInfo;
  }

  bufferMemory = vk::raii::DeviceMemory(device, allocInfo);
  buffer.bindMemory(*bufferMemory, 0);
}

// Simple submission of a buffer copy command to the GPU
void App::CopyBuffer(const vk::raii::Buffer& srcBuffer, const vk::raii::Buffer& dstBuffer, vk::DeviceSize size)
{
  const auto commandCopyBuffer = BeginSingleTimeCommands();
  commandCopyBuffer.copyBuffer(*srcBuffer, *dstBuffer, vk::BufferCopy{ .size = size });
  EndSingleTimeCommands(commandCopyBuffer);
}

// Mip level inclusive copying of a host-visible Buffer to an Image
void App::CopyBufferToImage(
  const vk::raii::Buffer& buffer,
  const vk::raii::Image& image,
  uint32_t width, uint32_t height, uint32_t mipLevels,
  ktxTexture2* kTexture
)
{
  auto commandBuffer = BeginSingleTimeCommands();

  std::vector<vk::BufferImageCopy> regions;

  // Get each mip level as a region of the texture
  for (uint32_t level = 0; level < mipLevels; level++) {
    ktx_size_t offset;
    ktxTexture2_GetImageOffset(kTexture, level, 0, 0, &offset);

    // Mip levels are always half the size of previous (cascading resolutions)
    // Dividing by 2 is super easy with unsigned integers, a single bit shift towards the endian
    uint32_t mipWidth = std::max(1u, width >> level), mipHeight = std::max(1u, height >> level);

    vk::BufferImageCopy region {
      .bufferOffset = offset,
      .bufferRowLength = 0, .bufferImageHeight = 0,
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
  // Copy the collated regions into an image
  commandBuffer.copyBufferToImage(buffer, image, vk::ImageLayout::eTransferDstOptimal, regions);
  EndSingleTimeCommands(commandBuffer);
}

// Read bytes from file
static const std::vector<char> ReadFile(const char* fileName)
{
  // input stream for a file, ios::ate == opens at EOF, ios::binary == reads binary 
  std::ifstream file(fileName, std::ios::ate | std::ios::binary);

  if (!file.is_open()) throw std::runtime_error("failed to open file!");

  // We opened at the end of the file. By getting the streampos at that point we can determine the size of the file
  // and allocate a vector of chars for that number of bytes
  std::vector<char> buffer(file.tellg());
  // Move the streampos to the 0th character from beginning
  // AKA move to start of file
  file.seekg(0, std::ios::beg);
  // Read the specified number of bytes into buffer
  file.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
  file.close();
  return buffer;
}

// Write bytes to file (creates file if file does not exist)
static void WriteFile(const char* fileName, void const* code, size_t bytes)
{
  // output stream for a file, writes binary
  std::ofstream file(fileName, std::ios::binary);

  // Safeguard in case the resource is not available right now
  if (!file.is_open()) throw std::runtime_error("failed to open file!");

  // Write "byte" many chars to file
  file.write(static_cast<const char*>(code), bytes);
  file.close();
}
