#include "App.hpp"

#include <iostream>

static VKAPI_ATTR vk::Bool32 VKAPI_CALL debugCallback(vk::DebugUtilsMessageSeverityFlagBitsEXT severity, vk::DebugUtilsMessageTypeFlagsEXT type, const vk::DebugUtilsMessengerCallbackDataEXT* pCallbackData, void*)
{
  if ((severity & vk::DebugUtilsMessageSeverityFlagBitsEXT::eError) == severity)
  {
    std::cerr << "validation layer: type " << to_string(type) << " msg: " << pCallbackData->pMessage << std::endl;
  }

  return vk::False;
}

#include <fstream>

static std::vector<char> readFile(const std::string& fileName)
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

#include <cstdint>
#include <limits>
#include <stdexcept>
#include <vector>
#include <algorithm>
#include <execution>
#include <chrono>
#include <memory>

#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#ifndef IMGUI_IMPL_VULKAN_USE_VOLK
#define IMGUI_IMPL_VULKAN_USE_VOLK
#endif
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_vulkan.h"

#include "ktxvulkan.h"

#define CGLTF_IMPLEMENTATION
#include "cgltf.h"

void App::run()
{
  initWindow();
  initVulkan();
  initImGui();
  mainLoop();
  cleanup();
}

void App::initWindow()
{
  if (glfwInit() != GLFW_TRUE)
  {
    throw std::runtime_error("failed to initialise GLFW!");
  }

  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
  glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

  pWindow = glfwCreateWindow(WIDTH, HEIGHT, "App", nullptr, nullptr);

  if (pWindow == nullptr)
  {
    throw std::runtime_error("failed to create GLFWwindow!");
  }

  glfwSetFramebufferSizeCallback(pWindow, framebufferResizeCallback);
  glfwSetKeyCallback(pWindow, key_callback);
  glfwSetCursorPosCallback(pWindow, cursor_pos_callback);
  
  glfwSetWindowUserPointer(pWindow, this);
  glfwSetInputMode(pWindow, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
  glfwSetInputMode(pWindow, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);
}

void App::initVulkan()
{
  createInstance();
  setupDebugMessenger();
  createSurface();
  pickPhysicalDevice();
  createLogicalDevice();
  createSwapChain();
  createSwapChainImageViews();
  createDescriptorSetLayout();
  createGraphicsPipeline();
  createCommandPool();
  createDepthResources();
  loadAsset(std::filesystem::path(model_path).make_preferred());
  loadTextures(std::filesystem::path(model_path).make_preferred());
  createTextureSampler();
  loadGeometry();
  createVertexBuffer();
  createIndexBuffers();
  createUniformBuffers();
  createDescriptorPools();
  createDescriptorSets();
  createCommandBuffers();
  createSyncObjects();
}

std::vector<const char*> getRequiredExtensions()
{
  uint32_t glfwExtensionCount = 0;
  auto glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

  std::vector extensions(glfwExtensions, glfwExtensions + glfwExtensionCount);
  if (enableValidationLayers)
  {
    extensions.push_back(vk::EXTDebugUtilsExtensionName);
  }

  return extensions;
}

void App::createInstance()
{
  if (volkInitialize() != VK_SUCCESS)
  {
    throw std::runtime_error("failed to initialise volk!");
  }

  constexpr vk::ApplicationInfo appInfo {
    .applicationVersion = VK_MAKE_VERSION(1, 0, 0),
    .pEngineName        = "Backend Engine",
    .engineVersion      = VK_MAKE_VERSION(1, 0, 0),
    .apiVersion         = vk::ApiVersion14
  };

  std::vector<char const*> requiredLayers;
  if (enableValidationLayers)
  {
    requiredLayers.assign(validationLayers.begin(), validationLayers.end());
  }

  auto layerProperties = context.enumerateInstanceLayerProperties();
  for (auto const& requiredLayer : requiredLayers)
  {
      if (std::ranges::none_of(layerProperties,
                                [requiredLayer](auto const& layerProperty)
                                { return strcmp(layerProperty.layerName, requiredLayer) == 0; }))
      {
          throw std::runtime_error("Required layer not supported: " + std::string(requiredLayer));
      }
  }

  auto requiredExtensions = getRequiredExtensions();

  auto extensionProperties = context.enumerateInstanceExtensionProperties();
  for (auto const& requiredExtension : requiredExtensions)
  {
    if (std::ranges::none_of(extensionProperties, [requiredExtension](auto const& extensionProperty)
      { return strcmp(extensionProperty.extensionName, requiredExtension) == 0; }
    ))
    {
      throw std::runtime_error("required extension not supported: " + std::string(requiredExtension));
    }
  }

  vk::InstanceCreateInfo createInfo {
    .pApplicationInfo = &appInfo,
    .enabledLayerCount = static_cast<uint32_t>(requiredLayers.size()),
    .ppEnabledLayerNames = requiredLayers.data(),
    .enabledExtensionCount = static_cast<uint32_t>(requiredExtensions.size()),
    .ppEnabledExtensionNames = requiredExtensions.data()
  };

  instance = vk::raii::Instance(context, createInfo);
  volkLoadInstance(static_cast<VkInstance>(*instance));
}

void App::setupDebugMessenger()
{
  if (!enableValidationLayers) return;

  vk::DebugUtilsMessageSeverityFlagsEXT severityFlags(vk::DebugUtilsMessageSeverityFlagBitsEXT::eVerbose | vk::DebugUtilsMessageSeverityFlagBitsEXT::eInfo | vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning | vk::DebugUtilsMessageSeverityFlagBitsEXT::eError);
  vk::DebugUtilsMessageTypeFlagsEXT messageTypeFlags(vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral | vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance | vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation);

  vk::DebugUtilsMessengerCreateInfoEXT debugUtilsMessengerCreateInfoEXT {
    .messageSeverity = severityFlags,
    .messageType = messageTypeFlags,
    .pfnUserCallback = &debugCallback
  };

  debugMessenger = instance.createDebugUtilsMessengerEXT(debugUtilsMessengerCreateInfoEXT);
}

void App::createSurface()
{
  VkSurfaceKHR _surface; // glfwCreateWindowSurface requires the struct defined in the C API
  if (glfwCreateWindowSurface(*instance, pWindow, nullptr, &_surface) != VK_SUCCESS)
  {
    throw std::runtime_error("failed to create window surface!");
  }
  surface = vk::raii::SurfaceKHR(instance, _surface);
}

void App::pickPhysicalDevice()
{
  std::vector<vk::raii::PhysicalDevice> physicalDevices = instance.enumeratePhysicalDevices();
  if (physicalDevices.empty())
  {
    throw std::runtime_error("failed to find any physical devices");
  }
  const auto devIter = std::ranges::find_if(physicalDevices, [&]( auto const& _physicalDevice)
    {
      vk::PhysicalDeviceProperties properties = _physicalDevice.getProperties();
      // Check if the device supports the Vulkan 1.3 API version
      bool supportsVulkan1_3 = properties.apiVersion >= VK_API_VERSION_1_3;
      bool supportsSamplerAnisotropy = properties.limits.maxSamplerAnisotropy >= 1.0f;
      
      // Check if any of the queue families support graphics operations
      auto queueFamilies = _physicalDevice.getQueueFamilyProperties();
      bool supportsGraphics = std::ranges::any_of(queueFamilies, [](auto const& qfp) { return !!(qfp.queueFlags & vk::QueueFlagBits::eGraphics); } );
      bool supportsCompute = std::ranges::any_of(queueFamilies, [](auto const& qfp) { return !!(qfp.queueFlags & vk::QueueFlagBits::eCompute); });


      // Check if all required device extensions are available
      auto availableDeviceExtensions = _physicalDevice.enumerateDeviceExtensionProperties();
      bool supportsAllRequiredExtensions = std::ranges::all_of(requiredDeviceExtensions, [&availableDeviceExtensions](auto const& requiredDeviceExtension)
        {
          return std::ranges::any_of(availableDeviceExtensions, [requiredDeviceExtension](auto const& availableDeviceExtension)
            { return strcmp(availableDeviceExtension.extensionName, requiredDeviceExtension) == 0; }
          );
        }
      );

      auto features = _physicalDevice.template getFeatures2<vk::PhysicalDeviceFeatures2, vk::PhysicalDeviceVulkan13Features, vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT>();
      bool supportsRequiredFeatures = features.template get<vk::PhysicalDeviceVulkan13Features>().dynamicRendering &&
                                      features.template get<vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT>().extendedDynamicState;            

      return supportsVulkan1_3 && supportsSamplerAnisotropy && supportsGraphics && supportsCompute && supportsAllRequiredExtensions && supportsRequiredFeatures;
    }
  );

  if (devIter != physicalDevices.end())
  {
    physicalDevice = *devIter;
  }
  else
  {
    throw std::runtime_error( "failed to find a suitable GPU!" );
  }
}

// Set up as single queue for all needs
uint32_t findQueueFamilies(const vk::raii::PhysicalDevice& _physicalDevice, const vk::SurfaceKHR& _surface)
{  
  std::vector<vk::QueueFamilyProperties> queueFamilyProperties = _physicalDevice.getQueueFamilyProperties();

  /* Example of how to get a potentially separate queue. For specific queues change the QueueFlagBits and variable names
  auto graphicsQueueFamilyProperties = std::find_if(queueFamilyProperties.begin(), queueFamilyProperties.end(), [](const auto& qfp)
    {
      return !!(qfp.queueFlags & vk::QueueFlagBits::eGraphics);
    }
  );
  auto graphicsIndex = static_cast<uint32_t>(std::distance(queueFamilyProperties.begin(), graphicsQueueFamilyProperties));
  */

  uint32_t queueFamilyIndex = ~0U; // like UINT_MAX

  for (uint32_t qfpIndex = 0; qfpIndex < queueFamilyProperties.size(); qfpIndex++)
  {
    if ((queueFamilyProperties[qfpIndex].queueFlags & (vk::QueueFlagBits::eGraphics | vk::QueueFlagBits::eCompute)) &&
      _physicalDevice.getSurfaceSupportKHR(qfpIndex, _surface))
    {
      queueFamilyIndex = qfpIndex;
      break;
    }
  }

  if (queueFamilyIndex == ~0U)
  {
    throw std::runtime_error("could not find a queue for graphics AND compute AND present!");
  }
  
  // return the index of the queue with a graphics queue family
  return queueFamilyIndex;
}

void App::createLogicalDevice()
{
  auto queueFamilyIndex = findQueueFamilies(physicalDevice, surface);

  vk::StructureChain<vk::PhysicalDeviceFeatures2, vk::PhysicalDeviceVulkan13Features, vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT> featureChain = {
    { .features = {.samplerAnisotropy = vk::True}},
    {.synchronization2 = true, .dynamicRendering = true},
    {.extendedDynamicState = true}
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
  graphicsIndex = queueFamilyIndex;
  computeIndex = queueFamilyIndex;

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

  return {
    std::clamp<uint32_t>(width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width),
    std::clamp<uint32_t>(height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height)
  };
}

void App::createSwapChain()
{
  auto surfaceCapabilities = physicalDevice.getSurfaceCapabilitiesKHR(surface);
  swapChainSurfaceFormat = chooseSwapSurfaceFormat(physicalDevice.getSurfaceFormatsKHR(surface));
  auto swapChainPresentMode = chooseSwapPresentMode(physicalDevice.getSurfacePresentModesKHR(surface));
  swapChainExtent = chooseSwapExtent(surfaceCapabilities, pWindow);
  uint32_t minImageCount = std::max(3u, surfaceCapabilities.minImageCount);
  // clamp to the maxImageCount so long as maxImageCount has a maximum and is < than minImageCount
  minImageCount = (surfaceCapabilities.maxImageCount > 0 && minImageCount > surfaceCapabilities.maxImageCount) ? surfaceCapabilities.maxImageCount : minImageCount;
  uint32_t imageCount = surfaceCapabilities.minImageCount + 1;
  if (surfaceCapabilities.maxImageCount > 0 && imageCount > surfaceCapabilities.maxImageCount)
    imageCount = surfaceCapabilities.maxImageCount;

  vk::SwapchainCreateInfoKHR swapChainCreateInfo {
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

void App::createSwapChainImageViews()
{
  swapChainImageViews.clear();

  vk::ImageViewCreateInfo imageViewCreateInfo {
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

void App::createDescriptorSetLayout()
{
  std::array bindings = {
    vk::DescriptorSetLayoutBinding(0, vk::DescriptorType::eUniformBuffer, 1, vk::ShaderStageFlagBits::eVertex, nullptr),
    vk::DescriptorSetLayoutBinding(1, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eFragment, nullptr),
  };

  vk::DescriptorSetLayoutCreateInfo layoutInfo {
    .bindingCount = static_cast<uint32_t>(bindings.size()),
    .pBindings = bindings.data()
  };

  descriptorSetLayout = vk::raii::DescriptorSetLayout(device, layoutInfo);
}

void App::createGraphicsPipeline()
{
  graphicsPipeline = std::pair(nullptr, nullptr);
  auto shaderModule = createShaderModule(readFile(shader_path));
  
  vk::PipelineShaderStageCreateInfo vertShaderModuleCreateInfo {
    .stage = vk::ShaderStageFlagBits::eVertex,
    .module = shaderModule,
    .pName = "vertMain"
  };
  vk::PipelineShaderStageCreateInfo fragShaderModuleCreateInfo {
    .stage = vk::ShaderStageFlagBits::eFragment,
    .module = shaderModule,
    .pName = "fragMain"
  };
  vk::PipelineShaderStageCreateInfo shaderStages[] = {vertShaderModuleCreateInfo, fragShaderModuleCreateInfo};
  
  auto bindingDescription = Vertex::getBindingDescription();
  auto attributesDescriptions = Vertex::getAttributeDescriptions();
  vk::PipelineVertexInputStateCreateInfo vertexInputInfo {
    .vertexBindingDescriptionCount = 1,
    .pVertexBindingDescriptions = &bindingDescription,
    .vertexAttributeDescriptionCount = static_cast<uint32_t>(attributesDescriptions.size()),
    .pVertexAttributeDescriptions = attributesDescriptions.data()
  };
  vk::PipelineInputAssemblyStateCreateInfo inputAssemblyInfo {
    .topology = vk::PrimitiveTopology::eTriangleList
  };

  std::vector<vk::DynamicState> dynamicStates = {
    vk::DynamicState::eViewport,
    vk::DynamicState::eScissor
  };

  vk::PipelineDynamicStateCreateInfo dynamicInfo {
    .dynamicStateCount = static_cast<uint32_t>(dynamicStates.size()),
    .pDynamicStates = dynamicStates.data()
  };

  vk::PipelineViewportStateCreateInfo viewportInfo {
    .viewportCount = 1,
    .scissorCount = 1
  };

  vk::PipelineRasterizationStateCreateInfo rasterizerInfo {
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

  vk::PipelineMultisampleStateCreateInfo multisamplingInfo {
    .rasterizationSamples = vk::SampleCountFlagBits::e1,
    .sampleShadingEnable = vk::False,
  };

  vk::PipelineDepthStencilStateCreateInfo depthStencil
  {
    .depthTestEnable = vk::True,
    .depthWriteEnable = vk::True,
    .depthCompareOp = vk::CompareOp::eLess,
    .depthBoundsTestEnable = vk::False,
    .stencilTestEnable = vk::False
  };

  vk::PipelineColorBlendAttachmentState colorBlendAttachment {
    .blendEnable = vk::False,
    .srcColorBlendFactor = vk::BlendFactor::eSrcAlpha,
    .dstColorBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha,
    .colorBlendOp = vk::BlendOp::eAdd,
    .srcAlphaBlendFactor = vk::BlendFactor::eSrcAlpha,
    .dstAlphaBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha,
    .alphaBlendOp = vk::BlendOp::eAdd,
    .colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA,
    
  };

  vk::PipelineColorBlendStateCreateInfo colorBlendInfo {
    .logicOpEnable = vk::False,
    .logicOp = vk::LogicOp::eCopy,
    .attachmentCount = 1,
    .pAttachments = &colorBlendAttachment
  };

  vk::PipelineLayoutCreateInfo pipelineLayoutInfo {
    .setLayoutCount = 1,
    .pSetLayouts = &*descriptorSetLayout,
    .pushConstantRangeCount = 0
  };
  graphicsPipeline.first = vk::raii::PipelineLayout(device, pipelineLayoutInfo);

  vk::Format depthFormat = findDepthFormat();
  vk::PipelineRenderingCreateInfo pipelineRenderingInfo = {
    .colorAttachmentCount = 1,
    .pColorAttachmentFormats = &swapChainSurfaceFormat,
    .depthAttachmentFormat = depthFormat
  };

  vk::GraphicsPipelineCreateInfo graphicsPipelineInfo {
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
    .layout = graphicsPipeline.first,
    .renderPass = nullptr,
  };

  graphicsPipeline.second = vk::raii::Pipeline(device, nullptr, graphicsPipelineInfo);
}

[[nodiscard]] vk::raii::ShaderModule App::createShaderModule(const std::vector<char>& code) const 
{
    vk::ShaderModuleCreateInfo createInfo {
      .codeSize = code.size() * sizeof(char),
      .pCode = reinterpret_cast<const uint32_t*>(code.data())
    };
    
    vk::raii::ShaderModule shaderModule{device, createInfo};
    
    return shaderModule;
};

[[nodiscard]] vk::Format App::findDepthFormat() const
{
  return findSupportedFormat(
    {vk::Format::eD32Sfloat, vk::Format::eD32SfloatS8Uint, vk::Format::eD24UnormS8Uint},
    vk::ImageTiling::eOptimal,
    vk::FormatFeatureFlagBits::eDepthStencilAttachment
  );
}

vk::Format App::findSupportedFormat(
  const std::vector<vk::Format>& candidates,
  vk::ImageTiling tiling,
  vk::FormatFeatureFlags features
) const
{
  auto formatIter = std::ranges::find_if(candidates, [&](auto const format)
    {
      vk::FormatProperties props = physicalDevice.getFormatProperties(format);
      return (((tiling == vk::ImageTiling::eLinear ) && ((props.linearTilingFeatures  & features) == features)) ||
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

void App::createCommandPool()
{
  vk::CommandPoolCreateInfo commandPoolInfo {
    .flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer,
    .queueFamilyIndex = graphicsIndex,
  };
  commandPool = vk::raii::CommandPool(device, commandPoolInfo);
}

void App::createDepthResources()
{
  vk::Format depthFormat = findDepthFormat();
  createImage(
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
  depthImageView = createImageView(depthImage.first, depthFormat, vk::ImageAspectFlagBits::eDepth, 1);
}

void App::createImage(
  uint32_t width, uint32_t height, uint32_t mipLevels,
  vk::Format format,
  vk::ImageTiling tiling,
  vk::ImageUsageFlags usage,
  vk::MemoryPropertyFlags properties,
  vk::raii::Image& image,
  vk::raii::DeviceMemory& imageMemory
)
{
  vk::ImageCreateInfo imageInfo {
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

  image = vk::raii::Image( device, imageInfo );

  vk::MemoryRequirements memRequirements = image.getMemoryRequirements();
  vk::MemoryAllocateInfo allocInfo{
    .allocationSize = memRequirements.size, 
    .memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits, properties)
  };
  imageMemory = vk::raii::DeviceMemory(device, allocInfo);
  image.bindMemory(imageMemory, 0);
}

uint32_t App::findMemoryType(uint32_t typeFilter, vk::MemoryPropertyFlags properties)
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

[[nodiscard]] vk::raii::ImageView App::createImageView(
  const vk::raii::Image& image,
  vk::Format format,
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

void App::loadAsset(std::filesystem::path path)
{
  cgltf_options options = {};
  asset = NULL;
  if (cgltf_parse_file(&options, path.generic_string().c_str(), &asset) != cgltf_result_success)
  {
    throw std::runtime_error(std::string("failed to load ").append(path.generic_string()));
    cgltf_free(asset);
  }

  if (cgltf_validate(asset) != cgltf_result_success)
  {
    throw std::runtime_error(std::string("failed to validate ").append(path.generic_string()));
    cgltf_free(asset);
  }

  if (cgltf_load_buffers(&options, asset, path.generic_string().c_str()) != cgltf_result_success)
  {
    throw std::runtime_error(std::string("failed to load buffers for ").append(path.generic_string()));
    cgltf_free(asset);
  }
}

void App::loadTextures(std::filesystem::path path)
{
  textureImages.clear();
  textureImageViews.clear();

  for (size_t i = 0; i < asset->materials_count; i++)
  {
    std::filesystem::path uri = asset->materials[i].pbr_metallic_roughness.base_color_texture.texture->basisu_image->uri;
    const std::string texturePath = (path.parent_path() / uri).string();
    textureImages.emplace_back(createTextureImage(texturePath));
  }
}

[[nodiscard]] std::pair<vk::raii::Image, vk::raii::DeviceMemory> App::createTextureImage(const std::string texturePath)
{
  ktxTexture2* kTexture;
  auto result = ktxTexture2_CreateFromNamedFile(texturePath.c_str(), KTX_TEXTURE_CREATE_NO_FLAGS, &kTexture);

  if (result != KTX_SUCCESS)
  {
    throw std::runtime_error(std::string("failed to load ktx texture image: ").append(texturePath));
  }

  if (ktxTexture2_NeedsTranscoding(kTexture))
  {
    ktx_transcode_fmt_e tf;
    auto deviceFeatures = physicalDevice.getFeatures();
    if (deviceFeatures.textureCompressionBC)
    {
      tf = KTX_TTF_BC3_RGBA;
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

  createBuffer(
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
  
  createImage(texWidth, texHeight, mipLevels, textureFormat, vk::ImageTiling::eOptimal, vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled, vk::MemoryPropertyFlagBits::eDeviceLocal, textureImage, textureImageMemory);
  
  transitionImageLayout(textureImage, vk::ImageLayout::eUndefined, vk::ImageLayout::eTransferDstOptimal, mipLevels);
  copyBufferToImage(stagingBuffer, textureImage, texWidth, texHeight, mipLevels, kTexture);
  transitionImageLayout(textureImage, vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eShaderReadOnlyOptimal, mipLevels);
  
  ktxTexture2_Destroy(kTexture);
  
  createTextureImageView(textureImage, textureFormat, mipLevels);
  return std::pair(std::move(textureImage), std::move(textureImageMemory));
}

void App::createBuffer(
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
    .memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits, properties)
  };
  bufferMemory = vk::raii::DeviceMemory(device, allocInfo);
  buffer.bindMemory(*bufferMemory, 0);
}

void App::transitionImageLayout(
  const vk::raii::Image& image,
  vk::ImageLayout oldLayout, vk::ImageLayout newLayout,
  uint32_t mipLevels
)
{
  const auto commandBuffer = beginSingleTimeCommands();

  vk::ImageMemoryBarrier barrier {
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
  endSingleTimeCommands(*commandBuffer);
}

std::unique_ptr<vk::raii::CommandBuffer> App::beginSingleTimeCommands()
{
  vk::CommandBufferAllocateInfo allocInfo {
    .commandPool = commandPool,
    .level = vk::CommandBufferLevel::ePrimary,
    .commandBufferCount = 1
  };
  std::unique_ptr<vk::raii::CommandBuffer> commandBuffer = std::make_unique<vk::raii::CommandBuffer>(std::move(device.allocateCommandBuffers(allocInfo).front()));

  vk::CommandBufferBeginInfo beginInfo {
    .flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit
  };
  commandBuffer->begin(beginInfo);

  return commandBuffer;
}

void App::endSingleTimeCommands(const vk::raii::CommandBuffer& commandBuffer) const
{
  commandBuffer.end();

  vk::SubmitInfo submitInfo {
    .commandBufferCount = 1,
    .pCommandBuffers = &*commandBuffer
  };
  queue.submit(submitInfo, nullptr);
  queue.waitIdle();
}

void App::copyBufferToImage(
  const vk::raii::Buffer& buffer,
  const vk::raii::Image& image,
  uint32_t width, uint32_t height, uint32_t mipLevels,
  ktxTexture2* kTexture
)
{
  std::unique_ptr<vk::raii::CommandBuffer> commandBuffer = beginSingleTimeCommands();
  
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

  endSingleTimeCommands(*commandBuffer);
}

void App::createTextureImageView(const vk::raii::Image& image, vk::Format format, uint32_t mipLevels)
{
  textureImageViews.emplace_back(createImageView(image, format, vk::ImageAspectFlagBits::eColor, mipLevels));
}

void App::createTextureSampler()
{
  vk::PhysicalDeviceProperties properties = physicalDevice.getProperties();
  vk::SamplerCreateInfo samplerInfo {
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

void App::loadGeometry()
{
  for (cgltf_size meshIt = 0; meshIt < asset->meshes_count; meshIt++)
  {
    meshes.emplace_back();

    auto m = &asset->meshes[meshIt];

    for (cgltf_size primIt = 0; primIt < m->primitives_count; primIt++)
    {
      prims.emplace_back();

      auto p = &m->primitives[primIt];

      uint32_t v_offset = static_cast<uint32_t>(vertices.size());
      //uint32_t i_offset = static_cast<uint32_t>(indices.size());

      auto indexAccessor = p->indices;

      cgltf_size num_idx_elems = cgltf_num_components(indexAccessor->type) * indexAccessor->count;
      uint32_t* unpacked_indices = static_cast<uint32_t*>(malloc(num_idx_elems * sizeof(uint32_t)));

      if (unpacked_indices)
      {
        cgltf_size written_uints = cgltf_accessor_unpack_indices(indexAccessor, unpacked_indices, static_cast<cgltf_size>(sizeof(uint32_t)), num_idx_elems);
        //std::clog << "Unpacked " << written_uints << " indices, ";
        prims.back().indices.resize(indexAccessor->count);

        for (cgltf_size i = 0; i < written_uints; i++)
        {
          prims.back().indices[i] = unpacked_indices[i] + v_offset;
        }
        free(unpacked_indices);
      }


      for (cgltf_size i = 0; i < asset->materials_count; i++)
      {
        if (strcmp(p->material->pbr_metallic_roughness.base_color_texture.texture->basisu_image->uri, asset->materials[i].pbr_metallic_roughness.base_color_texture.texture->basisu_image->uri) == 0)
        {
          prims.back().imageViewIndex = i;
          break;
        }
      }

      cgltf_accessor* posAccessor = NULL; cgltf_accessor *uvAccessor = NULL;
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

      vertices.resize(vertices.size() + posAccessor->count);
      for (size_t i = 0; i < posAccessor->count; i++)
      {
        vertices[v_offset + i].pos = positions[i] * scale;
        vertices[v_offset + i].texCoord = uvs[i];
      }
    }
  }
  cgltf_free(asset);
}

void App::copyBuffer(
  vk::raii::Buffer& srcBuffer, vk::raii::Buffer& dstBuffer,
  vk::DeviceSize size
)
{
  vk::CommandBufferAllocateInfo allocInfo {
    .commandPool = commandPool,
    .level = vk::CommandBufferLevel::ePrimary, 
    .commandBufferCount = 1
  };
  vk::raii::CommandBuffer commandCopyBuffer = std::move(device.allocateCommandBuffers(allocInfo).front());
  commandCopyBuffer.begin(vk::CommandBufferBeginInfo{.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit});
  commandCopyBuffer.copyBuffer(*srcBuffer, *dstBuffer, vk::BufferCopy{.size = size});
  commandCopyBuffer.end();
  queue.submit(vk::SubmitInfo{.commandBufferCount = 1, .pCommandBuffers = &*commandCopyBuffer}, nullptr);
  queue.waitIdle();
}

void App::createVertexBuffer()
{
  vk::DeviceSize bufferSize = sizeof(vertices[0]) * vertices.size();
  vk::raii::Buffer stagingBuffer({});
  vk::raii::DeviceMemory stagingBufferMemory({});

  createBuffer(
    bufferSize,
    vk::BufferUsageFlagBits::eTransferSrc,
    vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent,
    stagingBuffer,
    stagingBufferMemory
  );

  void* dataStaging = stagingBufferMemory.mapMemory(0, bufferSize);
  memcpy(dataStaging, vertices.data(), bufferSize);
  stagingBufferMemory.unmapMemory();

  createBuffer(
    bufferSize,
    vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eTransferDst,
    vk::MemoryPropertyFlagBits::eDeviceLocal,
    vertexBuffer.first,
    vertexBuffer.second
  );

  copyBuffer(stagingBuffer, vertexBuffer.first, bufferSize);
}

void App::createIndexBuffers()
{
  for (auto& p : prims)
  {
    vk::DeviceSize bufferSize = sizeof(p.indices[0]) * p.indices.size();
    vk::raii::Buffer stagingBuffer({});
    vk::raii::DeviceMemory stagingBufferMemory({});

    createBuffer(
      bufferSize,
      vk::BufferUsageFlagBits::eTransferSrc,
      vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent,
      stagingBuffer,
      stagingBufferMemory
    );

    void* dataStaging = stagingBufferMemory.mapMemory(0, bufferSize);
    memcpy(dataStaging, p.indices.data(), (size_t) bufferSize);
    stagingBufferMemory.unmapMemory();

    createBuffer(
      bufferSize,
      vk::BufferUsageFlagBits::eIndexBuffer | vk::BufferUsageFlagBits::eTransferDst,
      vk::MemoryPropertyFlagBits::eDeviceLocal,
      p.indexBuffer.first,
      p.indexBuffer.second
    );

    copyBuffer(stagingBuffer, p.indexBuffer.first, bufferSize);
  }
}

void App::createUniformBuffers()
{
  uniformBuffers.clear();
  //uniformBuffersMemory.clear();
  uniformBuffersMapped.clear();

  for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
  {
    vk::DeviceSize bufferSize = sizeof(MVP);
    vk::raii::Buffer buffer({});
    vk::raii::DeviceMemory bufferMemory({});
    createBuffer(bufferSize, vk::BufferUsageFlagBits::eUniformBuffer, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent, buffer, bufferMemory);
    uniformBuffers.emplace_back(std::pair(std::move(buffer), std::move(bufferMemory)));
    uniformBuffersMapped.emplace_back(uniformBuffers[i].second.mapMemory(0, bufferSize));
  }
}


void App::createDescriptorPools()
{
  std::array poolSizes = {
    vk::DescriptorPoolSize(vk::DescriptorType::eUniformBuffer, MAX_FRAMES_IN_FLIGHT * static_cast<uint32_t>(prims.size())),
    vk::DescriptorPoolSize(vk::DescriptorType::eCombinedImageSampler, MAX_FRAMES_IN_FLIGHT * static_cast<uint32_t>(prims.size()))
  };

  vk::DescriptorPoolCreateInfo poolInfo {
    .flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
    .maxSets = MAX_FRAMES_IN_FLIGHT * static_cast<uint32_t>(prims.size()),
    .poolSizeCount = static_cast<uint32_t>(poolSizes.size()),
    .pPoolSizes = poolSizes.data()
  };

  descriptorPool = vk::raii::DescriptorPool(device, poolInfo);

  // Create descriptor pool for ImGui
  std::array imguiPoolSizes = {
    vk::DescriptorPoolSize(vk::DescriptorType::eSampler, 10),
    vk::DescriptorPoolSize(vk::DescriptorType::eCombinedImageSampler, 10),
    vk::DescriptorPoolSize(vk::DescriptorType::eSampledImage, 10),
    vk::DescriptorPoolSize(vk::DescriptorType::eStorageImage, 10),
    vk::DescriptorPoolSize(vk::DescriptorType::eUniformTexelBuffer, 10),
    vk::DescriptorPoolSize(vk::DescriptorType::eStorageTexelBuffer, 10),
    vk::DescriptorPoolSize(vk::DescriptorType::eUniformBuffer, 10),
    vk::DescriptorPoolSize(vk::DescriptorType::eStorageBuffer, 10),
    vk::DescriptorPoolSize(vk::DescriptorType::eUniformBufferDynamic, 10),
    vk::DescriptorPoolSize(vk::DescriptorType::eStorageBufferDynamic, 10),
    vk::DescriptorPoolSize(vk::DescriptorType::eInputAttachment, 10)
  };

  vk::DescriptorPoolCreateInfo imguiPoolInfo {
    .flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
    .maxSets = 1000 * static_cast<uint32_t>(imguiPoolSizes.size()),
    .poolSizeCount = static_cast<uint32_t>(imguiPoolSizes.size()),
    .pPoolSizes = imguiPoolSizes.data()
  };

  imguiDescriptorPool = vk::raii::DescriptorPool(device, imguiPoolInfo);
}

void App::createDescriptorSets()
{
  for (auto& p : prims)
  {
    std::vector<vk::DescriptorSetLayout> layouts(MAX_FRAMES_IN_FLIGHT, *descriptorSetLayout);
    vk::DescriptorSetAllocateInfo allocInfo {
      .descriptorPool = static_cast<vk::DescriptorPool>(descriptorPool),
      .descriptorSetCount = static_cast<uint32_t>(layouts.size()),
      .pSetLayouts = layouts.data(),
    };
    p.descriptorSets.clear();
    p.descriptorSets = device.allocateDescriptorSets(allocInfo);

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
    {
      vk::DescriptorBufferInfo bufferInfo {
        .buffer = static_cast<vk::Buffer>(uniformBuffers[i].first),
        .offset = 0,
        .range = sizeof(MVP)
      };

      vk::DescriptorImageInfo imageInfo {
        .sampler = static_cast<vk::Sampler>(textureSampler),
        .imageView = static_cast<vk::ImageView>(textureImageViews[p.imageViewIndex]),
        .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal
      };

      std::array descriptorWrites = {
        vk::WriteDescriptorSet{
          .dstSet = static_cast<vk::DescriptorSet>(p.descriptorSets[i]),
          .dstBinding = 0,
          .dstArrayElement = 0,
          .descriptorCount = 1,
          .descriptorType = vk::DescriptorType::eUniformBuffer,
          .pBufferInfo = &bufferInfo
        },
        vk::WriteDescriptorSet{
          .dstSet = static_cast<vk::DescriptorSet>(p.descriptorSets[i]),
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

void App::createCommandBuffers()
{
  commandBuffers.clear();

  vk::CommandBufferAllocateInfo allocInfo {
    .commandPool = commandPool,
    .level = vk::CommandBufferLevel::ePrimary,
    .commandBufferCount = MAX_FRAMES_IN_FLIGHT
  };

  commandBuffers = vk::raii::CommandBuffers(device, allocInfo);
}

void App::createSyncObjects()
{
  presentCompleteSemaphores.clear();
  renderFinishedSemaphores.clear();
  graphicsInFlightFences.clear();

  for (size_t i = 0; i < swapChainImages.size(); i++)
  { 
    presentCompleteSemaphores.emplace_back(device, vk::SemaphoreCreateInfo());
    renderFinishedSemaphores.emplace_back(device, vk::SemaphoreCreateInfo());
  }

  computeInFlightFences.clear();
  computeFinishedSemaphores.clear();

  for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
  {    
    graphicsInFlightFences.emplace_back(device, vk::FenceCreateInfo {.flags = vk::FenceCreateFlagBits::eSignaled});
    computeFinishedSemaphores.emplace_back(device, vk::SemaphoreCreateInfo());
    computeInFlightFences.emplace_back(device, vk::FenceCreateInfo{ .flags = vk::FenceCreateFlagBits::eSignaled });
  }
}

void App::initImGui()
{
  if (!static_cast<VkDevice>(*device))
  {
    throw std::runtime_error("ImGui not working with device!");
  }

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO(); (void)io;
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

  if (!ImGui_ImplGlfw_InitForVulkan(pWindow, true))
  {
    throw std::runtime_error("failed to initialise ImGui GLFW backend!");
  }


  ImGui_ImplVulkan_InitInfo initInfo = {};
    initInfo.ApiVersion = vk::ApiVersion14;
    initInfo.Instance = static_cast<VkInstance>(*instance);
    initInfo.PhysicalDevice = static_cast<VkPhysicalDevice>(*physicalDevice);
    initInfo.Device = static_cast<VkDevice>(*device);
    initInfo.QueueFamily = graphicsIndex;
    initInfo.Queue = static_cast<VkQueue>(*queue);
    initInfo.DescriptorPool = static_cast<VkDescriptorPool>(*imguiDescriptorPool);
    initInfo.MinImageCount = static_cast<uint32_t>(swapChainImages.size());
    initInfo.ImageCount = static_cast<uint32_t>(swapChainImages.size());
    initInfo.MSAASamples = VkSampleCountFlagBits::VK_SAMPLE_COUNT_1_BIT;
    initInfo.UseDynamicRendering = true;

    vk::PipelineRenderingCreateInfo imguiPipelineInfo {
      .colorAttachmentCount = 1,
      .pColorAttachmentFormats = reinterpret_cast<const vk::Format *>(&swapChainSurfaceFormat),
      .depthAttachmentFormat = findDepthFormat()
    };

    initInfo.PipelineRenderingCreateInfo = imguiPipelineInfo;

  if (ImGui_ImplVulkan_Init(&initInfo) != true)
  {
    throw std::runtime_error("failed to initialise ImGuiImplVulkan!");
  }
}

void App::mainLoop()
{
  static bool showWindow = true;
  float deltaMultiplier = 1000000.0f;
  for (auto& p : prims)
  {
    stats.tris += static_cast<uint32_t>(p.indices.size());
  }
  stats.tris /= 3;
  camera.update(1.0f);
  double xpos, ypos;
  std::chrono::system_clock::time_point start = std::chrono::system_clock::now();
  std::chrono::system_clock::time_point end = std::chrono::system_clock::time_point::max();
  long long delta = 1;


  while (glfwWindowShouldClose(pWindow) != GLFW_TRUE)
  {
    glfwPollEvents();
    if (framebufferResized)
    {
        framebufferResized = false;
        recreateSwapChain();
    }

    if (hotReload)
    {
        hotReload = false;
        reloadShaders();
    }

    glfwGetCursorPos(pWindow, &xpos, &ypos);
    
    camera.update(static_cast<double>(delta) / static_cast<double>(deltaMultiplier));
    if (xpos == camera.oldXpos)
      camera.deltaYaw = 0.0f;
    if (ypos == camera.oldYpos)
      camera.deltaPitch = 0.0f;

    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
    auto pi = glm::pi<double>();
    auto neg_pi = -pi;
    auto pi_2 = glm::pi<double>() / 2.0;
    auto neg_pi_2 = -pi_2;

    {
      ImGui::Begin("Delta Frametime", &showWindow, ImGuiWindowFlags_AlwaysAutoResize);
      ImGui::Text("%llius", stats.frametime);
      ImGui::Text("%i tris", stats.tris);
      ImGui::Spacing();
      ImGui::SliderFloat("X", &camera.position.x, -30.0f, 30.0f);
      ImGui::SliderFloat("Y", &camera.position.y, -30.0f, 30.0f);
      ImGui::SliderFloat("Z", &camera.position.z, -30.0f, 30.0f);
      ImGui::SliderFloat("Move Speed", &camera.moveSpeed, 0.01f, 100.0f);
      ImGui::Spacing();
      ImGui::SliderScalar("Pitch", ImGuiDataType_Double, &camera.pitch, &neg_pi_2, &pi_2);
      ImGui::SliderScalar("Yaw", ImGuiDataType_Double, &camera.yaw, &neg_pi, &pi);
      ImGui::SliderFloat("Pitch Speed", &camera.pitchSpeed, 0.01f, 40.0f);
      ImGui::SliderFloat("Yaw Speed", &camera.yawSpeed, 0.01f, 40.0f);
      ImGui::Spacing();
      ImGui::SliderFloat("FOV", &camera.fov, 20.0f, 170.0f);
      ImGui::SliderFloat("FOV Speed", &camera.fovSpeed, 0.01f, 1000.0f);
      ImGui::Spacing();
      ImGui::SliderFloat("Shift Speed", &camera.shiftSpeed, 0.01f, 4.0f);
      ImGui::InputFloat("Delta Mult", &deltaMultiplier);
      ImGui::Spacing();
      ImGui::InputText("Model Path", model_path, IM_ARRAYSIZE(model_path));
      ImGui::InputText("Shader Path", shader_path, IM_ARRAYSIZE(shader_path));
      ImGui::End();
    }

    ImGui::Render();
    auto deltaStart = std::chrono::system_clock::now();
    
    // Lock to 60fps
    while (std::chrono::duration_cast<std::chrono::microseconds>(end - start).count() < 16667)
    {
      end = std::chrono::system_clock::now();
    }

    start = std::chrono::system_clock::now();
    drawFrame();
    end = std::chrono::system_clock::now();
    stats.frametime = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
    delta = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
  }
  device.waitIdle();
}

void App::recreateSwapChain()
{
    int width, height;
    glfwGetFramebufferSize(pWindow, &width, &height);
    while (width == 0 || height == 0)
    {
        glfwGetFramebufferSize(pWindow, &width, &height);
        glfwWaitEvents();
    }

    device.waitIdle();

    cleanupSwapChain();

    createSwapChain();
    createSwapChainImageViews();
    createDepthResources();
}

void App::cleanupSwapChain()
{
    swapChainImageViews.clear();
    swapChain = nullptr;
}

void App::reloadShaders()
{
    device.waitIdle();
    auto f = fopen(shader_path, "r");
    if (f == NULL)
    {
      return;
    }
    fclose(f);
    createGraphicsPipeline();
}

void App::drawFrame()
{
  //{
  //  // COMPUTE
  //  while (vk::Result::eTimeout == device.waitForFences(*computeInFlightFences[currentFrame], vk::True, UINT64_MAX))
  //    ;
  //  updateUniformBuffer(currentFrame);
  //  device.resetFences(*computeInFlightFences[currentFrame]);
  //  computeCommandBuffers[currentFrame].reset();
  //  recordComputeCommandBuffer();

  //  const vk::SubmitInfo submitInfo({}, {}, { *computeCommandBuffers[currentFrame] }, { *computeFinishedSemaphores[currentFrame] });
  //  queue.submit(submitInfo, *computeInFlightFences[currentFrame]);
  //}
  {
    // GRAPHICS
    while (vk::Result::eTimeout == device.waitForFences(*graphicsInFlightFences[currentFrame], vk::True, UINT64_MAX))
      ;

    auto [result, imageIndex] = swapChain.acquireNextImage(UINT64_MAX, *presentCompleteSemaphores[semaphoreIndex], nullptr);

    if (result == vk::Result::eErrorOutOfDateKHR || framebufferResized)
    {
      framebufferResized = false;
      recreateSwapChain();
      return;
    }
    else if (result != vk::Result::eSuccess && result != vk::Result::eSuboptimalKHR)
    {
      throw std::runtime_error("failed to acquire swap chain image!");
    }

    updateModelViewProjection(currentFrame);

    device.resetFences(*graphicsInFlightFences[currentFrame]);
    commandBuffers[currentFrame].reset();

    recordCommandBuffer(imageIndex);

    vk::PipelineStageFlags waitDestinationStageMask(vk::PipelineStageFlagBits::eColorAttachmentOutput);

    const vk::SubmitInfo submitInfo{
      .waitSemaphoreCount = 1,
      .pWaitSemaphores = &*presentCompleteSemaphores[semaphoreIndex],
      .pWaitDstStageMask = &waitDestinationStageMask,
      .commandBufferCount = 1,
      .pCommandBuffers = &*commandBuffers[currentFrame],
      .signalSemaphoreCount = 1,
      .pSignalSemaphores = &*renderFinishedSemaphores[imageIndex]
    };

    queue.submit(submitInfo, *graphicsInFlightFences[currentFrame]);

    const vk::PresentInfoKHR presentInfo{
      .waitSemaphoreCount = 1,
      .pWaitSemaphores = &*renderFinishedSemaphores[imageIndex],
      .swapchainCount = 1,
      .pSwapchains = &*swapChain,
      .pImageIndices = &imageIndex,
    };

    result = queue.presentKHR(presentInfo);
    if (result == vk::Result::eErrorOutOfDateKHR || result == vk::Result::eSuboptimalKHR || framebufferResized)
    {
      framebufferResized = false;
      recreateSwapChain();
    }
    else if (result != vk::Result::eSuccess)
    {
      throw std::runtime_error("failed to present swap chain image!");
    }

    semaphoreIndex = (semaphoreIndex + 1) % presentCompleteSemaphores.size();
    currentFrame = (currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
  }
}

void App::updateModelViewProjection(uint32_t imageIndex)
{
  MVP mvp{};
  mvp.view = camera.getViewMatrix();
  mvp.proj = glm::perspective(
    glm::radians(camera.fov),
    static_cast<float>(swapChainExtent.width) / static_cast<float>(swapChainExtent.height),
    0.1f,
    1000.0f
  );
  mvp.proj[1][1] *= -1;
  mvp.model = glm::rotate(camera.getRotationMatrix(), 0.0f, glm::vec3(0.0f, 0.0f, 1.0f));

  memcpy(uniformBuffersMapped[imageIndex], &mvp, sizeof(mvp));
}

void App::recordCommandBuffer(uint32_t imageIndex)
{
  commandBuffers[currentFrame].begin({});

  transitionImageLayout(
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
    .dstStageMask = vk::PipelineStageFlagBits2::eEarlyFragmentTests | vk::PipelineStageFlagBits2::eLateFragmentTests,
    .dstAccessMask = vk::AccessFlagBits2::eDepthStencilAttachmentRead | vk::AccessFlagBits2::eDepthStencilAttachmentWrite,
    .oldLayout = vk::ImageLayout::eUndefined,
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

  vk::ClearValue clearColour = vk::ClearColorValue(0.0f, 0.0f, 0.0f, 1.0f);
  vk::ClearDepthStencilValue clearDepth = vk::ClearDepthStencilValue(1.0f, 0);
  vk::RenderingAttachmentInfo colourAttachmentInfo {
    .imageView = swapChainImageViews[imageIndex],
    .imageLayout = vk::ImageLayout::eAttachmentOptimal,
    .loadOp = vk::AttachmentLoadOp::eClear,
    .storeOp = vk::AttachmentStoreOp::eStore,
    .clearValue = clearColour,
  };

  vk::RenderingAttachmentInfo depthAttachmentInfo {
    .imageView = depthImageView,
    .imageLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal,
    .loadOp = vk::AttachmentLoadOp::eClear,
    .storeOp = vk::AttachmentStoreOp::eDontCare,
    .clearValue = clearDepth,
  };
  
  vk::RenderingInfo renderingInfo {
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
  
  commandBuffers[currentFrame].bindPipeline(
    vk::PipelineBindPoint::eGraphics,
    *graphicsPipeline.second
  );

  commandBuffers[currentFrame].setViewport(0, vk::Viewport(0.0f, 0.0f, static_cast<float>(swapChainExtent.width), static_cast<float>(swapChainExtent.height), 0.0f, 1.0f));
  commandBuffers[currentFrame].setScissor(0, vk::Rect2D(vk::Offset2D(0, 0), swapChainExtent));
  
  commandBuffers[currentFrame].bindVertexBuffers(0, *vertexBuffer.first, {0});
  
  for (auto& p : prims)
  {
    commandBuffers[currentFrame].bindIndexBuffer(*p.indexBuffer.first, 0, vk::IndexType::eUint32);
    commandBuffers[currentFrame].bindDescriptorSets(
      vk::PipelineBindPoint::eGraphics,
      graphicsPipeline.first,
      0,
      *p.descriptorSets[currentFrame],
      nullptr
    );
    commandBuffers[currentFrame].drawIndexed(
      static_cast<uint32_t>(p.indices.size()),
      1, 0, 0, 0
    );
  }

  
  ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), static_cast<VkCommandBuffer>(*commandBuffers[currentFrame]));

  commandBuffers[currentFrame].endRendering();
  
  transitionImageLayout(
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

void App::transitionImageLayout(
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

  vk::DependencyInfo dependencyInfo {
    .dependencyFlags = {},
    .imageMemoryBarrierCount = 1,
    .pImageMemoryBarriers = &barrier
  };

  commandBuffers[currentFrame].pipelineBarrier2(dependencyInfo);
}

void App::cleanup()
{
  ImGui_ImplVulkan_Shutdown();
  ImGui_ImplGlfw_Shutdown();
  ImGui::DestroyContext();

  prims.clear();

  glfwDestroyWindow(pWindow);
  glfwTerminate();
}
