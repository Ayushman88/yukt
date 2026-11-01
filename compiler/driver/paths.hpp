#pragma once

#include <filesystem>

namespace yukt {

std::filesystem::path executable_path();
std::filesystem::path install_prefix();
std::filesystem::path stdlib_root();
std::filesystem::path runtime_archive();
std::filesystem::path prefix_runtime_archive();
std::filesystem::path templates_root();

}  // namespace yukt
