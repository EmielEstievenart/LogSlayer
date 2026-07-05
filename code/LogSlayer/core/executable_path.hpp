#pragma once

#include <filesystem>

namespace slayerlog
{

/// Absolute path of the running executable, or an empty path when the
/// platform query fails. Used when exported scripts must invoke this binary.
std::filesystem::path executable_path();

} // namespace slayerlog
