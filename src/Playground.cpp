#include "Playground.hpp"

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

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <execution>
#include <future>
#include <limits>
#include <memory>
#include <random>
#include <ranges>
#include <stdexcept>
#include <vector>

#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#ifndef VOLK_IMPLEMENTATION
#define VOLK_IMPLEMENTATION
#endif

void Playground::run()
{
  initWindow();
  initVulkan();
  mainLoop();
  cleanup();
}

void Playground::initWindow()
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
}

void Playground::initVulkan()
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
  createVertexBuffer();
  createIndexBuffers();
  createUniformBuffers();
  createDescriptorPool();
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

void Playground::createInstance()
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

void Playground::setupDebugMessenger()
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

void Playground::createSurface()
{
  VkSurfaceKHR _surface; // glfwCreateWindowSurface requires the struct defined in the C API
  if (glfwCreateWindowSurface(*instance, pWindow, nullptr, &_surface) != VK_SUCCESS)
  {
    throw std::runtime_error("failed to create window surface!");
  }
  surface = vk::raii::SurfaceKHR(instance, _surface);
}

void Playground::pickPhysicalDevice()
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

      auto features = _physicalDevice.template getFeatures2<vk::PhysicalDeviceFeatures2,
                                                            vk::PhysicalDeviceVulkan13Features,
                                                            vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT,
                                                            vk::PhysicalDeviceTimelineSemaphoreFeaturesKHR>();
      bool supportsRequiredFeatures = features.template get<vk::PhysicalDeviceFeatures2>().features.samplerAnisotropy &&
                                      features.template get<vk::PhysicalDeviceVulkan13Features>().dynamicRendering &&
                                      features.template get<vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT>().extendedDynamicState &&
                                      features.template get<vk::PhysicalDeviceTimelineSemaphoreFeaturesKHR>().timelineSemaphore;

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

void Playground::createLogicalDevice()
{
  auto queueFamilyIndex = findQueueFamilies(physicalDevice, surface);

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
  queueIndex = queueFamilyIndex;
  
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

void Playground::createSwapChain()
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

void Playground::createSwapChainImageViews()
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

void Playground::createDescriptorSetLayout() {
  std::array bindings{
    vk::DescriptorSetLayoutBinding(0, vk::DescriptorType::eUniformBuffer, 1, vk::ShaderStageFlagBits::eVertex, nullptr),
    vk::DescriptorSetLayoutBinding(1, vk::DescriptorType::eUniformBuffer, 1, vk::ShaderStageFlagBits::eFragment, nullptr)
  };

  vk::DescriptorSetLayoutCreateInfo layoutInfo{
    .bindingCount = 2,
    .pBindings = bindings.data()
  };

  descriptorSetLayout = vk::raii::DescriptorSetLayout(device, layoutInfo);
}


void Playground::createGraphicsPipeline()
{
  graphicsPipeline = std::pair(nullptr, nullptr);
  auto shaderModule = createShaderModule(readFile(shader_path));

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
  vk::PipelineShaderStageCreateInfo shaderStages[] = {vertShaderStageCreateInfo, fragShaderStageCreateInfo};
  
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
    .blendEnable = vk::True,
    .srcColorBlendFactor = vk::BlendFactor::eSrcAlpha,
    .dstColorBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha,
    .colorBlendOp = vk::BlendOp::eAdd,
    .srcAlphaBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha,
    .dstAlphaBlendFactor = vk::BlendFactor::eZero,
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
    .layout = *graphicsPipeline.first,
    .renderPass = nullptr,
    .subpass = 0
  };

  graphicsPipeline.second = vk::raii::Pipeline(device, nullptr, graphicsPipelineInfo);
}

[[nodiscard]] vk::raii::ShaderModule Playground::createShaderModule(const std::vector<char>& code) const 
{
    vk::ShaderModuleCreateInfo createInfo {
      .codeSize = code.size() * sizeof(char),
      .pCode = reinterpret_cast<const uint32_t*>(code.data())
    };
    
    vk::raii::ShaderModule shaderModule{device, createInfo};
    
    return shaderModule;
};

[[nodiscard]] vk::Format Playground::findDepthFormat() const
{
  return findSupportedFormat(
    {vk::Format::eD32Sfloat, vk::Format::eD32SfloatS8Uint, vk::Format::eD24UnormS8Uint},
    vk::ImageTiling::eOptimal,
    vk::FormatFeatureFlagBits::eDepthStencilAttachment
  );
}

vk::Format Playground::findSupportedFormat(
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

void Playground::createCommandPool()
{
  vk::CommandPoolCreateInfo commandPoolInfo {
    .flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer,
    .queueFamilyIndex = queueIndex,
  };
  commandPool = vk::raii::CommandPool(device, commandPoolInfo);
}

void Playground::createDepthResources()
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

void Playground::createImage(
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

uint32_t Playground::findMemoryType(uint32_t typeFilter, vk::MemoryPropertyFlags properties)
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

[[nodiscard]] vk::raii::ImageView Playground::createImageView(
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

void Playground::createBuffer(
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

void Playground::copyBuffer(
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

void Playground::createVertexBuffer()
{
  vk::DeviceSize bufferSize = sizeof(quad.vertices[0]) * quad.vertices.size();
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
  memcpy(dataStaging, quad.vertices.data(), bufferSize);
  stagingBufferMemory.unmapMemory();

  createBuffer(
    bufferSize,
    vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eTransferDst,
    vk::MemoryPropertyFlagBits::eDeviceLocal,
    quad.vertexBuffer.first,
    quad.vertexBuffer.second
  );

  copyBuffer(stagingBuffer, quad.vertexBuffer.first, bufferSize);
}

void Playground::createIndexBuffers()
{
  vk::DeviceSize bufferSize = sizeof(quad.indices[0]) * quad.indices.size();
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
  memcpy(dataStaging, quad.indices.data(), (size_t) bufferSize);
  stagingBufferMemory.unmapMemory();

  createBuffer(
    bufferSize,
    vk::BufferUsageFlagBits::eIndexBuffer | vk::BufferUsageFlagBits::eTransferDst,
    vk::MemoryPropertyFlagBits::eDeviceLocal,
    quad.indexBuffer.first,
    quad.indexBuffer.second
  );

  copyBuffer(stagingBuffer, quad.indexBuffer.first, bufferSize);
}

void Playground::createUniformBuffers()
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
    createBuffer(mvpBufferSize, vk::BufferUsageFlagBits::eUniformBuffer, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent, mvpBuffer, mvpBufferMemory);
    mvpBuffers.emplace_back(std::pair(std::move(mvpBuffer), std::move(mvpBufferMemory)));
    mvpBuffersMapped.emplace_back(mvpBuffers[i].second.mapMemory(0, mvpBufferSize));

    vk::DeviceSize uniformBufferSize = sizeof(UniformBuffer);
    vk::raii::Buffer uniformBuffer({});
    vk::raii::DeviceMemory uniformBufferMemory({});
    createBuffer(uniformBufferSize, vk::BufferUsageFlagBits::eUniformBuffer, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent, uniformBuffer, uniformBufferMemory);
    uniformBuffers.emplace_back(std::pair(std::move(uniformBuffer), std::move(uniformBufferMemory)));
    uniformBuffersMapped.emplace_back(uniformBuffers[i].second.mapMemory(0, uniformBufferSize));
  }
}

void Playground::createDescriptorPool() {
  std::array poolSizes{
    vk::DescriptorPoolSize(vk::DescriptorType::eUniformBuffer, MAX_FRAMES_IN_FLIGHT),
    vk::DescriptorPoolSize(vk::DescriptorType::eUniformBuffer, MAX_FRAMES_IN_FLIGHT)
  };
  vk::DescriptorPoolCreateInfo poolInfo { 
    .flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
    .maxSets = MAX_FRAMES_IN_FLIGHT,
    .poolSizeCount = static_cast<uint32_t>(poolSizes.size()),
    .pPoolSizes = poolSizes.data()
  };
  graphicsDescriptorPool = vk::raii::DescriptorPool(device, poolInfo);
}

void Playground::createDescriptorSets() {
  std::vector<vk::DescriptorSetLayout> layouts(MAX_FRAMES_IN_FLIGHT, *descriptorSetLayout);
  vk::DescriptorSetAllocateInfo allocInfo {
    .descriptorPool = graphicsDescriptorPool,
    .descriptorSetCount = static_cast<uint32_t>(layouts.size()),
    .pSetLayouts = layouts.data()
  };

  quad.descriptorSets = device.allocateDescriptorSets(allocInfo);

  for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
    vk::DescriptorBufferInfo mvpBufferInfo {
      .buffer = mvpBuffers[i].first,
      .offset = 0,
      .range = sizeof(MVP)
    };

    vk::DescriptorBufferInfo uniformBufferInfo{
      .buffer = uniformBuffers[i].first,
      .offset = 0,
      .range = sizeof(UniformBuffer)
    };
    std::array descriptorWrites{
      vk::WriteDescriptorSet {
        .dstSet = static_cast<vk::DescriptorSet>(*quad.descriptorSets[i]),
        .dstBinding = 0,
        .dstArrayElement = 0,
        .descriptorCount = 1,
        .descriptorType = vk::DescriptorType::eUniformBuffer,
        .pBufferInfo = &mvpBufferInfo
      },
      vk::WriteDescriptorSet {
        .dstSet = static_cast<vk::DescriptorSet>(*quad.descriptorSets[i]),
        .dstBinding = 1,
        .dstArrayElement = 0,
        .descriptorCount = 1,
        .descriptorType = vk::DescriptorType::eUniformBuffer,
        .pBufferInfo = &uniformBufferInfo
      }
    };
    device.updateDescriptorSets(descriptorWrites, {});
  }
}


void Playground::createCommandBuffers()
{
  commandBuffers.clear();

  vk::CommandBufferAllocateInfo allocInfo {
    .commandPool = commandPool,
    .level = vk::CommandBufferLevel::ePrimary,
    .commandBufferCount = MAX_FRAMES_IN_FLIGHT
  };

  commandBuffers = vk::raii::CommandBuffers(device, allocInfo);
}

void Playground::createSyncObjects()
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

void Playground::mainLoop()
{
  static bool showWindow = true;
  float deltaMultiplier = 10000000.0f;

  camera.yaw = glm::pi<double>() / 2.0;
  camera.update(1.0);
  double xpos, ypos;
  auto frameStart = std::chrono::system_clock::now();
  auto frameEnd = std::chrono::system_clock::time_point::max();

  while (glfwWindowShouldClose(pWindow) != GLFW_TRUE)
  {
    frameStart = std::chrono::system_clock::now();
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
    
    // Lock to 60fps
    while (std::chrono::duration_cast<std::chrono::microseconds>(frameEnd - frameStart).count() < 16667)
    {
      frameEnd = std::chrono::system_clock::now();
    }

    auto drawStart = std::chrono::system_clock::now();
    drawFrame();
    frameEnd = std::chrono::system_clock::now();
    delta = std::chrono::duration_cast<std::chrono::microseconds>(frameEnd - frameStart).count();
  }
  device.waitIdle();
}

void Playground::recreateSwapChain()
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

void Playground::cleanupSwapChain()
{
    swapChainImageViews.clear();
    swapChain = nullptr;
}

void Playground::reloadShaders()
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

void Playground::drawFrame()
{
  auto [result, imageIndex] = swapChain.acquireNextImage(UINT64_MAX, *presentSemaphores[semaphoreIndex], *inFlightFences[currentFrame]);
  while (vk::Result::eTimeout == device.waitForFences(*inFlightFences[currentFrame], vk::True, UINT64_MAX))
    ;

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
  device.resetFences(*inFlightFences[currentFrame]);
  updateUniformBuffer(currentFrame);
  updateMVPBuffer(currentFrame);
  
  //// Update timeline value for this frame
  uint64_t renderWaitValue = timelineValue;
  uint64_t renderSignalValue = ++timelineValue;
  {
    // GRAPHICS
    recordCommandBuffer(imageIndex);

    vk::PipelineStageFlags waitStage = vk::PipelineStageFlagBits::eColorAttachmentOutput;

    vk::TimelineSemaphoreSubmitInfo renderTimelineInfo{
    .waitSemaphoreValueCount = 1,
    .pWaitSemaphoreValues = &renderWaitValue,
    .signalSemaphoreValueCount = 1,
    .pSignalSemaphoreValues = &renderSignalValue
    };

    vk::SubmitInfo renderSubmitInfo {
        .pNext = &renderTimelineInfo,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &*timelineSemaphore,
        .pWaitDstStageMask = &waitStage,
        .commandBufferCount = 1,
        .pCommandBuffers = &*commandBuffers[currentFrame],
        .signalSemaphoreCount = 1,
        .pSignalSemaphores = &*timelineSemaphore
    };

    queue.submit(renderSubmitInfo, nullptr);
  }

  vk::SemaphoreWaitInfo waitInfo{
    .semaphoreCount = 1,
    .pSemaphores = &*timelineSemaphore,
    .pValues = &renderSignalValue
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
    recreateSwapChain();
  }
  else if (result != vk::Result::eSuccess)
  {
    throw std::runtime_error("failed to present swap chain image!");
  }

  semaphoreIndex = (semaphoreIndex + 1) % presentSemaphores.size();
  currentFrame = (currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
}

void Playground::updateUniformBuffer(uint32_t imageIndex)
{
  UniformBuffer ubo{};
  ubo.time = static_cast<float>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now() - start).count()) / 1000.0f;
  ubo.viewportWidth = swapChainExtent.width;
  ubo.viewportHeight = swapChainExtent.height;

  memcpy(uniformBuffersMapped[imageIndex], &ubo, sizeof(ubo));
}

void Playground::updateMVPBuffer(uint32_t imageIndex)
{
  MVP mvpo{};
  mvpo.mvp = camera.getMVPMatrix();
  
  memcpy(mvpBuffersMapped[imageIndex], &mvpo, sizeof(mvpo));
}

void Playground::recordCommandBuffer(uint32_t imageIndex)
{
  commandBuffers[currentFrame].reset();
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
  
  commandBuffers[currentFrame].bindVertexBuffers(0, *quad.vertexBuffer.first, {0});
  

  commandBuffers[currentFrame].bindIndexBuffer(*quad.indexBuffer.first, 0, vk::IndexType::eUint16);
  commandBuffers[currentFrame].bindDescriptorSets(
    vk::PipelineBindPoint::eGraphics,
    graphicsPipeline.first,
    0,
    *quad.descriptorSets[currentFrame],
    nullptr
  );
  commandBuffers[currentFrame].drawIndexed(
    static_cast<uint32_t>(quad.indices.size()),
    1, 0, 0, 0
  );
  
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

void Playground::transitionImageLayout(
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

void Playground::cleanup()
{

  quad.descriptorSets.clear();
  quad.indexBuffer = std::pair(nullptr, nullptr);
  quad.vertexBuffer = std::pair(nullptr, nullptr);

  swapChain.clear();
  surface = nullptr;

  glfwDestroyWindow(pWindow);
  glfwTerminate();
}
