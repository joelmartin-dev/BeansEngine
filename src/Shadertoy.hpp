#ifndef _SHADERTOY_HPP_
#define _SHADERTOY_HPP_

#include <vector> // resizable container
#include <filesystem> // for platform-agnostic paths

#include <slang/slang-com-helper.h>
#include <slang/slang-com-ptr.h>
#include <slang/slang.h>

// Windows has different calling conventions, vk_platform defines alternatives
#include <vulkan/vk_platform.h>

// volk loads function pointers at runtime directly from the driver
// rather than linking all of that information in the executable
#include <volk/volk.h> // Meta-loader for vulkan functions

#include <vulkan/vulkan_raii.hpp> // C++ bindings and RAII definitions for vulkan

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h> // for callback declarations, pWindow member

#include "Measurement.hpp"
#include "Vertex.hpp"
#include "Model.hpp"

constexpr uint32_t WIDTH = 1440;
constexpr uint32_t HEIGHT = 900;

constexpr uint32_t MAX_FRAMES_IN_FLIGHT = 2;

static char slang_path[256] = "assets/shaders/uv.slang";
static char spirv_path[256] = "assets/shaders/uv.spv";

// not array; vector allows implicit typing and contents are immutable
const std::vector validationLayers = {
  "VK_LAYER_KHRONOS_validation"
};

#ifdef NDEBUG
constexpr bool enableValidationLayers = false;
#else
constexpr bool enableValidationLayers = true;
#endif

#include <glm/glm.hpp>
#include <glm/gtx/type_aligned.hpp>

struct UniformBuffer {
  float iTime = 0.0f;
  glm::aligned_ivec2 iResolution;
  glm::aligned_ivec2 iMouse;
};

static bool framebufferResized = false;
static bool vertexStage = true, fragmentStage = true, computeStage = false;

class Shadertoy
{
  public:
  void run();
  private:
  // Class Variables
  GLFWwindow* pWindow = nullptr;

  Quad quad;

  Slang::ComPtr<slang::IGlobalSession> globalSession;

  vk::raii::Context context;
  vk::raii::Instance instance = nullptr;
  vk::raii::DebugUtilsMessengerEXT debugMessenger = nullptr;
  
  std::vector<const char*> requiredDeviceExtensions = {
    vk::KHRSwapchainExtensionName,
    vk::KHRDynamicRenderingExtensionName,
    vk::KHRSpirv14ExtensionName,
    vk::KHRSynchronization2ExtensionName,
    vk::KHRCreateRenderpass2ExtensionName
  };
  
  vk::raii::PhysicalDevice physicalDevice = nullptr;
  vk::raii::Device device = nullptr;
  
  uint32_t queueIndex = ~0;
  vk::raii::Queue queue = nullptr;

  vk::raii::SurfaceKHR surface = nullptr;
  vk::Format swapChainSurfaceFormat;
  vk::Extent2D swapChainExtent;
  vk::raii::SwapchainKHR swapChain = nullptr;
  std::vector<vk::Image> swapChainImages;
  
  std::vector<vk::raii::ImageView> swapChainImageViews;
  
  //vk::raii::DescriptorSetLayout descriptorSetLayout = nullptr;
  
  //std::pair<vk::raii::PipelineLayout, vk::raii::Pipeline> graphicsPipeline = std::pair(nullptr, nullptr);

  std::pair<vk::raii::Buffer, vk::raii::DeviceMemory> vertexBuffer = std::pair(nullptr, nullptr);

  vk::raii::CommandPool commandPool = nullptr;
  std::vector<vk::raii::CommandBuffer> commandBuffers;
  
  std::pair<vk::raii::Image, vk::raii::DeviceMemory> depthImage = std::pair(nullptr, nullptr);
  vk::raii::ImageView depthImageView = nullptr;
  
  std::vector<std::pair<vk::raii::Buffer, vk::raii::DeviceMemory>> uniformBuffers;
  std::vector<void*> uniformBuffersMapped;

  //vk::raii::DescriptorPool graphicsDescriptorPool = nullptr;
  vk::raii::DescriptorPool imguiDescriptorPool = nullptr;

  vk::raii::Semaphore timelineSemaphore = nullptr;
  std::vector<vk::raii::Semaphore> presentSemaphores;
  std::vector<vk::raii::Fence> inFlightFences;
  uint32_t currentFrame = 0;
  uint32_t semaphoreIndex = 0;

  uint64_t timelineValue = 0;
  int64_t delta = 1;
  std::chrono::system_clock::time_point start = std::chrono::system_clock::now();

  struct SwapChainSupportDetails {
    vk::SurfaceCapabilitiesKHR capabilities;
    std::vector<vk::SurfaceFormatKHR> formats;
    std::vector<vk::PresentModeKHR> presentModes;
  };

  // Class Functions
  void initWindow();
  
  void initSlang();

  void initVulkan();
  void createInstance();
  void setupDebugMessenger();
  void createSurface();
  void pickPhysicalDevice();
  void createLogicalDevice();
  void createSwapChain();
  void createSwapChainImageViews();
  void createDescriptorSetLayout();
  void createGraphicsPipeline();
  vk::raii::ShaderModule createShaderModule(const std::vector<char>& code) const;
  [[nodiscard]] vk::Format findDepthFormat() const;
  vk::Format findSupportedFormat(
    const std::vector<vk::Format>& candidates, 
    vk::ImageTiling, 
    vk::FormatFeatureFlags features
  ) const;
  void createCommandPool();
  void createDepthResources();
  void createImage(
    uint32_t width, uint32_t height, uint32_t mipLevels, 
    vk::Format format,
    vk::ImageTiling tiling,
    vk::ImageUsageFlags usage,
    vk::MemoryPropertyFlags properties,
    vk::raii::Image& image,
    vk::raii::DeviceMemory& imageMemory
  );
  uint32_t findMemoryType(uint32_t typeFilter, vk::MemoryPropertyFlags properties);
  [[nodiscard]] vk::raii::ImageView createImageView(
    const vk::raii::Image& image,
    vk::Format format,
    vk::ImageAspectFlags aspectFlags,
    uint32_t mipLevels
  ) const;
  void createBuffer(
    vk::DeviceSize size,
    vk::BufferUsageFlags usage,
    vk::MemoryPropertyFlags properties,
    vk::raii::Buffer& buffer,
    vk::raii::DeviceMemory& bufferMemory
  );
  void copyBuffer(
    vk::raii::Buffer& srcBuffer, vk::raii::Buffer& dstBuffer,
    vk::DeviceSize size
  );
  void createVertexBuffer();
  void createIndexBuffers();
  void createUniformBuffers();
  void createDescriptorPool();
  void createDescriptorSets();
  void createCommandBuffers();
  void createSyncObjects();
  
  void initImGui();

  void mainLoop();
  void recreateSwapChain();
  void cleanupSwapChain();
  void reloadShaders();
  void compileShader();
  void drawFrame();
  void updateUniformBuffer(uint32_t imageIndex);
  void transitionImageLayout(
    uint32_t imageIndex,
    vk::ImageLayout oldLayout,
    vk::ImageLayout newLayout,
    vk::AccessFlags2 srcAccessMask,
    vk::AccessFlags2 dstAccessMask,
    vk::PipelineStageFlags2 srcStageMask,
    vk::PipelineStageFlags2 dstStageMask
  );
  void recordCommandBuffer(uint32_t imageIndex);
  
  void cleanup();
};
#endif
