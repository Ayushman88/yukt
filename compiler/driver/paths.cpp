#include "driver/paths.hpp"

#include <cstdlib>
#include <string>

#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

#ifndef YUKT_RUNTIME_LIB
#define YUKT_RUNTIME_LIB ""
#endif

#ifndef YUKT_TEMPLATES_DIR
#define YUKT_TEMPLATES_DIR ""
#endif

namespace yukt {
namespace {

namespace fs = std::filesystem;

fs::path env_path(const char* name) {
    const char* value = std::getenv(name);
    if (value == nullptr || value[0] == '\0') {
        return {};
    }
    return value;
}

}  // namespace

fs::path executable_path() {
    std::error_code ec;
    fs::path exe;
#if defined(__APPLE__)
    uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    std::string buffer(size, '\0');
    if (_NSGetExecutablePath(buffer.data(), &size) == 0) {
        exe = fs::canonical(fs::path(buffer.c_str()), ec);
    }
#elif defined(__linux__)
    exe = fs::canonical("/proc/self/exe", ec);
#endif
    if (ec) {
        return {};
    }
    return exe;
}

fs::path install_prefix() {
    const auto exe = executable_path();
    if (exe.empty()) {
        return {};
    }
    return exe.parent_path().parent_path();
}

fs::path stdlib_root() {
    const auto from_env = env_path("YUKT_STD");
    if (!from_env.empty()) {
        return from_env;
    }
#ifdef YUKT_STDLIB_DIR
    {
        const std::string configured = YUKT_STDLIB_DIR;
        if (!configured.empty() && fs::exists(configured)) {
            return configured;
        }
    }
#endif
    const auto prefix = install_prefix();
    if (prefix.empty()) {
        return {};
    }
    return prefix / "share" / "yukt" / "std";
}

fs::path prefix_runtime_archive() {
    const auto prefix = install_prefix();
    if (prefix.empty()) {
        return {};
    }
    return prefix / "lib" / "libyukt_runtime.a";
}

fs::path runtime_archive() {
    const std::string configured = YUKT_RUNTIME_LIB;
    if (!configured.empty() && fs::exists(configured)) {
        return configured;
    }
    const auto candidate = prefix_runtime_archive();
    if (!candidate.empty() && fs::exists(candidate)) {
        return candidate;
    }
    return {};
}

fs::path templates_root() {
    const auto from_env = env_path("YUKT_TEMPLATES");
    if (!from_env.empty()) {
        return from_env;
    }
    const std::string configured = YUKT_TEMPLATES_DIR;
    if (!configured.empty() && fs::exists(configured)) {
        return configured;
    }
    const auto prefix = install_prefix();
    if (prefix.empty()) {
        return {};
    }
    return prefix / "share" / "yukt" / "templates";
}

}  // namespace yukt
