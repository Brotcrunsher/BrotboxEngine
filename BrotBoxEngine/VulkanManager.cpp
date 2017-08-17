#include "stdafx.h"
#include "BBE/Vector2.h"
#include "BBE/Vector3.h"
#include "BBE/VulkanManager.h"
#include "BBE/Color.h"
#include "BBE/Exceptions.h"
#include "BBE/Rectangle.h"
#include "BBE/VWTransform.h"
#include "BBE/EngineSettings.h"
#include "BBE/VertexWithNormal.h"
#include "BBE/Terrain.h"

bbe::INTERNAL::vulkan::VulkanManager *bbe::INTERNAL::vulkan::VulkanManager::s_pinstance = nullptr;

void bbe::INTERNAL::vulkan::VulkanManager::destroyPendingBuffers()
{
	while (m_pendingDestructionBuffers.hasDataLeft())
	{
		vkDestroyBuffer(m_device.getDevice(), m_pendingDestructionBuffers.pop(), nullptr);
	}
	while (m_pendingDestructionMemory.hasDataLeft())
	{
		vkFreeMemory(m_device.getDevice(), m_pendingDestructionMemory.pop(), nullptr);
	}
}

bbe::INTERNAL::vulkan::VulkanManager::VulkanManager()
{
}

void bbe::INTERNAL::vulkan::VulkanManager::init(const char * appName, uint32_t major, uint32_t minor, uint32_t patch, GLFWwindow * window, uint32_t initialWindowWidth, uint32_t initialWindowHeight)
{
	if (s_pinstance != nullptr)
	{
		throw SingletonViolationException();
	}
	s_pinstance = this;

	m_screenWidth = initialWindowWidth;
	m_screenHeight = initialWindowHeight;

	m_window = window;
	m_instance.init(appName, major, minor, patch);
	m_surface.init(m_instance, m_window);
	m_physicalDeviceContainer.init(m_instance, m_surface);
	m_device.init(m_physicalDeviceContainer, m_surface);
	m_swapchain.init(m_surface, m_device, initialWindowWidth, initialWindowHeight, nullptr);
	m_renderPass.init(m_device);

	m_commandPool.init(m_device);
	m_depthImage.create(m_device, m_commandPool, initialWindowWidth, initialWindowHeight);
	m_swapchain.createFramebuffers(m_depthImage, m_renderPass);
	m_semaphoreImageAvailable.init(m_device);
	m_semaphoreRenderingDone.init(m_device);
	m_presentFence.init(m_device);

	m_primitiveBrush3D.create(m_device);
	bbe::VWTransform::s_init(m_device.getDevice(), m_device.getPhysicalDevice(), m_commandPool, m_device.getQueue());

	int amountOfBuffers = Settings::getAmountOfTransformContainers();
	m_descriptorPool.setAmountOfSets(amountOfBuffers);
	m_descriptorPool.addPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 2 * amountOfBuffers * amountOfBuffers);
	for (int i = 0; i < amountOfBuffers; i++)
	{
		m_descriptorPool.addLayoutBinding(i * 2, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT);
		m_descriptorPool.addLayoutBinding(i * 2 + 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT);
		m_descriptorPool.addDescriptorBufferInfo(m_primitiveBrush3D.m_uboMatrices, 0, sizeof(Matrix4) * 2, 0, i);
		m_descriptorPool.addDescriptorBufferInfo(VWTransform::s_buffers[i], 0, sizeof(Matrix4) * VWTransform::NUM_BUFFERS_PER_CONTAINER, 1, i);
	}
	m_descriptorPool.create(m_device.getDevice());

	m_vertexShader2DPrimitive.init(m_device, "vert2DPrimitive.spv");
	m_fragmentShader2DPrimitive.init(m_device, "frag2DPrimitive.spv");
	m_pipeline2DPrimitive.init(m_vertexShader2DPrimitive, m_fragmentShader2DPrimitive, initialWindowWidth, initialWindowHeight);
	m_pipeline2DPrimitive.addVertexBinding(0, sizeof(Vector2), VK_VERTEX_INPUT_RATE_VERTEX);
	m_pipeline2DPrimitive.addVertexDescription(0, 0, VK_FORMAT_R32G32_SFLOAT, 0);
	m_pipeline2DPrimitive.addPushConstantRange(VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(Color));
	m_pipeline2DPrimitive.addPushConstantRange(VK_SHADER_STAGE_VERTEX_BIT, sizeof(Color), sizeof(float) * 4);
	m_pipeline2DPrimitive.enableDepthBuffer();
	m_pipeline2DPrimitive.create(m_device.getDevice(), m_renderPass.getRenderPass());

	m_vertexShader3DPrimitive.init(m_device, "vert3DPrimitive.spv");
	m_fragmentShader3DPrimitive.init(m_device, "frag3DPrimitive.spv");
	m_pipeline3DPrimitive.init(m_vertexShader3DPrimitive, m_fragmentShader3DPrimitive, initialWindowWidth, initialWindowHeight);
	m_pipeline3DPrimitive.addVertexBinding(0, sizeof(VertexWithNormal), VK_VERTEX_INPUT_RATE_VERTEX);
	m_pipeline3DPrimitive.addVertexDescription(0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(VertexWithNormal, m_pos));
	m_pipeline3DPrimitive.addVertexDescription(1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(VertexWithNormal, m_normal));
	m_pipeline3DPrimitive.addPushConstantRange(VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(Color));
	m_pipeline3DPrimitive.addPushConstantRange(VK_SHADER_STAGE_VERTEX_BIT, sizeof(Color), sizeof(uint32_t));
	m_pipeline3DPrimitive.addDescriptorSetLayout(m_descriptorPool.getLayout());
	m_pipeline3DPrimitive.enableDepthBuffer();
	m_pipeline3DPrimitive.create(m_device.getDevice(), m_renderPass.getRenderPass());
	m_uboMatrixViewProjection.create(m_device, sizeof(Matrix4), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
	m_uboMatrixModel.create(m_device, sizeof(Matrix4), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);

	

	

	bbe::Rectangle::s_init(m_device.getDevice(), m_device.getPhysicalDevice(), m_commandPool, m_device.getQueue());
	bbe::Circle::s_init(m_device.getDevice(), m_device.getPhysicalDevice(), m_commandPool, m_device.getQueue());
	bbe::Cube::s_init(m_device.getDevice(), m_device.getPhysicalDevice(), m_commandPool, m_device.getQueue());
	bbe::Terrain::s_init(m_device.getDevice(), m_device.getPhysicalDevice(), m_commandPool, m_device.getQueue());
}

void bbe::INTERNAL::vulkan::VulkanManager::destroy()
{
	vkDeviceWaitIdle(m_device.getDevice());
	s_pinstance = nullptr;
	bbe::VWTransform::s_destroy();
	bbe::Terrain::s_destroy();
	bbe::Cube::s_destroy();
	bbe::Circle::s_destroy();
	bbe::Rectangle::s_destroy();


	destroyPendingBuffers();
	m_presentFence.destroy();
	m_semaphoreRenderingDone.destroy();
	m_semaphoreImageAvailable.destroy();
	m_depthImage.destroy();
	m_commandPool.destroy();

	m_uboMatrixViewProjection.destroy();
	m_uboMatrixModel.destroy();
	m_pipeline3DPrimitive.destroy();
	m_fragmentShader3DPrimitive.destroy();
	m_vertexShader3DPrimitive.destroy();

	m_pipeline2DPrimitive.destroy();
	m_fragmentShader2DPrimitive.destroy();
	m_vertexShader2DPrimitive.destroy();

	m_descriptorPool.destroy();
	m_primitiveBrush3D.destroy();
	m_renderPass.destroy();
	m_swapchain.destroy();
	m_device.destroy();
	m_surface.destroy();
	m_instance.destroy();
}

void bbe::INTERNAL::vulkan::VulkanManager::preDraw2D()
{
	vkCmdBindPipeline(m_currentFrameDrawCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline2DPrimitive.getPipeline());
}

void bbe::INTERNAL::vulkan::VulkanManager::preDraw3D()
{
	vkCmdBindPipeline(m_currentFrameDrawCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline3DPrimitive.getPipeline());
}

void bbe::INTERNAL::vulkan::VulkanManager::preDraw()
{
	vkAcquireNextImageKHR(m_device.getDevice(), m_swapchain.getSwapchain(), std::numeric_limits<uint64_t>::max(), m_semaphoreImageAvailable.getSemaphore(), VK_NULL_HANDLE, &m_imageIndex);

	m_currentFrameDrawCommandBuffer = m_commandPool.getCommandBuffer();

	VkCommandBufferBeginInfo cbbi;
	cbbi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	cbbi.pNext = nullptr;
	cbbi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	cbbi.pInheritanceInfo = nullptr;


	VkResult result = vkBeginCommandBuffer(m_currentFrameDrawCommandBuffer, &cbbi);
	ASSERT_VULKAN(result);

	VkRenderPassBeginInfo renderPassBeginInfo;
	renderPassBeginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
	renderPassBeginInfo.pNext = nullptr;
	renderPassBeginInfo.renderPass = m_renderPass.getRenderPass();
	renderPassBeginInfo.framebuffer = m_swapchain.getFrameBuffer(m_imageIndex);
	renderPassBeginInfo.renderArea.offset = { 0, 0 };
	renderPassBeginInfo.renderArea.extent = { m_screenWidth, m_screenHeight };
	VkClearValue clearValue = { 0.0f, 0.0f, 0.0f, 1.0f };
	VkClearValue depthClearValue = { 1.0f, 0 };

	bbe::List<VkClearValue> clearValues = { 
		clearValue,
		depthClearValue
	};

	renderPassBeginInfo.clearValueCount = clearValues.getLength();
	renderPassBeginInfo.pClearValues = clearValues.getRaw();


	vkCmdBeginRenderPass(m_currentFrameDrawCommandBuffer, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);
	

	VkViewport viewport;
	viewport.x = 0.0f;
	viewport.y = 0.0f;
	viewport.width = (float)m_screenWidth;
	viewport.height = (float)m_screenHeight;
	viewport.minDepth = 0.0f;
	viewport.maxDepth = 1.0f;
	vkCmdSetViewport(m_currentFrameDrawCommandBuffer, 0, 1, &viewport);

	VkRect2D scissor;
	scissor.offset = { 0, 0 };
	scissor.extent = { m_screenWidth, m_screenHeight };
	vkCmdSetScissor(m_currentFrameDrawCommandBuffer, 0, 1, &scissor);

	m_primitiveBrush2D.INTERNAL_beginDraw(m_device, m_currentFrameDrawCommandBuffer, m_pipeline2DPrimitive.getLayout(), m_screenWidth, m_screenHeight);
	m_primitiveBrush3D.INTERNAL_beginDraw(m_device, m_currentFrameDrawCommandBuffer, m_pipeline3DPrimitive.getLayout(), m_screenWidth, m_screenHeight, &m_descriptorPool);
}

void bbe::INTERNAL::vulkan::VulkanManager::postDraw()
{
	vkCmdEndRenderPass(m_currentFrameDrawCommandBuffer);

	VkResult result = vkEndCommandBuffer(m_currentFrameDrawCommandBuffer);
	ASSERT_VULKAN(result);

	VkSemaphore semImAv = m_semaphoreImageAvailable.getSemaphore();
	VkSemaphore semReDo = m_semaphoreRenderingDone.getSemaphore();
	VkPipelineStageFlags waitStageMask[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
	VkSwapchainKHR swapchain = m_swapchain.getSwapchain();
	VkQueue queue = m_device.getQueue();

	VkSubmitInfo si = {};
	si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	si.pNext = nullptr;
	si.waitSemaphoreCount = 1;
	si.pWaitSemaphores = &(semImAv);
	si.pWaitDstStageMask = waitStageMask;
	si.commandBufferCount = 1;
	si.pCommandBuffers = &m_currentFrameDrawCommandBuffer;
	si.signalSemaphoreCount = 1;
	si.pSignalSemaphores = &semReDo;

	result = vkQueueSubmit(queue, 1, &si, m_presentFence.getFence());
	ASSERT_VULKAN(result);

	VkPresentInfoKHR pi = {};
	pi.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
	pi.pNext = nullptr;
	pi.waitSemaphoreCount = 1;
	pi.pWaitSemaphores = &semReDo;
	pi.swapchainCount = 1;
	pi.pSwapchains = &swapchain;
	pi.pImageIndices = &m_imageIndex;
	pi.pResults = nullptr;

	result = vkQueuePresentKHR(m_device.getQueue(), &pi);
	ASSERT_VULKAN(result);
	m_presentFence.waitForFence();
	m_commandPool.freeCommandBuffer(m_currentFrameDrawCommandBuffer);
	destroyPendingBuffers();
}

bbe::PrimitiveBrush2D * bbe::INTERNAL::vulkan::VulkanManager::getBrush2D()
{
	return &m_primitiveBrush2D;
}

bbe::PrimitiveBrush3D * bbe::INTERNAL::vulkan::VulkanManager::getBrush3D()
{
	return &m_primitiveBrush3D;
}

void bbe::INTERNAL::vulkan::VulkanManager::addPendingDestructionBuffer(VkBuffer buffer, VkDeviceMemory memory)
{
	m_pendingDestructionBuffers.push(buffer);
	m_pendingDestructionMemory.push(memory);
}

