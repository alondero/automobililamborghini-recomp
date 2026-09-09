// App-local Vulkan loader. SDL and plume must use the same loader instance,
// including when an Adreno driver has been imported. No system files change.
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#include <adrenotools/driver.h>
#include <android/log.h>
#include <cstdlib>
#include <dlfcn.h>
#include <mutex>

extern "C" __attribute__((visibility("default")))
PFN_vkVoidFunction vkGetInstanceProcAddr(VkInstance instance, const char* name) {
    static std::once_flag once;
    static PFN_vkGetInstanceProcAddr get_proc = nullptr;
    std::call_once(once, [] {
        void* loader = nullptr;
        const char* directory = std::getenv("LAMBO_ANDROID_DRIVER_DIR");
        const char* library = std::getenv("LAMBO_ANDROID_DRIVER_NAME");
        if (directory && library) {
            loader = adrenotools_open_libvulkan(RTLD_NOW, ADRENOTOOLS_DRIVER_CUSTOM,
                std::getenv("LAMBO_ANDROID_CACHE_DIR"),
                std::getenv("LAMBO_ANDROID_NATIVE_LIB_DIR"), directory, library, nullptr, nullptr);
        } else {
            loader = dlopen("libvulkan.so", RTLD_NOW | RTLD_LOCAL);
        }
        if (loader) get_proc = reinterpret_cast<PFN_vkGetInstanceProcAddr>(dlsym(loader, "vkGetInstanceProcAddr"));
        if (!get_proc) __android_log_print(ANDROID_LOG_ERROR, "Lamborghini",
            "Cannot load %s Vulkan driver: %s", library ? library : "system", dlerror());
        // Retain the loader for the game process lifetime: its dispatch tables
        // and hook namespaces may still be referenced by driver worker threads.
    });
    return get_proc ? get_proc(instance, name) : nullptr;
}
