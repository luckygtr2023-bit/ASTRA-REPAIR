// ASTRA COSMOS — Production Win32 + Vulkan application (v0.4 Integrated Simulation)
//
// One integrated application: the scientific simulation (mirror of the Python
// authority astra.orbital) drives RenderState; the renderer consumes it every
// frame. No mock Vulkan. Positions/times are simulated truthfully in double
// precision and rebased to renderer floats only at the visualization boundary.
//
// Controls (keyboard, see README):
//   Arrows look | PgUp/PgDn zoom/speed | Tab select+focus | X deselect
//   O free/follow cam | WASDQE move | +/- warp | 0-8 presets | Space pause
//   . step | BKSP epoch-reset | F5 restart | F2/F3 save/load | V vectors
//   P apsis markers | F1 HUD+inspector | ESC quit

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <vulkan/vulkan.h>
#include <vulkan/vulkan_win32.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

// ASTRA engine headers (preserved)
#include "scene/floating_origin.h"
#include "scene/scene.h"
// ASTRA application layer (mirror of the Python scientific authority)
#include "app/celestial_sim.h"
#include "app/orbit_camera.h"
#include "app/hud_state.h"
#include "app/audio_bus.h"
#include "app/persist.h"
#include "app/star_lut.h"

// ─── Application state ────────────────────────────────────────────────────────
static std::vector<astra::app::CelestialBody> g_bodies;
static std::vector<astra::app::Vec3d> g_world;
static std::vector<astra::app::Vec3d> g_vel; // heliocentric km/s (authoritative mirror)
static astra::app::SimClock g_clock;
static astra::app::OrbitCamera g_camera;
static int g_focus = 3;                     // Earth
static const double POS_SCALE = 100.0 / astra::app::AU_KM; // km -> render units
static bool g_keys[256] = {};
// v0.3: camera modes. FOLLOW = orbit camera locked to the focus body.
// FREE = unconstrained WASD movement in the same target-relative frame —
// the renderer NEVER uses absolute float astronomical coordinates anywhere.
enum class CamMode { FOLLOW, FREE };
static CamMode g_cam_mode = CamMode::FOLLOW;
static float g_free_pos[3] = {0.0f, 0.0f, 0.0f}; // target-relative render units
static bool g_show_vectors = true;
static bool g_show_apsis = false;            // peri/apo tick marks (P key)
static bool g_step_once = false;
// v0.4: selection is a distinct authoritative identity from the camera target.
// g_selection = inspector/highlight identity; g_focus = camera target.
// Tab changes both together (normal exploration), X clears selection only.
static int g_selection = 3;
static astra::app::AudioBus g_audio;          // event routing; classified
static std::string g_viz_mode = "orbital";    // hud-reported visualization mode
static double g_last_fps = 0.0;               // REAL measured (for HUD/inspector)

// ─── Window state ─────────────────────────────────────────────────────────────
static HWND g_hwnd = nullptr;
static bool g_quit = false;
static bool g_minimized = false;
static uint32_t g_width = 1280, g_height = 720;

// ─── Vulkan core ──────────────────────────────────────────────────────────────
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

// Depth buffer
static VkImage g_depth_img = VK_NULL_HANDLE;
static VkDeviceMemory g_depth_mem = VK_NULL_HANDLE;
static VkImageView g_depth_view = VK_NULL_HANDLE;
static VkFormat g_depth_format = VK_FORMAT_D32_SFLOAT;

// Geometry (unit icosphere, shared by every body draw)
static VkBuffer g_vb = VK_NULL_HANDLE;
static VkDeviceMemory g_vb_mem = VK_NULL_HANDLE;
static VkBuffer g_ib = VK_NULL_HANDLE;
static VkDeviceMemory g_ib_mem = VK_NULL_HANDLE;
static uint32_t g_index_count = 0;

// Pipelines (single 128-byte push-constant layout shared by all)
static VkPipelineLayout g_pipeline_layout = VK_NULL_HANDLE;
static VkPipeline g_pipe_bg = VK_NULL_HANDLE;    // fullscreen starfield
static VkPipeline g_pipe_mesh = VK_NULL_HANDLE;  // lit celestial bodies
static VkPipeline g_pipe_orbit = VK_NULL_HANDLE; // Kepler trajectory overlays
static VkPipeline g_pipe_vector = VK_NULL_HANDLE; // velocity vectors
static VkShaderModule g_vert_module = VK_NULL_HANDLE;
static VkShaderModule g_frag_module = VK_NULL_HANDLE;

// ASTRA RenderState binding (mirrors bridge contract; truthful per frame)
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

static bool load_shader(const char* name, std::vector<uint32_t>& out) {
    const std::string dir = exe_dir();
    const std::string bases[] = {dir + "\\shaders\\", ".\\shaders\\", dir + "\\", ".\\"};
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

// ─── Window / input ───────────────────────────────────────────────────────────
static void focus_body(int idx, bool reset_view);

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
                g_width = w; g_height = h; g_swapchain_dirty = true;
            }
        }
        return 0;
    case WM_KEYUP:
        if (wp < 256) g_keys[wp] = false;
        return 0;
    case WM_KEYDOWN: {
        if (wp < 256) g_keys[wp] = true;
        const bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
        switch (wp) {
        case VK_ESCAPE: g_quit = true; break;
        case VK_TAB: {
            const int next = (g_focus + (shift ? -1 : 1) + (int)g_bodies.size()) % (int)g_bodies.size();
            g_selection = next;            // selection identity travels with the camera target on Tab
            focus_body(next, true);
            g_audio.push(astra::app::AudioEventKind::UI_SELECT, g_bodies[(size_t)g_selection].name, g_clock.sim_time_s);
            break;
        }
        case 'X':                          // deselect (camera target unchanged)
            if (g_selection >= 0) {
                g_selection = -1;
                g_audio.push(astra::app::AudioEventKind::UI_DESELECT, "selection", g_clock.sim_time_s);
            }
            break;
        case VK_SPACE:
            g_clock.paused = !g_clock.paused;
            g_audio.push(g_clock.paused ? astra::app::AudioEventKind::SIM_PAUSE
                                        : astra::app::AudioEventKind::SIM_RESUME, "clock", g_clock.sim_time_s);
            break;
        case VK_HOME:  focus_body(g_focus, true); break;
        case VK_OEM_PLUS: case VK_ADD:
            g_clock.warp = astra::app::SimClock::clamp_warp(g_clock.warp * 2.0);
            g_audio.push(astra::app::AudioEventKind::SIM_WARP, "clock", g_clock.sim_time_s);
            break;
        case VK_OEM_MINUS: case VK_SUBTRACT:
            g_clock.warp = astra::app::SimClock::clamp_warp(g_clock.warp / 2.0);
            g_audio.push(astra::app::AudioEventKind::SIM_WARP, "clock", g_clock.sim_time_s);
            break;
        case VK_OEM_PERIOD: g_step_once = true; g_audio.push(astra::app::AudioEventKind::SIM_STEP, "clock", g_clock.sim_time_s); break;
        case VK_BACK:    g_clock.sim_time_s = 0.0; g_audio.push(astra::app::AudioEventKind::SIM_RESET, "clock", g_clock.sim_time_s); break;
        case VK_F5:      g_clock.sim_time_s = 0.0; g_clock.paused = false; g_audio.push(astra::app::AudioEventKind::SIM_RESTART, "clock", g_clock.sim_time_s); break;
        case VK_F2: {    // F2 save scenario (persistence; traversal-safe names)
            astra::app::ScenarioSave s{};
            s.sim_time_s = g_clock.sim_time_s; s.warp = g_clock.warp; s.paused = g_clock.paused;
            s.focus = g_focus; s.selection = g_selection;
            s.cam_mode = (g_cam_mode == CamMode::FREE) ? 1 : 0;
            s.cam_azimuth = g_camera.azimuth; s.cam_elevation = g_camera.elevation; s.cam_distance = g_camera.distance;
            for (int k = 0; k < 3; ++k) s.free_pos[k] = g_free_pos[k];
            s.show_vectors = g_show_vectors; s.viz_mode = g_viz_mode;
            CreateDirectoryA((exe_dir() + "\\saves").c_str(), nullptr);
            if (astra::app::save_scenario_file(exe_dir() + "\\saves", "scenario_1.json", s)) {
                printf("[ASTRA] Scenario saved: %s\\saves\\scenario_1.json\n", exe_dir().c_str());
                g_audio.push(astra::app::AudioEventKind::SCENARIO_SAVE, "scenario_1", g_clock.sim_time_s);
            } else {
                printf("[ASTRA] Scenario save FAILED (disk)\n");
            }
            break;
        }
        case VK_F3: {    // F3 load scenario (strict parse: no silent state discard)
            astra::app::ScenarioSave s{};
            if (astra::app::load_scenario_file(exe_dir() + "\\saves", "scenario_1.json", s)) {
                g_clock.sim_time_s = s.sim_time_s; g_clock.warp = s.warp; g_clock.paused = s.paused;
                g_focus = std::clamp(s.focus, 0, (int)g_bodies.size() - 1);
                g_selection = (s.selection >= 0 && s.selection < (int)g_bodies.size()) ? s.selection : -1;
                g_cam_mode = (s.cam_mode == 1) ? CamMode::FREE : CamMode::FOLLOW;
                g_camera.azimuth = s.cam_azimuth; g_camera.elevation = s.cam_elevation;
                g_camera.distance = s.cam_distance;
                for (int k = 0; k < 3; ++k) g_free_pos[k] = s.free_pos[k];
                g_show_vectors = s.show_vectors; g_viz_mode = s.viz_mode;
                printf("[ASTRA] Scenario loaded: sim=%.3f d warp=x%.0f focus=%s sel=%s\n",
                       s.sim_time_s / 86400.0, s.warp, g_bodies[(size_t)g_focus].name.c_str(),
                       g_selection >= 0 ? g_bodies[(size_t)g_selection].name.c_str() : "none");
                g_audio.push(astra::app::AudioEventKind::SCENARIO_LOAD, "scenario_1", g_clock.sim_time_s);
            } else {
                printf("[ASTRA] Scenario load: no valid save at %s\\saves\\scenario_1.json (NOT AVAILABLE)\n", exe_dir().c_str());
            }
            break;
        }
        case '0': case '1': case '2': case '3': case '4': case '5': case '6': case '7': case '8': {
            const double warp_lut[] = {1.0, 10.0, 100.0, 1e3, 1e4, 1e5, 1e6, 1e7, 1e8};
            g_clock.warp = warp_lut[wp - '0'];
            g_audio.push(astra::app::AudioEventKind::SIM_WARP, "clock", g_clock.sim_time_s);
            break;
        }
        case 'O': {
            if (g_cam_mode == CamMode::FOLLOW) {
                // Enter FREE camera at the current eye position (target-relative).
                g_camera.eye_offset(g_free_pos);
                g_cam_mode = CamMode::FREE;
            } else {
                g_cam_mode = CamMode::FOLLOW;
            }
            g_audio.push(astra::app::AudioEventKind::UI_MODE, "camera", g_clock.sim_time_s);
            break;
        }
        case 'V': g_show_vectors = !g_show_vectors; g_viz_mode = g_show_vectors ? "velocity" : "orbital"; break;
        case 'P': g_show_apsis = !g_show_apsis; break;   // peri/apo tick marks
        case VK_F1: {
            extern void dump_inspector();
            dump_inspector();
            break;
        }
        }
        return 0;
    }
    }
    return DefWindowProcA(hwnd, msg, wp, lp);
}

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

// ─── Vulkan bootstrap (instance/surface/device/swapchain) ─────────────────────
static bool create_instance() {
    VkApplicationInfo app_info{};
    app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app_info.pApplicationName = "ASTRA COSMOS";
    app_info.applicationVersion = VK_MAKE_VERSION(0, 2, 0);
    app_info.pEngineName = "ASTRA Native";
    app_info.engineVersion = VK_MAKE_VERSION(0, 2, 0);
    app_info.apiVersion = VK_API_VERSION_1_3;

    const char* extensions[] = {VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_WIN32_SURFACE_EXTENSION_NAME};
    VkInstanceCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ci.pApplicationInfo = &app_info;
    ci.enabledExtensionCount = 2;
    ci.ppEnabledExtensionNames = extensions;
    VK_CHECK(vkCreateInstance(&ci, nullptr, &g_instance));
    printf("[ASTRA] Vulkan instance created\n");
    return true;
}

static bool create_surface() {
    VkWin32SurfaceCreateInfoKHR ci{};
    ci.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
    ci.hinstance = GetModuleHandleA(nullptr);
    ci.hwnd = g_hwnd;
    VK_CHECK(vkCreateWin32SurfaceKHR(g_instance, &ci, nullptr, &g_surface));
    printf("[ASTRA] Surface created\n");
    return true;
}

static bool create_device() {
    uint32_t count = 0;
    vkEnumeratePhysicalDevices(g_instance, &count, nullptr);
    if (count == 0) return false;
    std::vector<VkPhysicalDevice> devs(count);
    vkEnumeratePhysicalDevices(g_instance, &count, devs.data());

    int best_score = -1;
    for (auto& d : devs) {
        VkPhysicalDeviceProperties p{};
        vkGetPhysicalDeviceProperties(d, &p);
        int score = (p.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)   ? 3
                  : (p.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU) ? 2
                  : (p.deviceType == VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU)    ? 1 : 0;
        if (score > best_score) { best_score = score; g_gpu = d; }
    }

    VkPhysicalDeviceProperties props{};
    vkGetPhysicalDeviceProperties(g_gpu, &props);
    printf("[ASTRA] GPU: %s\n", props.deviceName);

    uint32_t qfc = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(g_gpu, &qfc, nullptr);
    std::vector<VkQueueFamilyProperties> qfs(qfc);
    vkGetPhysicalDeviceQueueFamilyProperties(g_gpu, &qfc, qfs.data());

    g_gfx_family = UINT32_MAX;
    for (uint32_t i = 0; i < qfc; i++) {
        if (qfs[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            VkBool32 present = VK_FALSE;
            vkGetPhysicalDeviceSurfaceSupportKHR(g_gpu, i, g_surface, &present);
            if (present) { g_gfx_family = i; break; }
        }
    }
    if (g_gfx_family == UINT32_MAX) return false;

    float prio = 1.0f;
    VkDeviceQueueCreateInfo qci{};
    qci.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    qci.queueFamilyIndex = g_gfx_family;
    qci.queueCount = 1;
    qci.pQueuePriorities = &prio;

    const char* dev_exts[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
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

static bool create_swapchain() {
    VkSurfaceCapabilitiesKHR caps{};
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(g_gpu, g_surface, &caps);

    uint32_t fmt_count = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(g_gpu, g_surface, &fmt_count, nullptr);
    if (fmt_count == 0) { printf("[ASTRA] Surface exposes no formats\n"); return false; }
    std::vector<VkSurfaceFormatKHR> fmts(fmt_count);
    vkGetPhysicalDeviceSurfaceFormatsKHR(g_gpu, g_surface, &fmt_count, fmts.data());

    VkSurfaceFormatKHR fmt = fmts[0];
    for (auto& f : fmts) {
        if (f.format == VK_FORMAT_B8G8R8A8_SRGB && f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) { fmt = f; break; }
    }

    VkExtent2D extent = caps.currentExtent;
    if (extent.width == UINT32_MAX) { extent.width = g_width; extent.height = g_height; }
    extent.width = std::clamp(extent.width, caps.minImageExtent.width, caps.maxImageExtent.width);
    extent.height = std::clamp(extent.height, caps.minImageExtent.height, caps.maxImageExtent.height);
    if (extent.width == 0 || extent.height == 0) return false;

    uint32_t image_count = caps.minImageCount + 1;
    if (caps.maxImageCount > 0 && image_count > caps.maxImageCount) image_count = caps.maxImageCount;

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
    sci.presentMode = VK_PRESENT_MODE_FIFO_KHR;
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

// ─── Memory / buffers ─────────────────────────────────────────────────────────
static uint32_t find_memory_type(uint32_t type_bits, VkMemoryPropertyFlags props) {
    VkPhysicalDeviceMemoryProperties mp{};
    vkGetPhysicalDeviceMemoryProperties(g_gpu, &mp);
    for (uint32_t i = 0; i < mp.memoryTypeCount; i++) {
        if ((type_bits & (1u << i)) && (mp.memoryTypes[i].propertyFlags & props) == props) return i;
    }
    return UINT32_MAX;
}

static bool create_upload_buffer(VkDeviceSize size, VkBufferUsageFlags usage,
                                 VkBuffer& buf, VkDeviceMemory& mem) {
    VkBufferCreateInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bi.size = size;
    bi.usage = usage;
    bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VK_CHECK(vkCreateBuffer(g_device, &bi, nullptr, &buf));
    VkMemoryRequirements req{};
    vkGetBufferMemoryRequirements(g_device, buf, &req);
    VkMemoryAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.allocationSize = req.size;
    ai.memoryTypeIndex = find_memory_type(req.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (ai.memoryTypeIndex == UINT32_MAX) return false;
    VK_CHECK(vkAllocateMemory(g_device, &ai, nullptr, &mem));
    VK_CHECK(vkBindBufferMemory(g_device, buf, mem, 0));
    return true;
}

// ─── Depth buffer ─────────────────────────────────────────────────────────────
static VkFormat choose_depth_format() {
    const VkFormat cands[] = {VK_FORMAT_D32_SFLOAT, VK_FORMAT_X8_D24_UNORM_PACK32, VK_FORMAT_D16_UNORM};
    for (VkFormat f : cands) {
        VkFormatProperties p{};
        vkGetPhysicalDeviceFormatProperties(g_gpu, f, &p);
        if (p.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) return f;
    }
    return VK_FORMAT_D32_SFLOAT;
}

static bool create_depth() {
    g_depth_format = choose_depth_format();
    VkImageCreateInfo ii{};
    ii.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ii.imageType = VK_IMAGE_TYPE_2D;
    ii.format = g_depth_format;
    ii.extent = {g_sc_extent.width, g_sc_extent.height, 1};
    ii.mipLevels = 1;
    ii.arrayLayers = 1;
    ii.samples = VK_SAMPLE_COUNT_1_BIT;
    ii.tiling = VK_IMAGE_TILING_OPTIMAL;
    ii.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    ii.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VK_CHECK(vkCreateImage(g_device, &ii, nullptr, &g_depth_img));

    VkMemoryRequirements req{};
    vkGetImageMemoryRequirements(g_device, g_depth_img, &req);
    VkMemoryAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.allocationSize = req.size;
    ai.memoryTypeIndex = find_memory_type(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (ai.memoryTypeIndex == UINT32_MAX) return false;
    VK_CHECK(vkAllocateMemory(g_device, &ai, nullptr, &g_depth_mem));
    VK_CHECK(vkBindImageMemory(g_device, g_depth_img, g_depth_mem, 0));

    VkImageViewCreateInfo vi{};
    vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    vi.image = g_depth_img;
    vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vi.format = g_depth_format;
    vi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    vi.subresourceRange.baseMipLevel = 0;
    vi.subresourceRange.levelCount = 1;
    vi.subresourceRange.baseArrayLayer = 0;
    vi.subresourceRange.layerCount = 1;
    VK_CHECK(vkCreateImageView(g_device, &vi, nullptr, &g_depth_view));
    return true;
}

// ─── Unit icosphere (subdivision 2: 162 vertices, 960 indices) ────────────────
struct MeshData { std::vector<float> verts; std::vector<uint16_t> indices; };

static MeshData make_icosphere(int subdivisions) {
    const float t = 1.61803398875f;
    const float inv = 1.0f / std::sqrt(1.0f + t * t);
    std::vector<float> v = {
        -inv,  t*inv, 0,   inv,  t*inv, 0,  -inv, -t*inv, 0,   inv, -t*inv, 0,
        0, -inv,  t*inv,   0,  inv,  t*inv,  0, -inv, -t*inv,   0,  inv, -t*inv,
         t*inv, 0, -inv,    t*inv, 0,  inv,   -t*inv, 0, -inv,   -t*inv, 0,  inv};
    std::vector<uint16_t> idx = {
        0,11,5, 0,5,1, 0,1,7, 0,7,10, 0,10,11, 1,5,9, 5,11,4, 11,10,2, 10,7,6, 7,1,8,
        3,9,4, 3,4,2, 3,2,6, 3,6,8, 3,8,9, 4,9,5, 2,4,11, 6,2,10, 8,6,7, 9,8,1};
    for (int s = 0; s < subdivisions; ++s) {
        std::unordered_map<uint32_t, uint16_t> cache;
        std::vector<uint16_t> out;
        auto midpoint = [&](uint16_t a, uint16_t b) -> uint16_t {
            uint32_t key = (a < b) ? ((uint32_t)a << 16 | b) : ((uint32_t)b << 16 | a);
            auto it = cache.find(key);
            if (it != cache.end()) return it->second;
            float x = (v[a*3+0] + v[b*3+0]) * 0.5f;
            float y = (v[a*3+1] + v[b*3+1]) * 0.5f;
            float z = (v[a*3+2] + v[b*3+2]) * 0.5f;
            float len = std::sqrt(x*x + y*y + z*z);
            uint16_t id = (uint16_t)(v.size() / 3);
            v.push_back(x/len); v.push_back(y/len); v.push_back(z/len);
            cache.emplace(key, id);
            return id;
        };
        for (size_t i = 0; i + 2 < idx.size(); i += 3) {
            uint16_t a = idx[i], b = idx[i+1], c = idx[i+2];
            uint16_t ab = midpoint(a, b), bc = midpoint(b, c), ca = midpoint(c, a);
            out.insert(out.end(), {a, ab, ca, b, bc, ab, c, ca, bc, ab, bc, ca});
        }
        idx.swap(out);
    }
    return {v, idx};
}

static bool create_geometry() {
    MeshData mesh = make_icosphere(2);
    g_index_count = (uint32_t)mesh.indices.size();
    const VkDeviceSize vb_size = mesh.verts.size() * sizeof(float);
    const VkDeviceSize ib_size = mesh.indices.size() * sizeof(uint16_t);
    if (!create_upload_buffer(vb_size, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, g_vb, g_vb_mem)) return false;
    if (!create_upload_buffer(ib_size, VK_BUFFER_USAGE_INDEX_BUFFER_BIT, g_ib, g_ib_mem)) return false;
    void* p = nullptr;
    VK_CHECK(vkMapMemory(g_device, g_vb_mem, 0, vb_size, 0, &p));
    memcpy(p, mesh.verts.data(), (size_t)vb_size);
    vkUnmapMemory(g_device, g_vb_mem);
    p = nullptr;
    VK_CHECK(vkMapMemory(g_device, g_ib_mem, 0, ib_size, 0, &p));
    memcpy(p, mesh.indices.data(), (size_t)ib_size);
    vkUnmapMemory(g_device, g_ib_mem);
    printf("[ASTRA] Geometry: icosphere %u indices\n", g_index_count);
    return true;
}

// ─── Render pass (color + depth) ──────────────────────────────────────────────
static bool create_render_pass() {
    VkAttachmentDescription atts[2]{};
    atts[0].format = g_sc_format;
    atts[0].samples = VK_SAMPLE_COUNT_1_BIT;
    atts[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    atts[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    atts[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    atts[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    atts[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    atts[0].finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    atts[1].format = g_depth_format;
    atts[1].samples = VK_SAMPLE_COUNT_1_BIT;
    atts[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    atts[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    atts[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    atts[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    atts[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    atts[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference color_ref{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkAttachmentReference depth_ref{1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
    VkSubpassDescription sub{};
    sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    sub.colorAttachmentCount = 1;
    sub.pColorAttachments = &color_ref;
    sub.pDepthStencilAttachment = &depth_ref;

    VkSubpassDependency dep{};
    dep.srcSubpass = VK_SUBPASS_EXTERNAL;
    dep.dstSubpass = 0;
    dep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dep.srcAccessMask = 0;
    dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    ci.attachmentCount = 2;
    ci.pAttachments = atts;
    ci.subpassCount = 1;
    ci.pSubpasses = &sub;
    ci.dependencyCount = 1;
    ci.pDependencies = &dep;
    VK_CHECK(vkCreateRenderPass(g_device, &ci, nullptr, &g_render_pass));
    return true;
}

static bool create_framebuffers() {
    g_framebuffers.resize(g_sc_views.size());
    for (size_t i = 0; i < g_sc_views.size(); i++) {
        VkImageView atts[2] = {g_sc_views[i], g_depth_view};
        VkFramebufferCreateInfo ci{};
        ci.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        ci.renderPass = g_render_pass;
        ci.attachmentCount = 2;
        ci.pAttachments = atts;
        ci.width = g_sc_extent.width;
        ci.height = g_sc_extent.height;
        ci.layers = 1;
        VK_CHECK(vkCreateFramebuffer(g_device, &ci, nullptr, &g_framebuffers[i]));
    }
    return true;
}

static bool create_commands() {
    VkCommandPoolCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    ci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    ci.queueFamilyIndex = g_gfx_family;
    VK_CHECK(vkCreateCommandPool(g_device, &ci, nullptr, &g_cmd_pool));

    VkCommandBufferAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    ai.commandPool = g_cmd_pool;
    ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = 1;
    VK_CHECK(vkAllocateCommandBuffers(g_device, &ai, &g_cmd_buf));
    return true;
}

static bool create_sync() {
    VkSemaphoreCreateInfo si{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    VkFenceCreateInfo fi{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, nullptr, VK_FENCE_CREATE_SIGNALED_BIT};
    VK_CHECK(vkCreateSemaphore(g_device, &si, nullptr, &g_img_sem));
    VK_CHECK(vkCreateSemaphore(g_device, &si, nullptr, &g_render_sem));
    VK_CHECK(vkCreateFence(g_device, &fi, nullptr, &g_fence));
    return true;
}

// ─── Pipelines (shared 128-byte push-constant layout) ─────────────────────────
static bool make_pipeline(const uint32_t* vs, size_t vs_words, const uint32_t* fs, size_t fs_words,
                          VkPrimitiveTopology topo, bool depth_test, bool depth_write,
                          bool with_vertex_input, VkPipeline& out) {
    VkShaderModule vm = create_shader_module(std::vector<uint32_t>(vs, vs + vs_words));
    VkShaderModule fm = create_shader_module(std::vector<uint32_t>(fs, fs + fs_words));
    if (!vm || !fm) { printf("[ASTRA] shader module failed\n"); return false; }

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vm;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fm;
    stages[1].pName = "main";

    VkVertexInputBindingDescription binding{};
    binding.binding = 0;
    binding.stride = 3 * sizeof(float);
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    VkVertexInputAttributeDescription attr{};
    attr.location = 0;
    attr.binding = 0;
    attr.format = VK_FORMAT_R32G32B32_SFLOAT;
    attr.offset = 0;

    VkPipelineVertexInputStateCreateInfo vi{};
    vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    if (with_vertex_input) {
        vi.vertexBindingDescriptionCount = 1;
        vi.pVertexBindingDescriptions = &binding;
        vi.vertexAttributeDescriptionCount = 1;
        vi.pVertexAttributeDescriptions = &attr;
    }

    VkPipelineInputAssemblyStateCreateInfo ia{};
    ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    ia.topology = topo;

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

    VkPipelineDepthStencilStateCreateInfo ds{};
    ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    ds.depthTestEnable = depth_test ? VK_TRUE : VK_FALSE;
    ds.depthWriteEnable = depth_write ? VK_TRUE : VK_FALSE;
    ds.depthCompareOp = VK_COMPARE_OP_LESS;
    ds.depthBoundsTestEnable = VK_FALSE;
    ds.stencilTestEnable = VK_FALSE;

    VkPipelineColorBlendAttachmentState cba{};
    cba.blendEnable = VK_FALSE;
    cba.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                         VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    VkPipelineColorBlendStateCreateInfo cb{};
    cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    cb.attachmentCount = 1;
    cb.pAttachments = &cba;

    const VkDynamicState dyn_states[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dy{};
    dy.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dy.dynamicStateCount = 2;
    dy.pDynamicStates = dyn_states;

    VkGraphicsPipelineCreateInfo gp{};
    gp.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    gp.stageCount = 2;
    gp.pStages = stages;
    gp.pVertexInputState = &vi;
    gp.pInputAssemblyState = &ia;
    gp.pViewportState = &vp;
    gp.pRasterizationState = &rs;
    gp.pMultisampleState = &ms;
    gp.pDepthStencilState = &ds;
    gp.pColorBlendState = &cb;
    gp.pDynamicState = &dy;
    gp.layout = g_pipeline_layout;
    gp.renderPass = g_render_pass;
    gp.subpass = 0;
    VkResult res = vkCreateGraphicsPipelines(g_device, VK_NULL_HANDLE, 1, &gp, nullptr, &out);
    vkDestroyShaderModule(g_device, vm, nullptr);
    vkDestroyShaderModule(g_device, fm, nullptr);
    if (res != VK_SUCCESS) { printf("[ASTRA] vkCreateGraphicsPipelines failed: %d\n", (int)res); return false; }
    return true;
}

static bool create_pipelines() {
    // One layout: 128-byte push-constant range used by bg/mesh/orbit draws.
    VkPushConstantRange pcr{};
    pcr.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pcr.offset = 0;
    pcr.size = 128;
    VkPipelineLayoutCreateInfo pli{};
    pli.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pli.pushConstantRangeCount = 1;
    pli.pPushConstantRanges = &pcr;
    VK_CHECK(vkCreatePipelineLayout(g_device, &pli, nullptr, &g_pipeline_layout));

    std::vector<uint32_t> astra_v, astra_f, sphere_v, sphere_f, orbit_v, orbit_f, vector_v;
    if (!load_shader("astra.vert.spv", astra_v)) return false;
    if (!load_shader("astra.frag.spv", astra_f)) return false;
    if (!load_shader("sphere.vert.spv", sphere_v)) return false;
    if (!load_shader("sphere.frag.spv", sphere_f)) return false;
    if (!load_shader("orbit.vert.spv", orbit_v)) return false;
    if (!load_shader("orbit.frag.spv", orbit_f)) return false;
    if (!load_shader("vector.vert.spv", vector_v)) return false;

    if (!make_pipeline(astra_v.data(), astra_v.size(), astra_f.data(), astra_f.size(),
                       VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, false, false, false, g_pipe_bg)) return false;
    if (!make_pipeline(sphere_v.data(), sphere_v.size(), sphere_f.data(), sphere_f.size(),
                       VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, true, true, true, g_pipe_mesh)) return false;
    if (!make_pipeline(orbit_v.data(), orbit_v.size(), orbit_f.data(), orbit_f.size(),
                       VK_PRIMITIVE_TOPOLOGY_LINE_STRIP, true, false, false, g_pipe_orbit)) return false;
    if (!make_pipeline(vector_v.data(), vector_v.size(), orbit_f.data(), orbit_f.size(),
                       VK_PRIMITIVE_TOPOLOGY_LINE_STRIP, true, false, false, g_pipe_vector)) return false;

    printf("[ASTRA] Pipelines created: background + bodies + orbit overlays + velocity vectors\n");
    return true;
}

// ─── Simulation → RenderState → draw data ─────────────────────────────────────
static float visual_radius_units(const astra::app::CelestialBody& b) {
    // Sublinear radius exaggeration — CINEMATIC visualization transform only;
    // scientific state keeps true SI values (see title-bar inspector).
    double rv = std::pow(b.radius_km / astra::app::R_EARTH_KM, 0.35) * 1.6;
    if (rv < 0.9) rv = 0.9;
    return (float)rv;
}

// CINEMATIC velocity-vector length (render units); direction is scientific,
// length is readability scaling only (true km/s shown in the inspector).
static double VisualVectorScale(double vmag_km_s) {
    return 4.0 + 0.12 * vmag_km_s;
}

static astra::app::Mat4 model_of(float px, float py, float pz, float s) {
    astra::app::Mat4 m = astra::app::Mat4::identity();
    m.m[0] = s; m.m[5] = s; m.m[10] = s;
    m.m[12] = px; m.m[13] = py; m.m[14] = pz;
    return m;
}

static void sim_tick(double real_dt_s) {
    if (g_step_once) {
        // Single simulation step: one wall-frame worth of warped sim time.
        g_clock.paused = true;
        g_clock.sim_time_s += g_clock.warp / 60.0;
        g_step_once = false;
    } else {
        g_clock.advance(real_dt_s);
    }
    g_world = astra::app::propagate_world(g_bodies, g_clock.sim_time_s);
    g_vel = astra::app::propagate_world_velocity(g_bodies, g_clock.sim_time_s);

    // Keep the ASTRA RenderState binding truthful (double authority).
    g_scene.state.sim_time_s = g_clock.sim_time_s;
    g_scene.state.tick = g_frame_count;
    g_scene.state.objects.clear();
    g_scene.state.objects.reserve(g_bodies.size());
    for (size_t i = 0; i < g_bodies.size(); ++i) {
        const char* kind = g_bodies[i].kind == astra::app::BodyKind::STAR ? "STAR"
                         : g_bodies[i].kind == astra::app::BodyKind::PLANET ? "PLANET" : "MOON";
        std::string frame = g_bodies[i].parent >= 0
            ? "heliocentric (parent=" + g_bodies[(size_t)g_bodies[i].parent].name + ")"
            : "heliocentric";
        g_scene.state.objects.push_back(
            {g_bodies[i].name, kind,
             {g_world[i][0], g_world[i][1], g_world[i][2]},
             frame, g_bodies[i].classification, 0,
             (float)g_bodies[i].mass_kg, 0,
             {g_vel[i][0], g_vel[i][1], g_vel[i][2]}});
    }
    // Single selection identity across camera/inspector/RenderState/highlight.
    g_scene.state.selected_id = (g_selection >= 0 && g_selection < (int)g_bodies.size())
        ? g_bodies[(size_t)g_selection].name : "";
    g_scene.state.camera.mode = (g_cam_mode == CamMode::FOLLOW) ? "orbit-follow" : "free";
    g_scene.state.camera.target = g_bodies[(size_t)g_focus].name;
}

static void focus_body(int idx, bool reset_view) {
    if (g_bodies.empty()) return;
    g_focus = ((idx % (int)g_bodies.size()) + (int)g_bodies.size()) % (int)g_bodies.size();
    if (reset_view) {
        g_camera.distance = visual_radius_units(g_bodies[(size_t)g_focus]) * 14.0;
        g_camera.clamp_distance(6.0, 6000.0);
    }
}

static void update_camera_from_input(double real_dt_s) {
    const double rot = 0.06;
    double daz = 0.0, del = 0.0;
    if (g_keys[VK_LEFT])  daz -= rot;
    if (g_keys[VK_RIGHT]) daz += rot;
    if (g_keys[VK_UP])    del += rot * 0.6;
    if (g_keys[VK_DOWN])  del -= rot * 0.6;
    if (daz != 0.0 || del != 0.0) g_camera.rotate(daz, del);

    if (g_cam_mode == CamMode::FOLLOW) {
        if (g_keys[VK_PRIOR]) g_camera.zoom(1.10);  // PgUp
        if (g_keys[VK_NEXT])  g_camera.zoom(1.0 / 1.10); // PgDn
        g_camera.clamp_distance(2.0, 20000.0);
    } else {
        // FREE camera: WASD in the camera plane, Q/E vertical, SHIFT = fast.
        const float az = (float)g_camera.azimuth, el = (float)g_camera.elevation;
        const float fwd[3] = {-(float)std::cos(el) * std::cos(az), -(float)std::sin(el), -(float)std::cos(el) * std::sin(az)};
        const float up[3] = {0.0f, 1.0f, 0.0f};
        const float right[3] = {(float)std::sin(az), 0.0f, -(float)std::cos(az)};
        // Pitch-aware up vector for E/Q.
        const float cup[3] = {fwd[1] * right[2] - fwd[2] * right[1], fwd[2] * right[0] - fwd[0] * right[2], fwd[0] * right[1] - fwd[1] * right[0]};
        float m[3] = {0.0f, 0.0f, 0.0f};
        if (g_keys['W']) for (int k = 0; k < 3; ++k) m[k] += fwd[k];
        if (g_keys['S']) for (int k = 0; k < 3; ++k) m[k] -= fwd[k];
        if (g_keys['D']) for (int k = 0; k < 3; ++k) m[k] += right[k];
        if (g_keys['A']) for (int k = 0; k < 3; ++k) m[k] -= right[k];
        if (g_keys['E']) for (int k = 0; k < 3; ++k) m[k] += cup[k];
        if (g_keys['Q']) for (int k = 0; k < 3; ++k) m[k] -= cup[k];
        (void)up;
        double speed = 8.0; // render units/s; scales with PgUp/PgDn multipliers
        if (g_keys[VK_SHIFT]) speed *= 32.0;
        if (g_keys[VK_PRIOR]) speed *= 8.0;
        if (g_keys[VK_NEXT]) speed /= 8.0;
        for (int k = 0; k < 3; ++k) g_free_pos[k] += m[k] * (float)(speed * real_dt_s);
        // Limit drift so float precision stays healthy (target-relative).
        for (int k = 0; k < 3; ++k) {
            if (g_free_pos[k] > 40000.0f) g_free_pos[k] = 40000.0f;
            if (g_free_pos[k] < -40000.0f) g_free_pos[k] = -40000.0f;
        }
    }
}

// v0.4: HUD snapshot from authoritative state (single mapping, tested by
// native_renderer/tests/v04_gates.cpp: NOT AVAILABLE semantics included).
static astra::app::HudSnapshot make_hud_snapshot(double fps, double frame_ms) {
    astra::app::HudSnapshot s{};
    s.sim_time_s = g_clock.sim_time_s;
    s.warp = g_clock.warp;
    s.paused = g_clock.paused;
    s.fps = fps;
    s.frame_ms = frame_ms;
    s.cam_mode = (g_cam_mode == CamMode::FREE) ? 1 : 0;
    s.reference_frame = "heliocentric";
    s.viz_mode = g_viz_mode;
    s.selected_index = g_selection;
    if (g_selection >= 0 && g_selection < (int)g_bodies.size()) {
        const auto& b = g_bodies[(size_t)g_selection];
        s.selected_name = b.name;
        s.selected_classification = b.classification;
        s.has_selected_kind = true;
        s.selected_kind = b.kind == astra::app::BodyKind::STAR ? "STAR"
                         : b.kind == astra::app::BodyKind::PLANET ? "PLANET" : "MOON";
        const astra::app::Vec3d& w = g_world[(size_t)g_selection];
        const astra::app::Vec3d& v = g_vel[(size_t)g_selection];
        const astra::app::Vec3d& tw = g_world[(size_t)g_focus]; // observer = camera target
        s.has_selected_state = true;
        s.r_helio_km = std::sqrt(w[0]*w[0] + w[1]*w[1] + w[2]*w[2]);
        s.speed_km_s = std::sqrt(v[0]*v[0] + v[1]*v[1] + v[2]*v[2]);
        s.observer_distance_km = std::sqrt((w[0]-tw[0])*(w[0]-tw[0]) + (w[1]-tw[1])*(w[1]-tw[1]) + (w[2]-tw[2])*(w[2]-tw[2]));
        s.light_delay_s = s.observer_distance_km / 299792.458;
    }
    return s;
}

static void update_inspector_title(double fps) {
    static double frame_ms = 0.0;
    frame_ms = (fps > 1e-6) ? 1000.0 / fps : 0.0;
    const auto snap = make_hud_snapshot(fps, frame_ms);
    const std::string line = astra::app::hud_summary_line(snap);
    // v0.4 title: single-line scientific HUD (full matrix on F1).
    SetWindowTextA(g_hwnd, ("ASTRA COSMOS v0.4  " + line).c_str());
}

void dump_inspector() {
    // v0.4: HUD matrix first (authoritative mapping incl. NOT AVAILABLE).
    const auto snap = make_hud_snapshot(g_last_fps, (g_last_fps > 1e-6) ? 1000.0 / g_last_fps : 0.0);
    const auto hud_lines = astra::app::render_hud_lines(astra::app::build_hud(snap));
    printf("\n[ASTRA] ═══ SCIENTIFIC HUD ═══\n");
    for (const auto& l : hud_lines) printf("[HUD] %s\n", l.c_str());
    printf("\n[ASTRA] ═══ SCIENTIFIC INSPECTOR (sim epoch J2000 %+.4f yr = sim %+.2f d) ═══\n",
           g_clock.sim_time_s / astra::app::DAY_S / astra::app::YEAR_D,
           g_clock.sim_time_s / astra::app::DAY_S);
    printf("[ASTRA] Focus(camera)=%s  Selection=%s · cam=%s · warp=x%.0f %s\n",
           g_bodies[(size_t)g_focus].name.c_str(),
           (g_selection >= 0) ? g_bodies[(size_t)g_selection].name.c_str() : "none",
           g_cam_mode == CamMode::FOLLOW ? "orbit-follow" : "free",
           g_clock.warp, g_clock.paused ? "PAUSED" : "");
    for (size_t i = 0; i < g_bodies.size(); ++i) {
        const auto& b = g_bodies[i];
        const astra::app::Vec3d& w = g_world[i];
        const astra::app::Vec3d& v = g_vel[i];
        const double r = std::sqrt(w[0]*w[0] + w[1]*w[1] + w[2]*w[2]);
        const double speed = std::sqrt(v[0]*v[0] + v[1]*v[1] + v[2]*v[2]);
        printf("  %-8s r=%10.4f AU  v=%8.3f km/s  m=%.3e kg  R=%.0f km %s\n",
               b.name.c_str(), r / astra::app::AU_KM, speed, b.mass_kg, b.radius_km,
               i == (size_t)g_focus ? "<- FOCUS" : "");
        if (b.parent >= 0 && b.elements.a_km > 0.0) {
            const double a_au = b.elements.a_km / astra::app::AU_KM;
            const double peri = b.elements.a_km * (1.0 - b.elements.e);
            const double apo = b.elements.a_km * (1.0 + b.elements.e);
            const double period_yr = 6.283185307179586476925 / astra::app::mean_motion(b.elements)
                                     / astra::app::DAY_S / astra::app::YEAR_D;
            printf("           a=%.5f AU  e=%.6f  peri=%.5f apo=%.5f AU  T=%.4f yr  parent=%s\n",
                   a_au, b.elements.e, peri / astra::app::AU_KM, apo / astra::app::AU_KM,
                   period_yr, g_bodies[(size_t)b.parent].name.c_str());
        }
        if (b.kind == astra::app::BodyKind::STAR) {
            printf("           T_eff=%.0f K (DATA-DERIVED, NASA)  L=1.0 L_sun (REAL, definition)  parent=(none)\n",
                   b.temperature_k);
        } else {
            printf("           T_eff=NOT AVAILABLE  L=NOT AVAILABLE  age=NOT AVAILABLE\n");
        }
        // Light-travel delay from this body to the camera target (Phase K seed).
        const astra::app::Vec3d& tw = g_world[(size_t)g_focus];
        const double d_km = std::sqrt((w[0]-tw[0])*(w[0]-tw[0]) + (w[1]-tw[1])*(w[1]-tw[1]) + (w[2]-tw[2])*(w[2]-tw[2]));
        printf("           light-travel delay to observer(%s)=%.3f s (SIMULATED, Newtonian c approx)\n",
               g_bodies[(size_t)g_focus].name.c_str(), d_km / 299792.458);
        printf("           frame=heliocentric(%s)  provenance=%s\n",
               b.parent >= 0 ? g_bodies[(size_t)b.parent].name.c_str() : "Sun",
               b.classification.c_str());
    }
    printf("[ASTRA] classifications: REAL=established physics/constants · DATA-DERIVED=JPL/NASA approx inputs · SIMULATED=two-body Kepler output · CINEMATIC=visual scaling only\n");
}

// ─── Swapchain recreation ─────────────────────────────────────────────────────
static void destroy_swapchain_depth() {
    if (g_depth_view) { vkDestroyImageView(g_device, g_depth_view, nullptr); g_depth_view = VK_NULL_HANDLE; }
    if (g_depth_img) { vkDestroyImage(g_device, g_depth_img, nullptr); g_depth_img = VK_NULL_HANDLE; }
    if (g_depth_mem) { vkFreeMemory(g_device, g_depth_mem, nullptr); g_depth_mem = VK_NULL_HANDLE; }
}

static bool recreate_swapchain() {
    g_swapchain_dirty = false;
    if (g_width == 0 || g_height == 0 || g_minimized) return true;

    vkDeviceWaitIdle(g_device);
    for (auto fb : g_framebuffers) vkDestroyFramebuffer(g_device, fb, nullptr);
    g_framebuffers.clear();
    for (auto v : g_sc_views) vkDestroyImageView(g_device, v, nullptr);
    g_sc_views.clear();
    destroy_swapchain_depth();
    if (g_swapchain != VK_NULL_HANDLE) { vkDestroySwapchainKHR(g_device, g_swapchain, nullptr); g_swapchain = VK_NULL_HANDLE; }

    if (!create_swapchain()) return false;
    if (!create_depth()) return false;
    if (!create_framebuffers()) return false;
    printf("[ASTRA] Swapchain recreated for %ux%u\n", g_sc_extent.width, g_sc_extent.height);
    return true;
}

// ─── Frame rendering ──────────────────────────────────────────────────────────
static bool render_frame(double fps) {
    if (g_width == 0 || g_height == 0 || g_minimized) { Sleep(16); return true; }
    if (g_swapchain_dirty && !recreate_swapchain()) return false;

    vkWaitForFences(g_device, 1, &g_fence, VK_TRUE, UINT64_MAX);

    uint32_t img_idx = 0;
    VkResult acquire = vkAcquireNextImageKHR(g_device, g_swapchain, UINT64_MAX, g_img_sem, VK_NULL_HANDLE, &img_idx);
    if (acquire == VK_ERROR_OUT_OF_DATE_KHR) { g_swapchain_dirty = true; return true; }
    if (acquire != VK_SUCCESS && acquire != VK_SUBOPTIMAL_KHR) {
        printf("[ASTRA] vkAcquireNextImageKHR failed: %d\n", (int)acquire);
        return false;
    }
    if (acquire == VK_SUBOPTIMAL_KHR) g_swapchain_dirty = true;

    vkResetFences(g_device, 1, &g_fence);
    vkResetCommandBuffer(g_cmd_buf, 0);

    VkCommandBufferBeginInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(g_cmd_buf, &bi);

    VkClearValue clears[2]{};
    clears[0].color = {{0.004f, 0.004f, 0.010f, 1.0f}};
    clears[1].depthStencil = {1.0f, 0};

    VkRenderPassBeginInfo rp{};
    rp.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rp.renderPass = g_render_pass;
    rp.framebuffer = g_framebuffers[img_idx];
    rp.renderArea.offset = {0, 0};
    rp.renderArea.extent = g_sc_extent;
    rp.clearValueCount = 2;
    rp.pClearValues = clears;
    vkCmdBeginRenderPass(g_cmd_buf, &rp, VK_SUBPASS_CONTENTS_INLINE);

    VkViewport viewport{0.0f, 0.0f, (float)g_sc_extent.width, (float)g_sc_extent.height, 0.0f, 1.0f};
    vkCmdSetViewport(g_cmd_buf, 0, 1, &viewport);
    VkRect2D scissor{{0, 0}, g_sc_extent};
    vkCmdSetScissor(g_cmd_buf, 0, 1, &scissor);

    // ── View/projection (camera never sees absolute float coordinates; all
    // render positions are target-relative, floating origin). ──
    const astra::app::Vec3d& target = g_world[(size_t)g_focus];
    const float aspect = (float)g_sc_extent.width / (float)g_sc_extent.height;
    const astra::app::Mat4 proj = g_camera.projection(aspect);
    astra::app::Mat4 view;
    if (g_cam_mode == CamMode::FOLLOW) {
        view = g_camera.view();
    } else {
        // FREE: eye at g_free_pos (target-relative), look direction from orbit angles.
        const double az = g_camera.azimuth, el = g_camera.elevation;
        const float fwd[3] = {(float)(-std::cos(el) * std::cos(az)), (float)(-std::sin(el)), (float)(-std::cos(el) * std::sin(az))};
        view = astra::app::Mat4::look_at(g_free_pos[0], g_free_pos[1], g_free_pos[2],
                                         g_free_pos[0] + fwd[0], g_free_pos[1] + fwd[1], g_free_pos[2] + fwd[2],
                                         0.0f, 1.0f, 0.0f);
    }
    const astra::app::Mat4 view_proj = astra::app::Mat4::multiply(proj, view);

    // ── 1. Background pass ──
    vkCmdBindPipeline(g_cmd_buf, VK_PIPELINE_BIND_POINT_GRAPHICS, g_pipe_bg);
    const float bg_pc[8] = {(float)g_clock.sim_time_s, (float)g_sc_extent.width,
                            (float)g_sc_extent.height, (float)g_frame_count, 0, 0, 0, 0};
    vkCmdPushConstants(g_cmd_buf, g_pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                       0, 16, bg_pc);
    vkCmdDraw(g_cmd_buf, 3, 1, 0, 0);

    // Render-space positions (floating origin at the camera target).
    std::vector<std::array<float,3>> rpos(g_bodies.size());
    for (size_t i = 0; i < g_bodies.size(); ++i) {
        rpos[i] = {(float)((g_world[i][0] - target[0]) * POS_SCALE),
                   (float)((g_world[i][1] - target[1]) * POS_SCALE),
                   (float)((g_world[i][2] - target[2]) * POS_SCALE)};
    }

    // ── 2. Celestial bodies (indexed draws, per-body push constants) ──
    vkCmdBindPipeline(g_cmd_buf, VK_PIPELINE_BIND_POINT_GRAPHICS, g_pipe_mesh);
    VkDeviceSize zero = 0;
    vkCmdBindVertexBuffers(g_cmd_buf, 0, 1, &g_vb, &zero);
    vkCmdBindIndexBuffer(g_cmd_buf, g_ib, 0, VK_INDEX_TYPE_UINT16);
    struct BodyPC { astra::app::Mat4 mvp; float bodyPosRad[4]; float sunPosEmis[4]; float color[4]; };
    const float sunx = rpos[0][0], suny = rpos[0][1], sunz = rpos[0][2];
    for (size_t i = 0; i < g_bodies.size(); ++i) {
        const float r = visual_radius_units(g_bodies[i]);
        BodyPC pc{};
        pc.mvp = astra::app::Mat4::multiply(view_proj, model_of(rpos[i][0], rpos[i][1], rpos[i][2], r));
        pc.bodyPosRad[0] = rpos[i][0]; pc.bodyPosRad[1] = rpos[i][1]; pc.bodyPosRad[2] = rpos[i][2]; pc.bodyPosRad[3] = r;
        pc.sunPosEmis[0] = sunx; pc.sunPosEmis[1] = suny; pc.sunPosEmis[2] = sunz;
        pc.sunPosEmis[3] = (g_bodies[i].kind == astra::app::BodyKind::STAR) ? 1.0f : 0.0f;
        // Selection highlight: selected body brightness +30% (UI only; single
        // authoritative identity g_selection; no sim effect).
        const float boost = ((int)i == g_selection) ? 1.3f : 1.0f;
        pc.color[0] = std::min(1.0f, g_bodies[i].color[0] * boost);
        pc.color[1] = std::min(1.0f, g_bodies[i].color[1] * boost);
        pc.color[2] = std::min(1.0f, g_bodies[i].color[2] * boost);
        pc.color[3] = 1.0f;
        vkCmdPushConstants(g_cmd_buf, g_pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                           0, sizeof(pc), &pc);
        vkCmdDrawIndexed(g_cmd_buf, g_index_count, 1, 0, 0, 0);
    }

    // ── 3. Orbit overlays (Kepler evaluated in-shader from real elements) ──
    if (g_bodies.size() > 1) {
        vkCmdBindPipeline(g_cmd_buf, VK_PIPELINE_BIND_POINT_GRAPHICS, g_pipe_orbit);
        struct OrbitPC { astra::app::Mat4 viewProj; float e1[4]; float e2[4]; float parent[4]; float color[4]; };
        for (size_t i = 1; i < g_bodies.size(); ++i) {
            const auto& b = g_bodies[i];
            const auto& el = b.elements;
            OrbitPC pc{};
            pc.viewProj = view_proj;
            pc.e1[0] = (float)el.a_km; pc.e1[1] = (float)el.e; pc.e1[2] = (float)el.i; pc.e1[3] = (float)el.raan;
            const double n = astra::app::mean_motion(el);
            const double two_pi = 6.283185307179586476925;
            double M = std::fmod(el.M0 + n * (g_clock.sim_time_s - el.epoch_s), two_pi);
            if (M < 0.0) M += two_pi;
            pc.e2[0] = (float)el.argp; pc.e2[1] = (float)M; pc.e2[2] = (float)POS_SCALE; pc.e2[3] = 0.0f;
            const auto& pw = rpos[(size_t)b.parent >= 0 ? (size_t)b.parent : 0];
            pc.parent[0] = pw[0]; pc.parent[1] = pw[1]; pc.parent[2] = pw[2]; pc.parent[3] = 0.0f;
            const bool selected = ((int)i == g_selection);
            pc.color[0] = g_bodies[i].color[0] * (selected ? 1.0f : 0.45f);
            pc.color[1] = g_bodies[i].color[1] * (selected ? 1.0f : 0.45f);
            pc.color[2] = g_bodies[i].color[2] * (selected ? 1.0f : 0.45f);
            pc.color[3] = 1.0f;
            vkCmdPushConstants(g_cmd_buf, g_pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                               0, sizeof(pc), &pc);
            vkCmdDraw(g_cmd_buf, 128, 1, 0, 0);
        }
    }

    // ── 4. Velocity vectors (real vis velocity, CINEMATIC length scale) ──
    if (g_show_vectors) {
        vkCmdBindPipeline(g_cmd_buf, VK_PIPELINE_BIND_POINT_GRAPHICS, g_pipe_vector);
        struct VectorPC { astra::app::Mat4 viewProj; float originScale[4]; float velocity[4]; float color[4]; };
        for (size_t i = 1; i < g_bodies.size(); ++i) {
            const double vmag = std::sqrt(g_vel[i][0]*g_vel[i][0] + g_vel[i][1]*g_vel[i][1] + g_vel[i][2]*g_vel[i][2]);
            if (vmag < 1e-9) continue;
            VectorPC pc{};
            pc.viewProj = view_proj;
            pc.originScale[0] = rpos[i][0]; pc.originScale[1] = rpos[i][1]; pc.originScale[2] = rpos[i][2];
            pc.originScale[3] = (float)(VisualVectorScale(vmag));
            pc.velocity[0] = (float)g_vel[i][0]; pc.velocity[1] = (float)g_vel[i][1]; pc.velocity[2] = (float)g_vel[i][2]; pc.velocity[3] = 0.0f;
            const bool selected_b = ((int)i == g_selection);
            pc.color[0] = 0.4f; pc.color[1] = selected_b ? 1.0f : 0.8f; pc.color[2] = 0.3f; pc.color[3] = 1.0f;
            vkCmdPushConstants(g_cmd_buf, g_pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                               0, sizeof(pc), &pc);
            vkCmdDraw(g_cmd_buf, 2, 1, 0, 0);
        }
    }

    // ── 5. Peri/apo tick marks (real apsis positions from elements; P key) ──
    if (g_show_apsis) {
        vkCmdBindPipeline(g_cmd_buf, VK_PIPELINE_BIND_POINT_GRAPHICS, g_pipe_vector);
        struct VectorPC { astra::app::Mat4 viewProj; float originScale[4]; float velocity[4]; float color[4]; };
        for (size_t i = 1; i < g_bodies.size(); ++i) {
            const auto& el = g_bodies[i].elements;
            if (!(el.a_km > 0.0)) continue; // apsis unavailable for the primary
            const double peri = el.a_km * (1.0 - el.e);
            const double apo = el.a_km * (1.0 + el.e);
            // Apsis directions in the orbit plane (nu=0 -> peri, nu=pi -> apo)
            // through the same PQW->IJK rotation as the scientific engine.
            const astra::app::Vec3d dir_p = astra::app::rotation_pqw_to_ijk(el.i, el.raan, el.argp, {peri, 0.0, 0.0});
            const astra::app::Vec3d dir_a = astra::app::rotation_pqw_to_ijk(el.i, el.raan, el.argp, {-apo, 0.0, 0.0});
            const astra::app::Vec3d& pw = g_world[(size_t)g_bodies[i].parent >= 0 ? (size_t)g_bodies[i].parent : 0];
            const float mp[2][3] = {
                {(float)((pw[0] + dir_p[0] - target[0]) * POS_SCALE), (float)((pw[1] + dir_p[1] - target[1]) * POS_SCALE), (float)((pw[2] + dir_p[2] - target[2]) * POS_SCALE)},
                {(float)((pw[0] + dir_a[0] - target[0]) * POS_SCALE), (float)((pw[1] + dir_a[1] - target[1]) * POS_SCALE), (float)((pw[2] + dir_a[2] - target[2]) * POS_SCALE)}};
            for (int m = 0; m < 2; ++m) {
                VectorPC pc{};
                pc.viewProj = view_proj;
                pc.originScale[0] = mp[m][0]; pc.originScale[1] = mp[m][1]; pc.originScale[2] = mp[m][2];
                pc.originScale[3] = (m == 0 ? 0.55f : 0.40f); // tick lengths
                // Global +Y tick direction (orientation is presentational; the
                // apsis POSITION is scientific).
                pc.velocity[0] = 0.0f; pc.velocity[1] = 1.0f; pc.velocity[2] = 0.0f; pc.velocity[3] = 0.0f;
                pc.color[0] = (m == 0) ? 1.0f : 0.55f; pc.color[1] = 0.85f; pc.color[2] = (m == 0) ? 0.35f : 1.0f; pc.color[3] = 1.0f;
                vkCmdPushConstants(g_cmd_buf, g_pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                                   0, sizeof(pc), &pc);
                vkCmdDraw(g_cmd_buf, 2, 1, 0, 0);
            }
        }
    }

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
    (void)fps;
    return true;
}

// ─── Cleanup ──────────────────────────────────────────────────────────────────
static void cleanup() {
    if (g_device) vkDeviceWaitIdle(g_device);
    if (g_pipe_bg) vkDestroyPipeline(g_device, g_pipe_bg, nullptr);
    if (g_pipe_mesh) vkDestroyPipeline(g_device, g_pipe_mesh, nullptr);
    if (g_pipe_orbit) vkDestroyPipeline(g_device, g_pipe_orbit, nullptr);
    if (g_pipe_vector) vkDestroyPipeline(g_device, g_pipe_vector, nullptr);
    if (g_pipeline_layout) vkDestroyPipelineLayout(g_device, g_pipeline_layout, nullptr);
    if (g_vert_module) vkDestroyShaderModule(g_device, g_vert_module, nullptr);
    if (g_frag_module) vkDestroyShaderModule(g_device, g_frag_module, nullptr);
    if (g_vb) vkDestroyBuffer(g_device, g_vb, nullptr);
    if (g_vb_mem) vkFreeMemory(g_device, g_vb_mem, nullptr);
    if (g_ib) vkDestroyBuffer(g_device, g_ib, nullptr);
    if (g_ib_mem) vkFreeMemory(g_device, g_ib_mem, nullptr);
    destroy_swapchain_depth();
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

// ─── Entry point ──────────────────────────────────────────────────────────────
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    AllocConsole();
    freopen("CONOUT$", "w", stdout);
    freopen("CONOUT$", "w", stderr);
    printf("[ASTRA] ASTRA COSMOS v0.4 — HUD model, audio bus, persistence, LUT, apsis markers, selection/deselect\n");

    // Scientific scenario (Python engine remains the authority; this native
    // mirror reproduces astra.orbital exactly — see app/celestial_sim.h).
    g_bodies = astra::app::make_solar_system();
    g_world = astra::app::propagate_world(g_bodies, g_clock.sim_time_s);
    g_vel = astra::app::propagate_world_velocity(g_bodies, g_clock.sim_time_s);
    // Phase R: consume the project star-temperature LUT for the star's visual
    // color (blackbody-approx 2000..40kK, provenance: generate_lut.py, CC0).
    {
        astra::app::RgbF star_c{};
        const std::string luts[] = {exe_dir() + "\\assets\\star_temperature_lut.ppm",
                                    "assets\\star_temperature_lut.ppm",
                                    "..\\assets\\star_temperature_lut.ppm",
                                    "native_renderer\\assets\\star_temperature_lut.ppm"};
        bool applied = false;
        for (const auto& p : luts) {
            if (astra::app::star_color_from_file(p, g_bodies[0].temperature_k, star_c)) {
                g_bodies[0].color[0] = star_c.r; g_bodies[0].color[1] = star_c.g; g_bodies[0].color[2] = star_c.b;
                printf("[ASTRA] Star color from LUT %s (T_eff=%.0fK -> %.3f %.3f %.3f, blackbody approx)\n",
                       p.c_str(), g_bodies[0].temperature_k, star_c.r, star_c.g, star_c.b);
                applied = true;
                break;
            }
        }
        if (!applied)
            printf("[ASTRA] Star LUT NOT AVAILABLE (assets/star_temperature_lut.ppm) — keeping table color (CINEMATIC)\n");
    }
    focus_body(3, true); // Earth
    printf("[ASTRA] Scenario: solar system (%zu bodies, epoch J2000, Kepler two-body)\n", g_bodies.size());

    if (!create_window())     { printf("[ASTRA] Window failed\n"); return 1; }
    if (!create_instance())   { printf("[ASTRA] Instance failed\n"); cleanup(); return 1; }
    if (!create_surface())    { printf("[ASTRA] Surface failed\n"); cleanup(); return 1; }
    if (!create_device())     { printf("[ASTRA] Device failed\n"); cleanup(); return 1; }
    if (!create_swapchain())  { printf("[ASTRA] Swapchain failed\n"); cleanup(); return 1; }
    if (!create_depth())      { printf("[ASTRA] Depth failed\n"); cleanup(); return 1; }
    if (!create_render_pass()){ printf("[ASTRA] Render pass failed\n"); cleanup(); return 1; }
    if (!create_framebuffers()){ printf("[ASTRA] Framebuffers failed\n"); cleanup(); return 1; }
    if (!create_commands())   { printf("[ASTRA] Commands failed\n"); cleanup(); return 1; }
    if (!create_sync())       { printf("[ASTRA] Sync failed\n"); cleanup(); return 1; }
    if (!create_geometry())   { printf("[ASTRA] Geometry failed\n"); cleanup(); return 1; }
    if (!create_pipelines())  { printf("[ASTRA] Pipelines failed\n"); cleanup(); return 1; }

    printf("[ASTRA] v0.4 controls: arrows look | PgUp/PgDn zoom/speed | Tab select+focus | X deselect | O free/orbit cam | WASDQE move | +/- warp | 0-8 presets | Space pause | . step | BKSP epoch | F5 restart | F2 save F3 load | V vectors | P apsis | F1 HUD+inspector | ESC quit\n");
    dump_inspector();

    auto t_prev = std::chrono::high_resolution_clock::now();
    double fps = 0.0;

    // ═══ PERSISTENT MAIN LOOP ═══
    while (!g_quit) {
        MSG msg{};
        while (PeekMessageA(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
            if (msg.message == WM_QUIT) g_quit = true;
        }
        if (g_quit) break;

        auto t_now = std::chrono::high_resolution_clock::now();
        double real_dt = std::chrono::duration<double>(t_now - t_prev).count();
        if (real_dt > 0.25) real_dt = 0.25; // avoid spiral after breakpoints
        t_prev = t_now;
        if (real_dt > 1e-6) fps = 0.9 * fps + 0.1 * (1.0 / real_dt);
        g_last_fps = fps;

        update_camera_from_input(real_dt);
        sim_tick(real_dt);
        update_inspector_title(fps);

        // Cosmic Audio routing (v0.4): every simulation/UI event surfaced with
        // its mandatory provenance classification. Audible output on Windows:
        // NOT VERIFIED in this environment (classification/routing gates pass).
        for (int k = 0; k < 4; ++k) {
            astra::app::AudioEvent ev{};
            if (!g_audio.pop(ev)) break;
            printf("[ASTRA-AUDIO] %-18s %-24s [%s] t=%+9.2fd subject=%s\n",
                   astra::app::audio_event_kind_name(ev.kind),
                   "classified", astra::app::audio_class_name(ev.classification),
                   ev.sim_time_s / 86400.0, ev.subject.c_str());
        }

        if (!render_frame(fps)) {
            printf("[ASTRA] Render failed\n");
            break;
        }

        g_frame_count++;
        if (g_frame_count % 300 == 0) {
            printf("[ASTRA] frame=%llu sim=%+.3f yr warp=x%.0f focus=%s fps=%.0f\n",
                (unsigned long long)g_frame_count,
                g_clock.sim_time_s / astra::app::DAY_S / astra::app::YEAR_D,
                g_clock.warp, g_bodies[(size_t)g_focus].name.c_str(), fps);
        }
    }

    g_clock.paused = true;
    printf("[ASTRA] Shutting down after %llu frames\n", (unsigned long long)g_frame_count);
    cleanup();
    return 0;
}
