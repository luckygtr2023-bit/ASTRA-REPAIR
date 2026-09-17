// ASTRA COSMOS — Production Win32 + Vulkan Entry Point
// Real rendering: fullscreen-triangle pipeline + push constants bound to ASTRA
// scientific state, runtime SPIR-V loading and full swapchain recreation.

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <vulkan/vulkan.h>
#include <vulkan/vulkan_win32.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

// ASTRA engine headers (preserved)
#include "scene/floating_origin.h"
#include "scene/scene.h"

// Globals
static HWND g_hwnd = nullptr;
static bool g_quit = false;
static bool g_minimized = false;
static uint32_t g_width = 1280, g_height = 720;

// Vulkan core
static VkInstance g_instance = VK_NULL_HANDLE;
static VkPhysicalDevice g_gpu = VK_NULL_HANDLE;
static VkDevice g_device = VK_NULL_HANDLE;
static uint32_t g_gfx_family = UINT32_MAX;
static VkQueue g_gfx_queue = VK_NULL_HANDLE;
static VkSurfaceKHR g_surface = VK_NULL_HANDLE;
static VkSwapchainKHR g_swapchain = VK_NULL_HANDLE;
static VkFormat g_sc_format = VK_FORMAT_UNDEFINED;
static VkExtent2D g_sc_extent = {};
static std::vector<VkImage> g_sc_images;
static std::vector<VkImageView> g_sc_views;
static VkRenderPass g_render_pass = VK_NULL_HANDLE;
static std::vector<VkFramebuffer> g_framebuffers;
static VkCommandPool g_cmd_pool = VK_NULL_HANDLE;
static VkCommandBuffer g_cmd_buf = VK_NULL_HANDLE;
static VkSemaphore g_img_sem = VK_NULL_HANDLE;
static VkSemaphore g_render_sem = VK_NULL_HANDLE;
static VkFence g_fence = VK_NULL_HANDLE;

// Pipeline
static VkPipelineLayout g_pipeline_layout = VK_NULL_HANDLE;
static VkPipeline g_graphics_pipeline = VK_NULL_HANDLE;
static VkShaderModule g_vert_module = VK_NULL_HANDLE;
static VkShaderModule g_frag_module = VK_NULL_HANDLE;

// ASTRA state
static astra::scene::Scene g_scene;
static uint64_t g_frame_count = 0;
static volatile bool g_swapchain_dirty = false;

#define VK_CHECK(call)                                                        \
    do {                                                                      \
        VkResult r_ = (call);                                                 \
        if (r_ != VK_SUCCESS) {                                               \
            printf("[ASTRA] VK ERROR %d at %s:%d (%s)\n", (int)r_,            \
                   __FILE__, __LINE__, #call);                                \
            return false;                                                     \
        }                                                                     \
    } while (0)

// ─── Filesystem helpers ───────────────────────────────────────────────────────

static std::string exe_dir() {
    char buf[MAX_PATH];
    DWORD n = GetModuleFileNameA(nullptr, buf, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return ".";
    std::string p(buf, n);
    size_t pos = p.find_last_of("\\/");
    return (pos == std::string::npos) ? std::string(".") : p.substr(0, pos);
}

static bool read_spirv(const std::string& path, std::vector<uint32_t>& out) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size <= 0 || (size % 4) != 0) { fclose(f); return false; }
    out.resize((size_t)size / 4);
    bool ok = fread(out.data(), 1, (size_t)size, f) == (size_t)size;
    fclose(f);
    return ok && out[0] == 0x07230203u; // SPIR-V magic
}

// ─── Window ───────────────────────────────────────────────────────────────────

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CLOSE: g_quit = true; return 0;
    case WM_DESTROY: PostQuitMessage(0); return 0;
    case WM_SIZE:
        if (wp == SIZE_MINIMIZED) { g_minimized = true; return 0; }
        g_minimized = false;
        {
            uint32_t w = LOWORD(lp), h = HIWORD(lp);
            if (w != 0 && h != 0 && (w != g_width || h != g_height)) {
                g_width = w;
                g_height = h;
                g_swapchain_dirty = true; // framebuffer size changed — recreate
            }
        }
        return 0;
    case WM_KEYDOWN:
        if (wp == VK_ESCAPE) g_quit = true;
        return 0;
    }
    return DefWindowProcA(hwnd, msg, wp, lp);
}

// Create window
static bool create_window() {
    HINSTANCE hinst = GetModuleHandleA(nullptr);
    WNDCLASSEXA wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hinst;
    wc.hCursor = LoadCursorA(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.lpszClassName = "AstraCosmos";
    wc.hIcon = LoadIconA(nullptr, IDI_APPLICATION);
    if (!RegisterClassExA(&wc)) return false;

    RECT rc = {0, 0, (LONG)g_width, (LONG)g_height};
    AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE);
    g_hwnd = CreateWindowExA(0, "AstraCosmos", "ASTRA COSMOS",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
        rc.right - rc.left, rc.bottom - rc.top,
        nullptr, nullptr, hinst, nullptr);
    if (!g_hwnd) return false;

    ShowWindow(g_hwnd, SW_SHOWDEFAULT);
    UpdateWindow(g_hwnd);
    printf("[ASTRA] Window created %ux%u\n", g_width, g_height);
    return true;
}

// Create Vulkan instance
static bool create_instance() {
    VkApplicationInfo app_info{};
    app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app_info.pApplicationName = "ASTRA COSMOS";
    app_info.applicationVersion = VK_MAKE_VERSION(0, 1, 0);
    app_info.pEngineName = "ASTRA Native";
    app_info.engineVersion = VK_MAKE_VERSION(0, 1, 0);
    app_info.apiVersion = VK_API_VERSION_1_3;

    const char* extensions[] = {
        VK_KHR_SURFACE_EXTENSION_NAME,
        VK_KHR_WIN32_SURFACE_EXTENSION_NAME
    };

    VkInstanceCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ci.pApplicationInfo = &app_info;
    ci.enabledExtensionCount = 2;
    ci.ppEnabledExtensionNames = extensions;

    VK_CHECK(vkCreateInstance(&ci, nullptr, &g_instance));
    printf("[ASTRA] Vulkan instance created\n");
    return true;
}

// Create surface
static bool create_surface() {
    VkWin32SurfaceCreateInfoKHR ci{};
    ci.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
    ci.hinstance = GetModuleHandleA(nullptr);
    ci.hwnd = g_hwnd;
    VK_CHECK(vkCreateWin32SurfaceKHR(g_instance, &ci, nullptr, &g_surface));
    printf("[ASTRA] Surface created\n");
    return true;
}

// Select GPU and create device
static bool create_device() {
    uint32_t count = 0;
    vkEnumeratePhysicalDevices(g_instance, &count, nullptr);
    if (count == 0) return false;
    std::vector<VkPhysicalDevice> devs(count);
    vkEnumeratePhysicalDevices(g_instance, &count, devs.data());

    // Prefer a discrete GPU, then integrated, then anything else.
    int best_score = -1;
    for (auto& d : devs) {
        VkPhysicalDeviceProperties p{};
        vkGetPhysicalDeviceProperties(d, &p);
        int score = (p.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)   ? 3
                  : (p.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU) ? 2
                  : (p.deviceType == VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU)    ? 1
                                                                             : 0;
        if (score > best_score) { best_score = score; g_gpu = d; }
    }

    VkPhysicalDeviceProperties props{};
    vkGetPhysicalDeviceProperties(g_gpu, &props);
    printf("[ASTRA] GPU: %s\n", props.deviceName);

    // Find graphics + present queue family
    uint32_t qfc = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(g_gpu, &qfc, nullptr);
    std::vector<VkQueueFamilyProperties> qfs(qfc);
    vkGetPhysicalDeviceQueueFamilyProperties(g_gpu, &qfc, qfs.data());

    g_gfx_family = UINT32_MAX;
    for (uint32_t i = 0; i < qfc; i++) {
        if (qfs[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            VkBool32 present = VK_FALSE;
            vkGetPhysicalDeviceSurfaceSupportKHR(g_gpu, i, g_surface, &present);
            if (present) {
                g_gfx_family = i;
                break;
            }
        }
    }
    if (g_gfx_family == UINT32_MAX) return false;

    float prio = 1.0f;
    VkDeviceQueueCreateInfo qci{};
    qci.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    qci.queueFamilyIndex = g_gfx_family;
    qci.queueCount = 1;
    qci.pQueuePriorities = &prio;

    const char* dev_exts[] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
    VkDeviceCreateInfo dci{};
    dci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    dci.queueCreateInfoCount = 1;
    dci.pQueueCreateInfos = &qci;
    dci.enabledExtensionCount = 1;
    dci.ppEnabledExtensionNames = dev_exts;

    VK_CHECK(vkCreateDevice(g_gpu, &dci, nullptr, &g_device));
    vkGetDeviceQueue(g_device, g_gfx_family, 0, &g_gfx_queue);
    printf("[ASTRA] Device created (queue family %u)\n", g_gfx_family);
    return true;
}

// Create swapchain (also used on recreation; framebuffers/views are rebuilt there)
static bool create_swapchain() {
    VkSurfaceCapabilitiesKHR caps{};
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(g_gpu, g_surface, &caps);

    uint32_t fmt_count = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(g_gpu, g_surface, &fmt_count, nullptr);
    if (fmt_count == 0) {
        printf("[ASTRA] Surface exposes no formats\n");
        return false;
    }
    std::vector<VkSurfaceFormatKHR> fmts(fmt_count);
    vkGetPhysicalDeviceSurfaceFormatsKHR(g_gpu, g_surface, &fmt_count, fmts.data());

    VkSurfaceFormatKHR fmt = fmts[0];
    for (auto& f : fmts) {
        if (f.format == VK_FORMAT_B8G8R8A8_SRGB && f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            fmt = f;
            break;
        }
    }

    VkExtent2D extent = caps.currentExtent;
    if (extent.width == UINT32_MAX) {
        extent.width = g_width;
        extent.height = g_height;
    }
    // Clamp into the surface-supported range (resize-safe).
    extent.width = std::clamp(extent.width, caps.minImageExtent.width, caps.maxImageExtent.width);
    extent.height = std::clamp(extent.height, caps.minImageExtent.height, caps.maxImageExtent.height);
    if (extent.width == 0 || extent.height == 0) {
        printf("[ASTRA] Surface extent is zero (minimized?) — deferring swapchain\n");
        return false;
    }

    uint32_t image_count = caps.minImageCount + 1;
    if (caps.maxImageCount > 0 && image_count > caps.maxImageCount) {
        image_count = caps.maxImageCount; // respect driver limit
    }

    VkSwapchainCreateInfoKHR sci{};
    sci.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    sci.surface = g_surface;
    sci.minImageCount = image_count;
    sci.imageFormat = fmt.format;
    sci.imageColorSpace = fmt.colorSpace;
    sci.imageExtent = extent;
    sci.imageArrayLayers = 1;
    sci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    sci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    sci.preTransform = caps.currentTransform;
    sci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    sci.presentMode = VK_PRESENT_MODE_FIFO_KHR; // guaranteed to exist
    sci.clipped = VK_TRUE;

    VK_CHECK(vkCreateSwapchainKHR(g_device, &sci, nullptr, &g_swapchain));
    g_sc_format = fmt.format;
    g_sc_extent = extent;

    uint32_t img_count = 0;
    vkGetSwapchainImagesKHR(g_device, g_swapchain, &img_count, nullptr);
    g_sc_images.resize(img_count);
    vkGetSwapchainImagesKHR(g_device, g_swapchain, &img_count, g_sc_images.data());

    g_sc_views.resize(img_count);
    for (uint32_t i = 0; i < img_count; i++) {
        VkImageViewCreateInfo vci{};
        vci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        vci.image = g_sc_images[i];
        vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vci.format = g_sc_format;
        vci.components = {VK_COMPONENT_SWIZZLE_IDENTITY,VK_COMPONENT_SWIZZLE_IDENTITY,VK_COMPONENT_SWIZZLE_IDENTITY,VK_COMPONENT_SWIZZLE_IDENTITY};
        vci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        vci.subresourceRange.baseMipLevel = 0;
        vci.subresourceRange.levelCount = 1;
        vci.subresourceRange.baseArrayLayer = 0;
        vci.subresourceRange.layerCount = 1;
        VK_CHECK(vkCreateImageView(g_device, &vci, nullptr, &g_sc_views[i]));
    }
    printf("[ASTRA] Swapchain created %ux%u (%u images)\n", extent.width, extent.height, img_count);
    return true;
}

// Create render pass
static bool create_render_pass() {
    VkAttachmentDescription att{};
    att.format = g_sc_format;
    att.samples = VK_SAMPLE_COUNT_1_BIT;
    att.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    att.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    att.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    att.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    att.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    att.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference ref{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription sub{};
    sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    sub.colorAttachmentCount = 1;
    sub.pColorAttachments = &ref;

    VkSubpassDependency dep{};
    dep.srcSubpass = VK_SUBPASS_EXTERNAL;
    dep.dstSubpass = 0;
    dep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.srcAccessMask = 0;
    dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    ci.attachmentCount = 1;
    ci.pAttachments = &att;
    ci.subpassCount = 1;
    ci.pSubpasses = &sub;
    ci.dependencyCount = 1;
    ci.pDependencies = &dep;
    VK_CHECK(vkCreateRenderPass(g_device, &ci, nullptr, &g_render_pass));
    return true;
}

// Create framebuffers
static bool create_framebuffers() {
    g_framebuffers.resize(g_sc_views.size());
    for (size_t i = 0; i < g_sc_views.size(); i++) {
        VkFramebufferCreateInfo ci{};
        ci.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        ci.renderPass = g_render_pass;
        ci.attachmentCount = 1;
        ci.pAttachments = &g_sc_views[i];
        ci.width = g_sc_extent.width;
        ci.height = g_sc_extent.height;
        ci.layers = 1;
        VK_CHECK(vkCreateFramebuffer(g_device, &ci, nullptr, &g_framebuffers[i]));
    }
    return true;
}

// Create command buffer (allocated from the SAME family the queue belongs to)
static bool create_commands() {
    VkCommandPoolCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    ci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    ci.queueFamilyIndex = g_gfx_family; // was hardcoded 0 — spec violation on some GPUs
    VK_CHECK(vkCreateCommandPool(g_device, &ci, nullptr, &g_cmd_pool));

    VkCommandBufferAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    ai.commandPool = g_cmd_pool;
    ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = 1;
    VK_CHECK(vkAllocateCommandBuffers(g_device, &ai, &g_cmd_buf));
    return true;
}

// Create sync objects
static bool create_sync() {
    VkSemaphoreCreateInfo si{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    VkFenceCreateInfo fi{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, nullptr, VK_FENCE_CREATE_SIGNALED_BIT};
    VK_CHECK(vkCreateSemaphore(g_device, &si, nullptr, &g_img_sem));
    VK_CHECK(vkCreateSemaphore(g_device, &si, nullptr, &g_render_sem));
    VK_CHECK(vkCreateFence(g_device, &fi, nullptr, &g_fence));
    return true;
}

// ─── Graphics pipeline: fullscreen triangle, ASTRA state via push constants ───

static VkShaderModule create_shader_module(const std::vector<uint32_t>& words) {
    if (words.empty()) return VK_NULL_HANDLE;
    VkShaderModuleCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    ci.codeSize = words.size() * sizeof(uint32_t);
    ci.pCode = words.data();
    VkShaderModule module = VK_NULL_HANDLE;
    if (vkCreateShaderModule(g_device, &ci, nullptr, &module) != VK_SUCCESS) {
        return VK_NULL_HANDLE;
    }
    return module;
}

static bool load_shader(const char* name, std::vector<uint32_t>& out) {
    // Search order: <exe>/shaders, CWD/shaders, <exe>, CWD.
    const std::string dir = exe_dir();
    const std::string bases[] = {
        dir + "\\shaders\\",
        ".\\shaders\\",
        dir + "\\",
        ".\\"
    };
    for (const auto& base : bases) {
        const std::string path = base + name;
        if (read_spirv(path, out)) {
            printf("[ASTRA] Shader loaded: %s\n", path.c_str());
            return true;
        }
    }
    printf("[ASTRA] Missing shader %s — expected shaders\\%s next to ASTRA COSMOS.exe\n", name, name);
    return false;
}

static bool create_pipeline() {
    std::vector<uint32_t> vert_spv, frag_spv;
    if (!load_shader("astra.vert.spv", vert_spv)) return false;
    if (!load_shader("astra.frag.spv", frag_spv)) return false;

    g_vert_module = create_shader_module(vert_spv);
    g_frag_module = create_shader_module(frag_spv);
    if (!g_vert_module || !g_frag_module) {
        printf("[ASTRA] vkCreateShaderModule failed\n");
        return false;
    }

    // Push constants MUST match src/shaders/astra.frag exactly:
    //   float sim_time; float width; float height; float frame;  (16 bytes)
    VkPushConstantRange pcr{};
    pcr.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    pcr.offset = 0;
    pcr.size = 4 * sizeof(float);

    VkPipelineLayoutCreateInfo pli{};
    pli.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pli.pushConstantRangeCount = 1;
    pli.pPushConstantRanges = &pcr;
    VK_CHECK(vkCreatePipelineLayout(g_device, &pli, nullptr, &g_pipeline_layout));

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = g_vert_module;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = g_frag_module;
    stages[1].pName = "main";

    // No vertex buffers — astra.vert generates the fullscreen triangle from gl_VertexIndex.
    VkPipelineVertexInputStateCreateInfo vi{};
    vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

    VkPipelineInputAssemblyStateCreateInfo ia{};
    ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    // Dynamic viewport/scissor — swapchain recreation needs no pipeline rebuild.
    VkPipelineViewportStateCreateInfo vp{};
    vp.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vp.viewportCount = 1;
    vp.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rs{};
    rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode = VK_CULL_MODE_NONE;
    rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rs.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo ms{};
    ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState cba{};
    cba.blendEnable = VK_FALSE;
    cba.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                         VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    VkPipelineColorBlendStateCreateInfo cb{};
    cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    cb.attachmentCount = 1;
    cb.pAttachments = &cba;

    const VkDynamicState dyn_states[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo ds{};
    ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    ds.dynamicStateCount = 2;
    ds.pDynamicStates = dyn_states;

    VkGraphicsPipelineCreateInfo gp{};
    gp.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    gp.stageCount = 2;
    gp.pStages = stages;
    gp.pVertexInputState = &vi;
    gp.pInputAssemblyState = &ia;
    gp.pViewportState = &vp;
    gp.pRasterizationState = &rs;
    gp.pMultisampleState = &ms;
    gp.pColorBlendState = &cb;
    gp.pDynamicState = &ds;
    gp.layout = g_pipeline_layout;
    gp.renderPass = g_render_pass;
    gp.subpass = 0;
    VK_CHECK(vkCreateGraphicsPipelines(g_device, VK_NULL_HANDLE, 1, &gp, nullptr, &g_graphics_pipeline));

    printf("[ASTRA] Graphics pipeline created (fullscreen triangle + push constants)\n");
    return true;
}

// ─── Swapchain recreation (resize / OUT_OF_DATE / SUBOPTIMAL) ────────────────

static bool recreate_swapchain() {
    g_swapchain_dirty = false;
    if (g_width == 0 || g_height == 0 || g_minimized) return true; // wait for restore

    vkDeviceWaitIdle(g_device);

    for (auto fb : g_framebuffers) vkDestroyFramebuffer(g_device, fb, nullptr);
    g_framebuffers.clear();
    for (auto v : g_sc_views) vkDestroyImageView(g_device, v, nullptr);
    g_sc_views.clear();
    if (g_swapchain != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(g_device, g_swapchain, nullptr);
        g_swapchain = VK_NULL_HANDLE;
    }

    if (!create_swapchain()) return false;
    if (!create_framebuffers()) return false;
    printf("[ASTRA] Swapchain recreated for %ux%u\n", g_sc_extent.width, g_sc_extent.height);
    return true;
}

// ─── Frame rendering ──────────────────────────────────────────────────────────

static bool render_frame() {
    if (g_width == 0 || g_height == 0 || g_minimized) {
        Sleep(16); // minimized — idle instead of spinning the GPU
        return true;
    }
    if (g_swapchain_dirty && !recreate_swapchain()) return false;

    vkWaitForFences(g_device, 1, &g_fence, VK_TRUE, UINT64_MAX);

    uint32_t img_idx = 0;
    VkResult acquire = vkAcquireNextImageKHR(g_device, g_swapchain, UINT64_MAX,
                                             g_img_sem, VK_NULL_HANDLE, &img_idx);
    if (acquire == VK_ERROR_OUT_OF_DATE_KHR) {
        g_swapchain_dirty = true; // fence still signaled — safe to retry next frame
        return true;
    }
    if (acquire != VK_SUCCESS && acquire != VK_SUBOPTIMAL_KHR) {
        printf("[ASTRA] vkAcquireNextImageKHR failed: %d\n", (int)acquire);
        return false;
    }
    if (acquire == VK_SUBOPTIMAL_KHR) g_swapchain_dirty = true; // draw once, then recreate

    vkResetFences(g_device, 1, &g_fence);
    vkResetCommandBuffer(g_cmd_buf, 0);

    VkCommandBufferBeginInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(g_cmd_buf, &bi);

    // Dark clear; the fragment shader paints the animated deep-space glow + grid.
    VkClearValue clear{};
    clear.color = {{0.005f, 0.005f, 0.012f, 1.0f}};

    VkRenderPassBeginInfo rp{};
    rp.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rp.renderPass = g_render_pass;
    rp.framebuffer = g_framebuffers[img_idx];
    rp.renderArea.offset = {0, 0};
    rp.renderArea.extent = g_sc_extent;
    rp.clearValueCount = 1;
    rp.pClearValues = &clear;
    vkCmdBeginRenderPass(g_cmd_buf, &rp, VK_SUBPASS_CONTENTS_INLINE);

    vkCmdBindPipeline(g_cmd_buf, VK_PIPELINE_BIND_POINT_GRAPHICS, g_graphics_pipeline);

    VkViewport viewport{0.0f, 0.0f, (float)g_sc_extent.width, (float)g_sc_extent.height, 0.0f, 1.0f};
    vkCmdSetViewport(g_cmd_buf, 0, 1, &viewport);
    VkRect2D scissor{{0, 0}, g_sc_extent};
    vkCmdSetScissor(g_cmd_buf, 0, 1, &scissor);

    // THE BINDING: ASTRA scientific state → GPU push constants (layout per astra.frag).
    const float pc[4] = {
        (float)g_scene.state.sim_time_s,   // authoritative sim clock
        (float)g_sc_extent.width,
        (float)g_sc_extent.height,
        (float)g_frame_count
    };
    vkCmdPushConstants(g_cmd_buf, g_pipeline_layout,
                       VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), pc);

    vkCmdDraw(g_cmd_buf, 3, 1, 0, 0); // fullscreen triangle

    vkCmdEndRenderPass(g_cmd_buf);
    vkEndCommandBuffer(g_cmd_buf);

    VkPipelineStageFlags wait_stages = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo submit{};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.waitSemaphoreCount = 1;
    submit.pWaitSemaphores = &g_img_sem;
    submit.pWaitDstStageMask = &wait_stages;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &g_cmd_buf;
    submit.signalSemaphoreCount = 1;
    submit.pSignalSemaphores = &g_render_sem;
    VK_CHECK(vkQueueSubmit(g_gfx_queue, 1, &submit, g_fence));

    VkPresentInfoKHR pi{};
    pi.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    pi.waitSemaphoreCount = 1;
    pi.pWaitSemaphores = &g_render_sem;
    pi.swapchainCount = 1;
    pi.pSwapchains = &g_swapchain;
    pi.pImageIndices = &img_idx;
    VkResult presented = vkQueuePresentKHR(g_gfx_queue, &pi);
    if (presented == VK_ERROR_OUT_OF_DATE_KHR || presented == VK_SUBOPTIMAL_KHR) {
        g_swapchain_dirty = true;
    } else if (presented != VK_SUCCESS) {
        printf("[ASTRA] vkQueuePresentKHR failed: %d\n", (int)presented);
        return false;
    }
    return true;
}

// Cleanup
static void cleanup() {
    if (g_device) vkDeviceWaitIdle(g_device);
    if (g_graphics_pipeline) vkDestroyPipeline(g_device, g_graphics_pipeline, nullptr);
    if (g_pipeline_layout) vkDestroyPipelineLayout(g_device, g_pipeline_layout, nullptr);
    if (g_vert_module) vkDestroyShaderModule(g_device, g_vert_module, nullptr);
    if (g_frag_module) vkDestroyShaderModule(g_device, g_frag_module, nullptr);
    if (g_img_sem) vkDestroySemaphore(g_device, g_img_sem, nullptr);
    if (g_render_sem) vkDestroySemaphore(g_device, g_render_sem, nullptr);
    if (g_fence) vkDestroyFence(g_device, g_fence, nullptr);
    if (g_cmd_pool) vkDestroyCommandPool(g_device, g_cmd_pool, nullptr);
    for (auto fb : g_framebuffers) vkDestroyFramebuffer(g_device, fb, nullptr);
    if (g_render_pass) vkDestroyRenderPass(g_device, g_render_pass, nullptr);
    for (auto v : g_sc_views) vkDestroyImageView(g_device, v, nullptr);
    if (g_swapchain) vkDestroySwapchainKHR(g_device, g_swapchain, nullptr);
    if (g_device) vkDestroyDevice(g_device, nullptr);
    if (g_surface) vkDestroySurfaceKHR(g_instance, g_surface, nullptr);
    if (g_instance) vkDestroyInstance(g_instance, nullptr);
    printf("[ASTRA] Clean shutdown\n");
}

// Main entry point
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    AllocConsole();
    freopen("CONOUT$", "w", stdout);
    freopen("CONOUT$", "w", stderr);
    printf("[ASTRA] ASTRA COSMOS v0.1 — Real Vulkan pipeline\n");

    if (!create_window()) { printf("[ASTRA] Window failed\n"); return 1; }
    if (!create_instance()) { printf("[ASTRA] Instance failed\n"); cleanup(); return 1; }
    if (!create_surface()) { printf("[ASTRA] Surface failed\n"); cleanup(); return 1; }
    if (!create_device()) { printf("[ASTRA] Device failed\n"); cleanup(); return 1; }
    if (!create_swapchain()) { printf("[ASTRA] Swapchain failed\n"); cleanup(); return 1; }
    if (!create_render_pass()) { printf("[ASTRA] Render pass failed\n"); cleanup(); return 1; }
    if (!create_framebuffers()) { printf("[ASTRA] Framebuffers failed\n"); cleanup(); return 1; }
    if (!create_commands()) { printf("[ASTRA] Commands failed\n"); cleanup(); return 1; }
    if (!create_sync()) { printf("[ASTRA] Sync failed\n"); cleanup(); return 1; }
    if (!create_pipeline()) { printf("[ASTRA] Pipeline failed\n"); cleanup(); return 1; }

    // Initialize ASTRA engine
    g_scene.state.tick = 0;
    g_scene.state.sim_time_s = 0.0;
    g_scene.state.objects.push_back({"sol", "STAR", {0,0,0}, "world", "REAL", 0, 1.989e30f, 0});
    printf("[ASTRA] Scientific engine initialized\n");
    printf("[ASTRA] Entering persistent loop — ESC to exit\n");

    auto t_start = std::chrono::high_resolution_clock::now();

    // ═══ PERSISTENT MAIN LOOP ═══
    while (!g_quit) {
        MSG msg{};
        while (PeekMessageA(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
            if (msg.message == WM_QUIT) g_quit = true;
        }
        if (g_quit) break;

        // Update ASTRA scientific state
        auto t_now = std::chrono::high_resolution_clock::now();
        float sim_time = std::chrono::duration<float>(t_now - t_start).count();
        g_scene.state.sim_time_s = sim_time;
        g_scene.state.tick = g_frame_count;

        if (!render_frame()) {
            printf("[ASTRA] Render failed\n");
            break;
        }

        g_frame_count++;
        if (g_frame_count % 300 == 0) {
            printf("[ASTRA] frame=%llu sim_time=%.1fs extent=%ux%u\n",
                (unsigned long long)g_frame_count, sim_time,
                g_sc_extent.width, g_sc_extent.height);
        }
    }

    printf("[ASTRA] Shutting down after %llu frames\n", (unsigned long long)g_frame_count);
    cleanup();
    return 0;
}
