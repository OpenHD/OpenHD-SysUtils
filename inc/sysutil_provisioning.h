// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <filesystem>
#include <string>
namespace sysutil {
bool apply_provisioning(const std::string& capsule, const std::filesystem::path& config,
                        std::string& error);
// Reject damaged optional selections without preventing factory startup.
bool import_provisioning_capsule(const std::string& capsule,
                                const std::filesystem::path& config);
// Imports only a static provisioning volume attached to the OHD MTD device.
bool import_ubi_provisioning();
}
