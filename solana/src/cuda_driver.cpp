#include "cuda_image.hpp"
#include "solvan/core.hpp"
#include <algorithm>
#include <cstring>
#include <stdexcept>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#define CUDA_CALL __stdcall
#else
#include <dlfcn.h>
#define CUDA_CALL
#endif
namespace solvan {
namespace {
using Ptr = unsigned long long;
using Context = void *;
using Module = void *;
using Function = void *;
// CUDA Driver API is loaded at runtime. No CUDA runtime DLL or SDK is required
// on the user's PC; all signatures follow NVIDIA's public Driver API ABI.
struct Driver {
#ifdef _WIN32
    HMODULE lib = nullptr;
#else
    void *lib = nullptr;
#endif
    std::string error;
    int(CUDA_CALL *init)(unsigned) = nullptr;
    int(CUDA_CALL *device_count)(int *) = nullptr;
    int(CUDA_CALL *device_get)(int *, int) = nullptr;
    int(CUDA_CALL *device_name)(char *, int, int) = nullptr;
    int(CUDA_CALL *pci_bus)(char *, int, int) = nullptr;
    int(CUDA_CALL *attribute)(int *, int, int) = nullptr;
    int(CUDA_CALL *retain)(Context *, int) = nullptr;
    int(CUDA_CALL *release)(int) = nullptr;
    int(CUDA_CALL *set_context)(Context) = nullptr;
    int(CUDA_CALL *module_load)(Module *, const void *) = nullptr;
    int(CUDA_CALL *module_unload)(Module) = nullptr;
    int(CUDA_CALL *get_function)(Function *, Module, const char *) = nullptr;
    int(CUDA_CALL *alloc)(Ptr *, size_t) = nullptr;
    int(CUDA_CALL *free_mem)(Ptr) = nullptr;
    int(CUDA_CALL *htod)(Ptr, const void *, size_t) = nullptr;
    int(CUDA_CALL *dtoh)(void *, Ptr, size_t) = nullptr;
    int(CUDA_CALL *launch)(Function, unsigned, unsigned, unsigned, unsigned, unsigned, unsigned,
                           unsigned, void *, void **, void **) = nullptr;
    int(CUDA_CALL *synchronize)() = nullptr;
    int(CUDA_CALL *error_string)(int, const char **) = nullptr;
    template <class F> void bind(F &f, const char *name, const char *fallback = nullptr) {
#ifdef _WIN32
        auto p = GetProcAddress(lib, name);
        if (!p && fallback)
            p = GetProcAddress(lib, fallback);
#else
        auto p = dlsym(lib, name);
        if (!p && fallback)
            p = dlsym(lib, fallback);
#endif
        if (!p)
            throw std::runtime_error(std::string("Missing NVIDIA driver function: ") + name);
        static_assert(sizeof(f) == sizeof(p), "Driver symbol pointer size mismatch");
        std::memcpy(&f, &p, sizeof(f));
    }
    Driver() {
        try {
#ifdef _WIN32
            lib = LoadLibraryExW(L"nvcuda.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
#else
            lib = dlopen("libcuda.so.1", RTLD_NOW | RTLD_LOCAL);
#endif
            if (!lib)
                throw std::runtime_error("NVIDIA driver not installed");
            bind(init, "cuInit");
            bind(device_count, "cuDeviceGetCount");
            bind(device_get, "cuDeviceGet");
            bind(device_name, "cuDeviceGetName");
            bind(pci_bus, "cuDeviceGetPCIBusId");
            bind(attribute, "cuDeviceGetAttribute");
            bind(retain, "cuDevicePrimaryCtxRetain");
            bind(release, "cuDevicePrimaryCtxRelease_v2", "cuDevicePrimaryCtxRelease");
            bind(set_context, "cuCtxSetCurrent");
            bind(module_load, "cuModuleLoadData");
            bind(module_unload, "cuModuleUnload");
            bind(get_function, "cuModuleGetFunction");
            bind(alloc, "cuMemAlloc_v2");
            bind(free_mem, "cuMemFree_v2");
            bind(htod, "cuMemcpyHtoD_v2");
            bind(dtoh, "cuMemcpyDtoH_v2");
            bind(launch, "cuLaunchKernel");
            bind(synchronize, "cuCtxSynchronize");
            bind(error_string, "cuGetErrorString");
        } catch (const std::exception &e) {
            error = e.what();
        }
    }
    // Keep the driver loaded until process exit, including static provider teardown.
    void check(int code) {
        if (code) {
            const char *text = nullptr;
            if (error_string)
                error_string(code, &text);
            throw std::runtime_error(std::string("CUDA: ") + (text ? text : std::to_string(code)));
        }
    }
    int select() {
        if (!error.empty())
            throw std::runtime_error(error);
        check(init(0));
        int n = 0;
        check(device_count(&n));
        for (int i = 0; i < n; i++) {
            int dev = 0, major = 0, minor = 0;
            check(device_get(&dev, i));
            check(attribute(&major, 75, dev));
            check(attribute(&minor, 76, dev));
            if (major * 10 + minor >= cuda_min_arch)
                return dev;
        }
        throw std::runtime_error("No NVIDIA GPU compatible with embedded CUDA kernel");
    }
};
Driver &driver() {
    static Driver d;
    return d;
}
struct Batch {
    Driver &d;
    int device = -1;
    Context context = nullptr;
    Module module = nullptr;
    Function function = nullptr, derive = nullptr;
    Ptr indices = 0, count = 0, pubs = 0;
    Batch() : d(driver()) {}
    ~Batch() {
        if (context) {
            d.set_context(context);
            if (indices)
                d.free_mem(indices);
            if (count)
                d.free_mem(count);
            if (pubs)
                d.free_mem(pubs);
            if (module)
                d.module_unload(module);
            d.release(device);
        }
    }
};
} // namespace
Device gpu_device() {
    try {
        auto &d = driver();
        int dev = d.select();
        char name[256] = {};
        d.check(d.device_name(name, sizeof(name), dev));
        return {true, name, ""};
    } catch (const std::exception &e) {
        return {false, std::string("CPU fallback: ") + e.what(), e.what()};
    }
}
Gpu::Gpu() {
    auto p = new Batch;
    try {
        p->device = p->d.select();
        p->d.check(p->d.retain(&p->context, p->device));
        p->d.check(p->d.set_context(p->context));
        p->d.check(p->d.module_load(&p->module, cuda_image));
        p->d.check(p->d.get_function(&p->function, p->module, "scan_solana"));
        p->d.check(p->d.get_function(&p->derive, p->module, "derive_solana"));
        p->d.check(p->d.alloc(&p->indices, 65536 * sizeof(uint32_t)));
        p->d.check(p->d.alloc(&p->count, sizeof(uint32_t)));
        p->d.check(p->d.alloc(&p->pubs, 65536 * 32));
        impl_ = p;
    } catch (...) {
        delete p;
        throw;
    }
}
Gpu::~Gpu() {
    delete static_cast<Batch *>(impl_);
}
static void bounds(const Prepared &p, uint64_t first, uint32_t n) {
    if (!n || n > 65536 || first > UINT64_MAX - n || !p.range_count || p.range_count > 4)
        throw std::invalid_argument("Invalid CUDA batch bounds");
}
std::vector<uint32_t> Gpu::run(const Prepared &prepared, uint64_t first, uint32_t n) {
    bounds(prepared, first, n);
    auto &p = *static_cast<Batch *>(impl_);
    p.d.check(p.d.set_context(p.context));
    KernelParams params{prepared, first, n};
    uint32_t count = 0;
    p.d.check(p.d.htod(p.count, &count, sizeof(count)));
    void *args[] = {&params, &p.indices, &p.count};
    p.d.check(p.d.launch(p.function, (n + 127) / 128, 1, 1, 128, 1, 1, 0, nullptr, args, nullptr));
    p.d.check(p.d.synchronize());
    p.d.check(p.d.dtoh(&count, p.count, sizeof(count)));
    if (count > n)
        throw std::runtime_error("CUDA result count overflow");
    std::vector<uint32_t> out(count);
    if (count)
        p.d.check(p.d.dtoh(out.data(), p.indices, count * sizeof(uint32_t)));
    return out;
}
std::vector<Bytes> Gpu::derive_for_test(const Prepared &prepared, uint64_t first, uint32_t n) {
    bounds(prepared, first, n);
    auto &p = *static_cast<Batch *>(impl_);
    p.d.check(p.d.set_context(p.context));
    KernelParams params{prepared, first, n};
    void *args[] = {&params, &p.pubs};
    p.d.check(p.d.launch(p.derive, (n + 127) / 128, 1, 1, 128, 1, 1, 0, nullptr, args, nullptr));
    p.d.check(p.d.synchronize());
    std::vector<Bytes> out(n);
    p.d.check(p.d.dtoh(out.data(), p.pubs, n * 32));
    return out;
}
} // namespace solvan
