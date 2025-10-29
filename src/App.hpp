#ifndef _APP_HPP_
#define _APP_HPP_

//================== Standard Libraries =================//
#include <vector> // Dynamic arrays
#include <filesystem> // Platform-agnostic filepaths
#include <mutex> // Synchronisation object
#include <fstream>

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
#include "RadianceStructs.hpp" // Vertex2D + Scene, with position and emissive colour
#include "BufferStructs.hpp" // Structs for passing data to shaders

//============================== Application Defaults ==============================//
// Let's the CPU start working on the next frame before the GPU asks (higher values == latency, CPU too far ahead)
constexpr uint32_t MAX_FRAMES_IN_FLIGHT = 2;

constexpr uint32_t MAX_CASCADES = 4;
constexpr uint32_t WORKGROUP_SIZE[] = {8, 8};

#ifndef RES_DIV
  #define RES_DIV 1
#endif

// Screen resolution defaults
constexpr uint32_t WIDTH = 1440;
constexpr uint32_t HEIGHT = 900;

constexpr uint32_t LIGHT_COUNT = 8;

// Default asset paths
static char model_path[256] = "assets/sponza/Sponza.gltf";
static char common_path[] = "assets/shaders/common.slang";
#ifdef REFERENCE
static char slang_path[256] = "assets/shaders/reference.slang";
#elif RESTIR
static char slang_path[256] = "assets/shaders/restir.slang";
#elif RADIANCE_CASCADES
static char slang_path[256] = "assets/shaders/radiance_cascades.slang";
#else
static char slang_path[256] = "assets/shaders/raster.slang";
#endif
static char spirv_path[256] = "assets/shaders/shader.spv";

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
struct App { 
  //======== InitWindow ========//
  GLFWwindow* pWindow = nullptr;
  
  //======== InitVulkan ========//
  // createInstance
  vk::raii::Context context;
  vk::raii::Instance instance = nullptr;
  
  // SetupDebugMessenger
  vk::raii::DebugUtilsMessengerEXT debugMessenger = nullptr;
  
  // CreateSurface
  vk::raii::SurfaceKHR surface = nullptr;
  
  // PickPhysicalDevice
  std::vector<const char*> requiredDeviceExtensions = {
    vk::KHRSwapchainExtensionName,
    vk::KHRSpirv14ExtensionName,
    vk::KHRSynchronization2ExtensionName,
    vk::KHRCreateRenderpass2ExtensionName,
    vk::KHRAccelerationStructureExtensionName,
    vk::KHRBufferDeviceAddressExtensionName,
    vk::KHRDeferredHostOperationsExtensionName,
    vk::KHRRayQueryExtensionName
  };
  vk::raii::PhysicalDevice physicalDevice = nullptr;
  
  // CreateLogicalDevice
  vk::raii::Device device = nullptr;
  uint32_t queueFamilyIndex = ~0U;
  vk::raii::Queue queue = nullptr;
  
  // CreateSwapChain
  struct SwapChainSupportDetails {
    vk::SurfaceCapabilitiesKHR capabilities;
    std::vector<vk::SurfaceFormatKHR> formats;
    std::vector<vk::PresentModeKHR> presentModes;
  };
  vk::Format swapChainSurfaceFormat;
  vk::Extent2D swapChainExtent;
  vk::raii::SwapchainKHR swapChain = nullptr;
  std::vector<vk::Image> swapChainImages;
  
  // CreateSwapChainImageViews
  std::vector<vk::raii::ImageView> swapChainImageViews;
  
  // CreateDepthResources
  std::pair<vk::raii::Image, vk::raii::DeviceMemory> depthImage = std::pair(nullptr, nullptr);
  vk::raii::ImageView depthImageView = nullptr;
  
  // CreateCommandPool
  vk::raii::CommandPool commandPool = nullptr;
  
  // CreateCommandBuffers
  std::vector<vk::raii::CommandBuffer> drawCommandBuffers;
  std::vector<vk::raii::CommandBuffer> computeCommandBuffers;

  // CreateSyncObjects
  std::vector<vk::raii::Fence> inFlightFences;
  uint32_t currentFrame = 0;
  vk::raii::Semaphore timelineSemaphore = nullptr;
  uint64_t timelineValue = 0;
  uint32_t semaphoreIndex = 0;
  
  // CreateDescriptorSetLayouts
  vk::raii::DescriptorSetLayout descriptorSetLayoutGlobal = nullptr;
  vk::raii::DescriptorSetLayout descriptorSetLayoutMaterial = nullptr;
  vk::raii::DescriptorSetLayout computeDescriptorSetLayout = nullptr;
  
  // InitSlang
  Slang::ComPtr<slang::IGlobalSession> globalSession = nullptr;
  
  // CreatePipelines
  std::pair<vk::raii::PipelineLayout, vk::raii::Pipeline> computePipeline = std::pair(nullptr, nullptr);
  std::pair<vk::raii::PipelineLayout, vk::raii::Pipeline> graphicsPipeline = std::pair(nullptr, nullptr);
  
  const std::vector<vk::DynamicState> dynamicStates = { vk::DynamicState::eViewport, vk::DynamicState::eScissor };
  const vk::PipelineDynamicStateCreateInfo dynamicInfo {
    .dynamicStateCount = static_cast<uint32_t>(dynamicStates.size()), .pDynamicStates = dynamicStates.data()
  };
  const vk::PipelineViewportStateCreateInfo viewportInfo { .viewportCount = 1, .scissorCount = 1 };
  const vk::PipelineRasterizationStateCreateInfo rasterizerInfo {
    .depthClampEnable = vk::False,
    .rasterizerDiscardEnable = vk::False,
    .polygonMode = vk::PolygonMode::eFill,
    .cullMode = vk::CullModeFlagBits::eBack, .frontFace = vk::FrontFace::eCounterClockwise,
    .depthBiasEnable = vk::False, .depthBiasConstantFactor = 0.0f,
    .depthBiasClamp = 0.0f, .depthBiasSlopeFactor = 1.0f,
    .lineWidth = 1.0f
  };
  const vk::PipelineMultisampleStateCreateInfo multisamplingInfo {
    .rasterizationSamples = vk::SampleCountFlagBits::e1, .sampleShadingEnable = vk::False
  };
  const vk::PipelineColorBlendAttachmentState colorBlendAttachment {
    .blendEnable = vk::True,
    .srcColorBlendFactor = vk::BlendFactor::eSrcAlpha,
    .dstColorBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha,
    .colorBlendOp = vk::BlendOp::eAdd,
    .srcAlphaBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha,
    .dstAlphaBlendFactor = vk::BlendFactor::eZero,
    .alphaBlendOp = vk::BlendOp::eAdd,
    .colorWriteMask = vk::ColorComponentFlagBits::eR |
    vk::ColorComponentFlagBits::eG |
    vk::ColorComponentFlagBits::eB |
    vk::ColorComponentFlagBits::eA
  };
  const vk::PipelineColorBlendStateCreateInfo colorBlendInfo {
    .logicOpEnable = vk::False, .logicOp = vk::LogicOp::eCopy,
    .attachmentCount = 1, .pAttachments = &colorBlendAttachment
  };
  
  // CreateUniformBuffers
  std::vector<std::pair<vk::raii::Buffer, vk::raii::DeviceMemory>> mvpBuffers;
  std::vector<void*> mvpBuffersMapped;
  
  // LoadGLTF
  cgltf_data* asset = nullptr;
  std::vector<Vertex> vertices;
  std::vector<uint32_t> indices;
  std::mutex m;
  std::vector<std::pair<vk::raii::Image, vk::raii::DeviceMemory>> baseTextureImages;
  std::vector<vk::raii::ImageView> baseTextureImageViews;
  vk::raii::Sampler baseTextureSampler = nullptr;
  
  
  // CreateVertexBuffer
  std::pair<vk::raii::Buffer, vk::raii::DeviceMemory> vertexBuffer = std::pair(nullptr, nullptr);
  Triangle tri; Quad quad;
  std::pair<vk::raii::Buffer, vk::raii::DeviceMemory> triangleVertexBuffer = std::pair(nullptr, nullptr);
  std::pair<vk::raii::Buffer, vk::raii::DeviceMemory> quadVertexBuffer = std::pair(nullptr, nullptr);
  
  // CreateIndexBuffers
  std::pair<vk::raii::Buffer, vk::raii::DeviceMemory> indexBuffer = std::pair(nullptr, nullptr);
  std::pair<vk::raii::Buffer, vk::raii::DeviceMemory> triangleIndexBuffer = std::pair(nullptr, nullptr);
  std::pair<vk::raii::Buffer, vk::raii::DeviceMemory> quadIndexBuffer = std::pair(nullptr, nullptr);
  
  // CreateColourBuffers
  std::pair<vk::raii::Buffer, vk::raii::DeviceMemory> colourBuffer = std::pair(nullptr, nullptr);

  // CreateUVBuffers
  std::pair<vk::raii::Buffer, vk::raii::DeviceMemory> uvBuffer = std::pair(nullptr, nullptr);
  
  // CreateNrmBuffers
  std::pair<vk::raii::Buffer, vk::raii::DeviceMemory> nrmBuffer = std::pair(nullptr, nullptr);
  
  // CreateAccelerationStructures
  std::vector<std::pair<vk::raii::Buffer, vk::raii::DeviceMemory>> blasBuffers;
  std::vector<vk::raii::AccelerationStructureKHR> blasHandles;
  
  std::vector<vk::AccelerationStructureInstanceKHR> blasInstances;
  std::pair<vk::raii::Buffer, vk::raii::DeviceMemory> blasInstanceBuffer = std::pair(nullptr, nullptr);
  
  std::pair<vk::raii::Buffer, vk::raii::DeviceMemory> tlasBuffer = std::pair(nullptr, nullptr);
  std::pair<vk::raii::Buffer, vk::raii::DeviceMemory> tlasScratchBuffer = std::pair(nullptr, nullptr);
  vk::raii::AccelerationStructureKHR tlasHandle = nullptr;
  
  std::vector<SubMesh> submeshes;
  
  // CreateInstanceLUT
  std::vector<InstanceLUT> blasInstanceLUTs;
  std::pair<vk::raii::Buffer, vk::raii::DeviceMemory> blasInstanceLUTBuffer = std::pair(nullptr, nullptr);
  
  // CreateIndirectCommands
  std::vector<vk::DrawIndexedIndirectCommand> indirectCommands;
  std::pair<vk::raii::Buffer, vk::raii::DeviceMemory> indirectCommandsBuffer = std::pair(nullptr, nullptr);
  
  // CreateRenderTexture
  vk::Extent2D renderTextureExtent;
  std::pair<vk::raii::Image, vk::raii::DeviceMemory> renderTexture = std::pair(nullptr, nullptr);
  vk::raii::ImageView renderTextureView = nullptr;
  
  // CreateDescriptorPools
  vk::raii::DescriptorPool graphicsDescriptorPool = nullptr;
  vk::raii::DescriptorPool imguiDescriptorPool = nullptr;
  vk::raii::DescriptorPool computeDescriptorPool = nullptr;
  
  // CreateDescriptorSets
  std::vector<vk::raii::DescriptorSet> globalDescriptorSets;
  std::vector<vk::raii::DescriptorSet> materialDescriptorSets;
  std::vector<vk::raii::DescriptorSet> computeDescriptorSets;

  // CreateComputeDescriptorSets

  //========= MainLoop =========//
  // DrawFrame
  Metrics stats = {};
  Camera camera = {};
  bool framebufferResized = false;
  int64_t delta = 1;
  std::chrono::system_clock::time_point start = std::chrono::system_clock::now();
  float runtime;
  uint32_t frame = 0;

  //======= SetupMeasuring ======//
  std::ofstream f;
  std::string measurement_file_name;
  std::streambuf *old_clog;
  std::streambuf *file_buf;

  // Extra objects
  //RenderTarget gIOutput;
  Scene scene;
#ifndef _WIN32
  bool useWayland;
#endif
  float sunIntensity;
  glm::aligned_vec3 sunDir;
  glm::aligned_mat4x4 oldView;

  void Run();

  // Run
  void InitWindow();
  void InitVulkan();
  void InitImGui();
  void SetUpMeasuring();
  void MainLoop();
  void Cleanup();

  
  // InitVulkan
  void CreateInstance();
  void SetupDebugMessenger();
  void CreateSurface();
  void PickPhysicalDevice();
  void CreateLogicalDevice();
  void CreateSwapChain();
  void CreateSwapChainImageViews();
  void CreateDepthResources();
  void CreateCommandPool();
  void CreateCommandBuffers();
  void CreateSyncObjects();
  void LoadGLTF(const std::filesystem::path& path);
  void CreateDescriptorSetLayouts();
  void InitSlang();
  void CreatePipelines();
  void CreateUniformBuffers();
  void CreateVertexBuffers();
  void CreateIndexBuffers();
  void CreateColourBuffers();
  void CreateUVBuffers();
  void CreateNrmBuffers();
  void CreateAccelerationStructures();
  void CreateBLASInstanceLUTBuffer();
  void CreateIndirectCommands();
  void CreateRenderTexture();
  void CreateDescriptorPools();
  void CreateDescriptorSets();
  
  // InitImGui
  
  // MainLoop
  void DrawFrame();
  void RecreateSwapChain();
  
  // Cleanup
  void CleanupSwapChain();
  
  // Pipelines
  [[nodiscard]] vk::raii::ShaderModule CreateShaderModule(const std::vector<char>& code) const;
  void CreateGraphicsPipeline();
  void CreateComputePipeline();
  
  // LoadGLTF (Asset loading)
  void LoadAsset(const char* path);
  void LoadTextures(const std::filesystem::path& parent_path);
  void CreateTextureImage(const char* texturePath, size_t idx);
  void TransitionImageLayout(
    const vk::raii::Image& image,
    vk::ImageLayout oldLayout, vk::ImageLayout newLayout,
    uint32_t mipLevels
  );
  void CreateTextureSampler();
  void LoadGeometry();
  
  // CreateVertexBuffers
  std::pair<vk::raii::Buffer, vk::raii::DeviceMemory> CreateVertexBuffer(const std::vector<Vertex>& verts);
  
  // CreateIndexBuffers
  std::pair<vk::raii::Buffer, vk::raii::DeviceMemory> CreateIndexBuffer(
    const std::vector<uint32_t>& indices, vk::BufferUsageFlags flags);
    
  // CreateColourBuffers
  std::pair<vk::raii::Buffer, vk::raii::DeviceMemory> CreateColourBuffer(const std::vector<Vertex>& verts);
  
  // CreateUVBuffers
  std::pair<vk::raii::Buffer, vk::raii::DeviceMemory> CreateUVBuffer(const std::vector<Vertex>& verts);
  
  // CreateNrmBuffers
  std::pair<vk::raii::Buffer, vk::raii::DeviceMemory> CreateNrmBuffer(const std::vector<Vertex>& verts);
    
  // Shader Management
  void CompileShader(const char* src, const char* dst);
  void ReloadShaders();
  
  // DrawFrame
  //void UpdateUniformBuffer(uint32_t imageIndex);
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
    vk::ImageTiling tiling, vk::ImageUsageFlags usage,
    vk::MemoryPropertyFlags properties,
    vk::raii::Image& image, vk::raii::DeviceMemory& imageMemory
  );

  [[nodiscard]] vk::raii::ImageView CreateImageView(
    const vk::Image& image,
    vk::Format format,
    vk::ImageAspectFlags aspectFlags,
    uint32_t mipLevels
  ) const;
  
  vk::raii::CommandBuffer BeginSingleTimeCommands() const;
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
