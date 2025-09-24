#ifndef _APP_HPP_
#define _APP_HPP_

//================== Standard Libraries =================//
#include <vector> // Dynamic arrays
#include <filesystem> // Platform-agnostic filepaths
#include <mutex> // Synchronisation object

//============================================ Vulkan Types and Functions ================================================//
// saves us from linking vulkan library, needs to be included BEFORE vulkan.h
#include <volk/volk.h> // Vulkan meta-loader (grabs functions from Vulkan driver, not linked Vulkan library)

// This app uses the RAII header similar to smart pointers, which detect when they go out of scope and free themselves without intervention
// This app makes use of the Vulkan_HPP C++ bindings, which can cut down on the explicitness of some of the intrinsic boilerplate of Vulkan structures,
// like not needing to define unused members or specifying the sType of strongly-typed structs
#include <vulkan/vulkan_raii.hpp> // C++ bindings and RAII definitions for vulkan

#ifdef NDEBUG
constexpr bool enableValidationLayers = false;
#else
constexpr bool enableValidationLayers = true;
#endif

// Vulkan Validation Layers to enable in Debug mode
const std::vector validationLayers = { // not array; vector allows implicit typing and contents are immutable
  "VK_LAYER_KHRONOS_validation"
};

//============================ Window Management ============================//
#define GLFW_INCLUDE_NONE // We already include Vulkan through vulkan_raii
#include <GLFW/glfw3.h> // Platform-agnostic window manager

//=========== Asset Management and Loading ===========//
#include "ktx.h" // Image loader, for ktxTexture2
#include "cgltf.h" // Model loader, for cgltf_asset

//======== Slang to SPIR-V Compilation ========//
#include <slang/slang-com-ptr.h>
#include <slang/slang.h>

//============================== User-Defined Structs ==============================//
#include "Camera.hpp" // Provides a global MVP matrix a.k.a. Camera
#include "Measurement.hpp" // Real-time performance metrics
#include "Model.hpp" // Alias structures for glTF data
#include "Vertex.hpp" // Hashable Vertex primitive, with position, colour and uvs
#include "Particle.hpp" // Hashable Particle primitive, with position and colour
#include "Light.hpp" // Hashable Light primitive, with position and emissive colour
#include "BufferStructs.hpp" // Structs for passing data to shaders

//============================== Application Defaults ==============================//
// Let's the CPU start working on the next frame before the GPU asks (higher values == latency, CPU too far ahead)
constexpr uint32_t MAX_FRAMES_IN_FLIGHT = 2;

// No. of particles to spawn
constexpr uint32_t PARTICLE_COUNT = 24;

// Screen resolution defaults
constexpr uint32_t WIDTH = 1440;
constexpr uint32_t HEIGHT = 900;

// Default asset paths
static char model_path[256] = "assets/sponza/Sponza.gltf";
static char slang_path[256] = "assets/shaders/shader.slang";
static char spirv_path[256] = "assets/shaders/shader.spv";
static char compute_slang_path[256] = "assets/shaders/compute.slang";
static char compute_spirv_path[256] = "assets/shaders/compute.spv";
static char postprocess_slang_path[256] = "assets/shaders/uv.slang";
static char postprocess_spirv_path[256] = "assets/shaders/uv.spv";

/*============================================================= Terminology =============================================================//
       Surface: an abstraction of an image, something a framebuffer can present to
         Queue: data highways with different capabilities i.e. graphics, compute, transfer
  Queue Family: groups of queues with the same capabilities
   Framebuffer:
     Swapchain: a list of framebuffers the size of which can be determined by how many frames ahead of the GPU you will allow the CPU to work on
          View: A data structure containing all the information necessary for accessing data buffers
    Descriptor:
      Pipeline: Define data processing flow on the GPU e.g. from glTF vertices to shaded meshes (one Pipeline per unique shader)
*/

/*====================================================== HOW TO BUILD A VULKAN APP ======================================================//
     A fairly lengthy process for the most boilerplate graphical application. 
     The CPU is smart, the GPU is dumb but a very willing and hard-working participant. If the CPU can create explicit instructions for tasks
     within the GPU's capabilities, the GPU will execute them as quickly as possible.

  1. Create a Vulkan Instance (loading required function pointers, etc.)
  2. (* Optional) Set up a debug message callback
  3. Create/Get an addressable surface
  4. Pick your preferred hardware
  5. Create a logical abstraction of chosen hardware that can interface with Vulkan, choosing which queues and queue families to use 
       A single queue capable of graphics and transfer is enough for simple rendering
  6. Set up a swapchain
  7. Create respective views for the swapchain members 
  8. Set up a command pool, which will be used to acquire command buffers to submit to your queues
  9. Allocate the command buffers from the command pool (~ one per pipeline per frame in flight, depends how you combine pipelines)
 10. Initialise the synchronisation objects you will use for recording the command buffers later
 11. (* Optional) Create the depth buffer (only needed if you intend to do drawcall-order-independent depth sorting)
 12. Define how information will be passed to shaders through DescriptorLayouts (which structs will be passed to which shader stage in a Pipeline)
 13. Construct your initial Pipelines (we don't need to know the exact data that will be passed in yet, just how it will be passed)
 14. Create the Uniform and Shader Storage Buffers (the arbitrary data structures referenced in the DescriptorLayouts)
 15. (* Optional) Load your assets
 16. Create the Vertex and Index buffers (unique vertices accessed at draw time by indices, static meshes can share the same vertex buffer with offset indices)
 17. Create descriptor pools (like command pools or families for descriptor sets, predefining commonalities and placing limitations on descriptor sets)
 18. Allocate for (on device) and construct explicit descriptor sets (At least one per pipeline per frame in flight)
    
     Now everything is in place to render!
*/
/*======================================================== RENDERING WITH VULKAN ========================================================//

*/

// Struct instead of Class as there is no inheritance, variable protection or even multiple instances
// All we need is run(), which calls everything else
struct App { 
  //======== initWindow ========//
  GLFWwindow* pWindow = nullptr;
  
  //======== initVulkan ========//
  // createInstance
  vk::raii::Context context;
  vk::raii::Instance instance = nullptr;
  
  // setupDebugMessenger
  vk::raii::DebugUtilsMessengerEXT debugMessenger = nullptr;
  
  // createSurface
  vk::raii::SurfaceKHR surface = nullptr;
  
  // pickPhysicalDevice
  std::vector<const char*> requiredDeviceExtensions = {
    vk::KHRSwapchainExtensionName,
    vk::KHRDynamicRenderingExtensionName,
    vk::KHRSpirv14ExtensionName,
    vk::KHRSynchronization2ExtensionName,
    vk::KHRCreateRenderpass2ExtensionName
  };
  vk::raii::PhysicalDevice physicalDevice = nullptr;
  
  // createLogicalDevice
  vk::raii::Device device = nullptr;
  uint32_t queueFamilyIndex = ~0U;
  vk::raii::Queue queue = nullptr;
  
  // createSwapChain
  struct SwapChainSupportDetails {
    vk::SurfaceCapabilitiesKHR capabilities;
    std::vector<vk::SurfaceFormatKHR> formats;
    std::vector<vk::PresentModeKHR> presentModes;
  };
  vk::Format swapChainSurfaceFormat;
  vk::Extent2D swapChainExtent;
  vk::raii::SwapchainKHR swapChain = nullptr;
  std::vector<vk::Image> swapChainImages;
  
  // createSwapChainImageViews
  std::vector<vk::raii::ImageView> swapChainImageViews;
  
  // createCommandPool
  vk::raii::CommandPool commandPool = nullptr;
  
  // createCommandBuffers
  std::vector<vk::raii::CommandBuffer> commandBuffers;
  
  // createComputeCommandBuffers
  std::vector<vk::raii::CommandBuffer> computeCommandBuffers;
  
  // createSyncObjects
  std::vector<vk::raii::Fence> inFlightFences;
  uint32_t currentFrame = 0;
  vk::raii::Semaphore timelineSemaphore = nullptr;
  uint64_t timelineValue = 0;
  std::vector<vk::raii::Semaphore> presentSemaphores;
  uint32_t semaphoreIndex = 0;
  
  // createDepthResources
  std::pair<vk::raii::Image, vk::raii::DeviceMemory> depthImage = std::pair(nullptr, nullptr);
  vk::raii::ImageView depthImageView = nullptr;
  
  // createDescriptorSetLayouts
  vk::raii::DescriptorSetLayout descriptorSetLayout = nullptr;
  vk::raii::DescriptorSetLayout computeDescriptorSetLayout = nullptr;
  
  // initSlang
  Slang::ComPtr<slang::IGlobalSession> globalSession = nullptr;
  
  // createPipelines
  std::pair<vk::raii::PipelineLayout, vk::raii::Pipeline> computePipeline = std::pair(nullptr, nullptr);
  std::pair<vk::raii::PipelineLayout, vk::raii::Pipeline> computeGraphicsPipeline = std::pair(nullptr, nullptr);
  std::pair<vk::raii::PipelineLayout, vk::raii::Pipeline> graphicsPipeline = std::pair(nullptr, nullptr);
  
  // createUniformBuffers
  std::vector<std::pair<vk::raii::Buffer, vk::raii::DeviceMemory>> mvpBuffers;
  std::vector<void*> mvpBuffersMapped;
  std::vector<std::pair<vk::raii::Buffer, vk::raii::DeviceMemory>> uniformBuffers;
  std::vector<void*> uniformBuffersMapped;
  std::vector<std::pair<vk::raii::Buffer, vk::raii::DeviceMemory>> shaderStorageBuffers;
  
  // loadGLTF
  cgltf_data* asset = nullptr;
  std::vector<Vertex> vertices;
  std::vector<Mesh> meshes;
  std::vector<Material> mats;
  std::vector<Material*> mats_DS;
  std::vector<std::pair<vk::raii::Image, vk::raii::DeviceMemory>> textureImages;
  std::mutex m;
  std::vector<vk::raii::ImageView> textureImageViews;
  vk::raii::Sampler textureSampler = nullptr;
  
  // createVertexBuffer
  std::pair<vk::raii::Buffer, vk::raii::DeviceMemory> vertexBuffer = std::pair(nullptr, nullptr);
  
  // createIndexBuffers (stored inside Material and Quad)

  // createDescriptorPools
  vk::raii::DescriptorPool graphicsDescriptorPool = nullptr;
  vk::raii::DescriptorPool computeDescriptorPool = nullptr;
  vk::raii::DescriptorPool imguiDescriptorPool = nullptr;
  
  // createDescriptorSets (stored inside Material and Quad)

  // createComputeDescriptorSets
  std::vector<vk::raii::DescriptorSet> computeDescriptorSets;

  //========= mainLoop =========//
  // drawFrame
  Metrics stats = {};
  Camera camera = {};
  bool framebufferResized = false;
  int64_t delta = 1;
  std::chrono::system_clock::time_point start = std::chrono::system_clock::now();

  // Extra objects
  Quad fullscreenQuad = {};
  std::vector<Light> lights;

  void Run();

  // run
  void InitWindow();
  void InitVulkan();
  void InitImGui();
  void MainLoop();
  void Cleanup();

  
  // initVulkan
  void CreateInstance();
  void SetupDebugMessenger();
  void CreateSurface();
  void PickPhysicalDevice();
  void CreateLogicalDevice();
  void CreateSwapChain();
  void CreateSwapChainImageViews();
  void CreateCommandPool();
  void CreateCommandBuffers();
  void CreateComputeCommandBuffers();
  void CreateSyncObjects();
  void CreateDepthResources();
  void CreateDescriptorSetLayouts();
  void InitSlang();
  void CreatePipelines();
  void CreateUniformBuffers();
  void CreateShaderStorageBuffers();
  void LoadGLTF(const std::filesystem::path& path);
  void CreateVertexBuffer();
  void CreateIndexBuffers();
  void CreateDescriptorPools();
  void CreateDescriptorSets();
  void CreateComputeDescriptorSets();

  // initImGui

  // mainLoop
  void DrawFrame();
  void RecreateSwapChain();

  // cleanup
  void CleanupSwapChain();

  // Pipelines
  [[nodiscard]] vk::raii::ShaderModule CreateShaderModule(const std::vector<char>& code) const;
  void CreateGraphicsPipeline();
  void CreatePostProcessPipeline();
  void CreateComputeGraphicsPipeline();
  void CreateComputePipeline();

  // loadGLTF (Asset loading)
  void LoadAsset(const char* path);
  void LoadTextures(const std::filesystem::path& parent_path);
  [[nodiscard]] std::pair<vk::raii::Image, vk::raii::DeviceMemory> CreateTextureImage(const char* texturePath, size_t idx);
  void TransitionImageLayout(
    const vk::raii::Image& image,
    vk::ImageLayout oldLayout, vk::ImageLayout newLayout,
    uint32_t mipLevels
  );
  void CreateTextureSampler();
  void LoadGeometry();


  // Shader Management
  void CompileShader(const char* src, const char* dst, bool reload);
  void ReloadShaders();

  // drawFrame
  void UpdateUniformBuffer(uint32_t imageIndex);
  void UpdateModelViewProjection(uint32_t imageIndex);
  void RecordComputeCommandBuffer();
  void RecordCommandBuffer(uint32_t imageIndex);
  void TransitionImageLayout(
    uint32_t imageIndex,
    vk::ImageLayout oldLayout,
    vk::ImageLayout newLayout,
    vk::AccessFlags2 srcAccessMask,
    vk::AccessFlags2 dstAccessMask,
    vk::PipelineStageFlags2 srcStageMask,
    vk::PipelineStageFlags2 dstStageMask
  );
  
  // Helper Functions
  [[nodiscard]] vk::Format FindDepthFormat() const;
  vk::Format FindSupportedFormat(
    const std::vector<vk::Format>& candidates, 
    vk::ImageTiling, 
    vk::FormatFeatureFlags features
  ) const;
  uint32_t FindMemoryType(uint32_t typeFilter, vk::MemoryPropertyFlags properties);

  void CreateImage(
    uint32_t width, uint32_t height, uint32_t mipLevels, 
    vk::Format format,
    vk::ImageTiling tiling,
    vk::ImageUsageFlags usage,
    vk::MemoryPropertyFlags properties,
    vk::raii::Image& image,
    vk::raii::DeviceMemory& imageMemory
  );

  [[nodiscard]] vk::raii::ImageView CreateImageView(
    const vk::raii::Image& image,
    vk::Format format,
    vk::ImageAspectFlags aspectFlags,
    uint32_t mipLevels
  ) const;
  
  std::unique_ptr<vk::raii::CommandBuffer> BeginSingleTimeCommands() const;
  void EndSingleTimeCommands(const vk::raii::CommandBuffer& commandBuffer) const;

  void CreateBuffer(
    vk::DeviceSize size,
    vk::BufferUsageFlags usage,
    vk::MemoryPropertyFlags properties,
    vk::raii::Buffer& buffer,
    vk::raii::DeviceMemory& bufferMemory
  );
  
  void CopyBuffer(
    const vk::raii::Buffer& srcBuffer,
    const vk::raii::Buffer& dstBuffer,
    vk::DeviceSize size
  );

  void CopyBufferToImage(
    const vk::raii::Buffer& buffer,
    const vk::raii::Image& image,
    uint32_t width, uint32_t height, uint32_t mipLevels,
    ktxTexture2* kTexture
  );
};

static const std::vector<char> ReadFile(const char* fileName);
static void WriteFile(const char* fileName, void const* code, size_t bytes);
#endif
