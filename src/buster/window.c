#pragma once
#include <buster/window.h>
#if defined (__linux__)
#include <X11/Xlib.h>
#include <xcb/xcb.h>
#endif
#include <buster/string8.h>
#include <buster/os.h>

STRUCT(WindowManagerInstance)
{
};


#define BUSTER_VULKAN_FUNCTION_POINTER(n) PFN_ ## n n
#define BUSTER_GLOBAL_VULKAN_FUNCTION_POINTER(n) BUSTER_GLOBAL_LOCAL __attribute__((used)) BUSTER_VULKAN_FUNCTION_POINTER(n)
#define BUSTER_VULKAN_LOAD_FUNCTION(v, n) n = (typeof(n)) os_dynamic_library_function_load(vulkan_library, S8(#n));
#ifndef VK_NO_PROTOTYPES
#define VK_NO_PROTOTYPES
#endif

#ifndef VULKAN_H_
#if defined(VK_USE_PLATFORM_WIN32_KHR)
#include <vulkan/vk_platform.h>
#include <vulkan/vulkan_core.h>

/* When VK_USE_PLATFORM_WIN32_KHR is defined, instead of including vulkan.h directly, we include individual parts of the SDK
* This is necessary to avoid including <windows.h> which is very heavy - it takes 200ms to parse without WIN32_LEAN_AND_MEAN
* and 100ms to parse with it. vulkan_win32.h only needs a few symbols that are easy to redefine ourselves.
*/
typedef unsigned long DWORD;
typedef const wchar_t* LPCWSTR;
typedef void* HANDLE;
typedef struct HINSTANCE__* HINSTANCE;
typedef struct HWND__* HWND;
typedef struct HMONITOR__* HMONITOR;
typedef struct _SECURITY_ATTRIBUTES SECURITY_ATTRIBUTES;

#include <vulkan/vulkan_win32.h>

#ifdef VK_ENABLE_BETA_EXTENSIONS
#include <vulkan/vulkan_beta.h>
#endif
#else
#include <vulkan/vulkan.h>
#endif
#endif

// INSTANCE FUNCTIONS START
// These functions require no instance
BUSTER_GLOBAL_VULKAN_FUNCTION_POINTER(vkGetInstanceProcAddr);
BUSTER_GLOBAL_VULKAN_FUNCTION_POINTER(vkEnumerateInstanceVersion);
BUSTER_GLOBAL_VULKAN_FUNCTION_POINTER(vkEnumerateInstanceLayerProperties);
BUSTER_GLOBAL_VULKAN_FUNCTION_POINTER(vkCreateInstance);

// These functions require an instance as a parameter
BUSTER_GLOBAL_VULKAN_FUNCTION_POINTER(vkGetDeviceProcAddr);
BUSTER_GLOBAL_VULKAN_FUNCTION_POINTER(vkCreateDebugUtilsMessengerEXT);
BUSTER_GLOBAL_VULKAN_FUNCTION_POINTER(vkEnumeratePhysicalDevices);
BUSTER_GLOBAL_VULKAN_FUNCTION_POINTER(vkGetPhysicalDeviceMemoryProperties);
BUSTER_GLOBAL_VULKAN_FUNCTION_POINTER(vkGetPhysicalDeviceProperties);
BUSTER_GLOBAL_VULKAN_FUNCTION_POINTER(vkGetPhysicalDeviceQueueFamilyProperties);
BUSTER_GLOBAL_VULKAN_FUNCTION_POINTER(vkGetPhysicalDeviceSurfaceCapabilitiesKHR);
BUSTER_GLOBAL_VULKAN_FUNCTION_POINTER(vkGetPhysicalDeviceSurfacePresentModesKHR);
BUSTER_GLOBAL_VULKAN_FUNCTION_POINTER(vkCreateDevice);

#if defined(VK_KHR_xcb_surface)
BUSTER_GLOBAL_VULKAN_FUNCTION_POINTER(vkCreateXcbSurfaceKHR);
#endif
#if defined(VK_KHR_win32_surface)
BUSTER_GLOBAL_VULKAN_FUNCTION_POINTER(vkCreateWin32SurfaceKHR);
#endif
#if defined(VK_EXT_metal_surface)
BUSTER_GLOBAL_VULKAN_FUNCTION_POINTER(vkCreateMetalSurfaceEXT);
#endif
// INSTANCE FUNCTIONS END

BUSTER_GLOBAL_VULKAN_FUNCTION_POINTER(vkCreateSwapchainKHR);
BUSTER_GLOBAL_VULKAN_FUNCTION_POINTER(vkCmdCopyBuffer2);
BUSTER_GLOBAL_VULKAN_FUNCTION_POINTER(vkAllocateMemory);
BUSTER_GLOBAL_VULKAN_FUNCTION_POINTER(vkCreateBuffer);
BUSTER_GLOBAL_VULKAN_FUNCTION_POINTER(vkGetBufferMemoryRequirements);
BUSTER_GLOBAL_VULKAN_FUNCTION_POINTER(vkBindBufferMemory);
BUSTER_GLOBAL_VULKAN_FUNCTION_POINTER(vkMapMemory);
BUSTER_GLOBAL_VULKAN_FUNCTION_POINTER(vkGetBufferDeviceAddress);
BUSTER_GLOBAL_VULKAN_FUNCTION_POINTER(vkResetFences);
BUSTER_GLOBAL_VULKAN_FUNCTION_POINTER(vkResetCommandBuffer);
BUSTER_GLOBAL_VULKAN_FUNCTION_POINTER(vkBeginCommandBuffer);
BUSTER_GLOBAL_VULKAN_FUNCTION_POINTER(vkEndCommandBuffer);
BUSTER_GLOBAL_VULKAN_FUNCTION_POINTER(vkQueueSubmit2);
BUSTER_GLOBAL_VULKAN_FUNCTION_POINTER(vkWaitForFences);
BUSTER_GLOBAL_VULKAN_FUNCTION_POINTER(vkCreateImage);
BUSTER_GLOBAL_VULKAN_FUNCTION_POINTER(vkGetImageMemoryRequirements);
BUSTER_GLOBAL_VULKAN_FUNCTION_POINTER(vkBindImageMemory);
BUSTER_GLOBAL_VULKAN_FUNCTION_POINTER(vkCreateImageView);
BUSTER_GLOBAL_VULKAN_FUNCTION_POINTER(vkCmdPipelineBarrier2);
BUSTER_GLOBAL_VULKAN_FUNCTION_POINTER(vkCmdBlitImage2);
BUSTER_GLOBAL_VULKAN_FUNCTION_POINTER(vkGetDeviceQueue);
BUSTER_GLOBAL_VULKAN_FUNCTION_POINTER(vkCreateCommandPool);
BUSTER_GLOBAL_VULKAN_FUNCTION_POINTER(vkAllocateCommandBuffers);
BUSTER_GLOBAL_VULKAN_FUNCTION_POINTER(vkCreateFence);
BUSTER_GLOBAL_VULKAN_FUNCTION_POINTER(vkCreateSampler);
BUSTER_GLOBAL_VULKAN_FUNCTION_POINTER(vkCreateShaderModule);
BUSTER_GLOBAL_VULKAN_FUNCTION_POINTER(vkCreateDescriptorSetLayout);
BUSTER_GLOBAL_VULKAN_FUNCTION_POINTER(vkCreatePipelineLayout);
BUSTER_GLOBAL_VULKAN_FUNCTION_POINTER(vkCreateGraphicsPipelines);
BUSTER_GLOBAL_VULKAN_FUNCTION_POINTER(vkDestroyImageView);
BUSTER_GLOBAL_VULKAN_FUNCTION_POINTER(vkDestroyImage);
BUSTER_GLOBAL_VULKAN_FUNCTION_POINTER(vkFreeMemory);
BUSTER_GLOBAL_VULKAN_FUNCTION_POINTER(vkDeviceWaitIdle);
BUSTER_GLOBAL_VULKAN_FUNCTION_POINTER(vkDestroySwapchainKHR);
BUSTER_GLOBAL_VULKAN_FUNCTION_POINTER(vkGetSwapchainImagesKHR);
BUSTER_GLOBAL_VULKAN_FUNCTION_POINTER(vkCreateDescriptorPool);
BUSTER_GLOBAL_VULKAN_FUNCTION_POINTER(vkAllocateDescriptorSets);
BUSTER_GLOBAL_VULKAN_FUNCTION_POINTER(vkCreateSemaphore);
BUSTER_GLOBAL_VULKAN_FUNCTION_POINTER(vkAcquireNextImageKHR);
BUSTER_GLOBAL_VULKAN_FUNCTION_POINTER(vkDestroyBuffer);
BUSTER_GLOBAL_VULKAN_FUNCTION_POINTER(vkUnmapMemory);
BUSTER_GLOBAL_VULKAN_FUNCTION_POINTER(vkCmdSetViewport);
BUSTER_GLOBAL_VULKAN_FUNCTION_POINTER(vkCmdSetScissor);
BUSTER_GLOBAL_VULKAN_FUNCTION_POINTER(vkCmdBeginRendering);
BUSTER_GLOBAL_VULKAN_FUNCTION_POINTER(vkCmdBindPipeline);
BUSTER_GLOBAL_VULKAN_FUNCTION_POINTER(vkCmdBindDescriptorSets);
BUSTER_GLOBAL_VULKAN_FUNCTION_POINTER(vkCmdBindIndexBuffer);
BUSTER_GLOBAL_VULKAN_FUNCTION_POINTER(vkCmdPushConstants);
BUSTER_GLOBAL_VULKAN_FUNCTION_POINTER(vkCmdDrawIndexed);
BUSTER_GLOBAL_VULKAN_FUNCTION_POINTER(vkCmdEndRendering);
BUSTER_GLOBAL_VULKAN_FUNCTION_POINTER(vkQueuePresentKHR);
BUSTER_GLOBAL_VULKAN_FUNCTION_POINTER(vkCmdCopyBufferToImage);
BUSTER_GLOBAL_VULKAN_FUNCTION_POINTER(vkUpdateDescriptorSets);

BUSTER_DECL void toy()
{
#if 0
    let display = XOpenDisplay(0);
    if (display)
    {
        let default_screen = DefaultScreen(display);
        let screen_width = (u32)DisplayWidth(display, default_screen);
        let screen_height = (u32)DisplayHeight(display, default_screen);
        let width = screen_width / 2;
        let height = screen_height / 2;
        let root_window = RootWindow(display, default_screen);
        u32 border_width = 10;
        let window = XCreateSimpleWindow(display, root_window, 0, 0, width, height, border_width, BlackPixel(display, default_screen), WhitePixel(display, default_screen));
        XSelectInput(display, window, 
                KeyPressMask |
                KeyReleaseMask |
                ButtonPressMask |
                ButtonReleaseMask |
                EnterWindowMask |
                LeaveWindowMask |
                PointerMotionMask |
                PointerMotionHintMask |
                Button1MotionMask |
                Button2MotionMask |
                Button3MotionMask |
                Button4MotionMask |
                Button5MotionMask |
                ButtonMotionMask |
                KeymapStateMask |
                ExposureMask |
                VisibilityChangeMask |
                StructureNotifyMask |
                ResizeRedirectMask |
                SubstructureNotifyMask |
                SubstructureRedirectMask |
                FocusChangeMask |
                PropertyChangeMask |
                ColormapChangeMask |
                OwnerGrabButtonMask
                );
        XMapWindow(display, window);

        XFlush(display);

        string8_print(S8("Starting event loop...\n"));

        while (1)
        {
            XEvent event;
            XNextEvent(display, &event);
            string8_print(S8("Event {u32} received!\n"), event.type);
        }
   }
#else
    let connection = xcb_connect(0, 0);
    if (connection)
    {
        let setup = xcb_get_setup(connection);
        xcb_screen_t* screen = 0;

        for (let screen_iterator = xcb_setup_roots_iterator(setup); screen_iterator.rem; xcb_screen_next(&screen_iterator))
        {
            screen = screen_iterator.data;
            if (screen)
            {
                break;
            }
        }

        let screen_width = screen->width_in_pixels;
        let screen_height = screen->height_in_pixels;

        u16 width = screen_width / 2;
        u16 height = screen_height / 2;

        let window_id = xcb_generate_id(connection);
        let parent_window = screen->root;
        u16 border_width = 10;

        xcb_create_window(connection, XCB_COPY_FROM_PARENT, window_id, parent_window, 0, 0, width, height, border_width, XCB_WINDOW_CLASS_INPUT_OUTPUT, screen->root_visual, 0, 0);
        u32 event_mask = XCB_EVENT_MASK_KEY_PRESS |
            XCB_EVENT_MASK_KEY_RELEASE |
            XCB_EVENT_MASK_BUTTON_PRESS |
            XCB_EVENT_MASK_BUTTON_RELEASE |
            XCB_EVENT_MASK_ENTER_WINDOW |
            XCB_EVENT_MASK_LEAVE_WINDOW |
            XCB_EVENT_MASK_POINTER_MOTION |
            XCB_EVENT_MASK_POINTER_MOTION_HINT |
            XCB_EVENT_MASK_BUTTON_1_MOTION |
            XCB_EVENT_MASK_BUTTON_2_MOTION |
            XCB_EVENT_MASK_BUTTON_3_MOTION |
            XCB_EVENT_MASK_BUTTON_4_MOTION |
            XCB_EVENT_MASK_BUTTON_5_MOTION |
            XCB_EVENT_MASK_BUTTON_MOTION |
            XCB_EVENT_MASK_KEYMAP_STATE |
            XCB_EVENT_MASK_EXPOSURE |
            XCB_EVENT_MASK_VISIBILITY_CHANGE |
            XCB_EVENT_MASK_STRUCTURE_NOTIFY |
            XCB_EVENT_MASK_RESIZE_REDIRECT |
            XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY |
            XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT |
            XCB_EVENT_MASK_FOCUS_CHANGE |
            XCB_EVENT_MASK_PROPERTY_CHANGE |
            XCB_EVENT_MASK_COLOR_MAP_CHANGE |
            XCB_EVENT_MASK_OWNER_GRAB_BUTTON;
        xcb_change_window_attributes(connection, window_id, XCB_CW_EVENT_MASK, &event_mask);
        xcb_map_window(connection, window_id);
        xcb_flush(connection);

        OsModule* vulkan_library = {};

#if defined(__linux__)
        vulkan_library = os_dynamic_library_load(SOs("libvulkan.so.1"));
#elif defined(_WIN32)
        vulkan_library = os_dynamic_library_load(SOs("vulkan-1.dll"));
#elif defined(__APPLE__)
        vulkan_library = os_dynamic_library_load("libvulkan.dylib");

        if (!os_library_is_valid(vulkan_library))
        {
            vulkan_library = os_dynamic_library_load("libvulkan.1.dylib");
        }

        if (!os_library_is_valid(vulkan_library))
        {
            vulkan_library = os_dynamic_library_load("libMoltenVK.dylib");
        }

        if (!os_library_is_valid(vulkan_library))
        {
            vulkan_library = os_dynamic_library_load("vulkan.framework/vulkan");
        }

        if (!os_library_is_valid(vulkan_library))
        {
            vulkan_library = os_dynamic_library_load("MoltenVK.framework/MoltenVK");
        }
#endif
        if (vulkan_library)
        {
            BUSTER_VULKAN_LOAD_FUNCTION(vulkan_library, vkGetInstanceProcAddr);
            while (1)
            {
                xcb_generic_event_t* event = 0;
                while ((event = xcb_poll_for_event(connection)))
                {
                    string8_print(S8("Received event {u8}\n"), event->response_type);
                }
            }
        }
    }
    else
    {
        string8_print(S8("could not connect to X\n"));
    }
#endif
}
