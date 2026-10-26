// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <filesystem>
#include <string>
namespace sysutil {
bool apply_provisioning(const std::string& capsule, const std::filesystem::path& config,
                        std::string& error);
// Imports only a static provisioning volume attached to the OHD MTD device.
bool import_ubi_provisioning();
}
