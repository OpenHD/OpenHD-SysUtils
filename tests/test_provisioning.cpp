#include "openhdprovisioning.h"
#include "sysutil_provisioning.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <unistd.h>
namespace fs = std::filesystem;
int main() {
    auto root = fs::temp_directory_path() / ("ohd-provisioning-" + std::to_string(getpid()));
    fs::create_directories(root / "config");
    const auto config = root / "config";
    std::string error;
    fs::create_directories(config / "openhd");
    std::ofstream(config / "openhd/air.txt");
    const auto capsule = openhd_provisioning::encode({{"openhd/settings.json", "{\"role\":\"ground\"}"}, {"openhd/ground.txt", ""}, {"openhd/maps/test.glidemap", std::string(140000, 'x')}});
    bool ok = sysutil::apply_provisioning(capsule, config, error);
    ok &= !fs::exists(config / "openhd/air.txt") && fs::exists(config / "openhd/ground.txt");
    ok &= fs::file_size(config / "openhd/maps/test.glidemap") == 140000;
    fs::remove(config / "openhd/settings.json");
    ok &= sysutil::apply_provisioning(capsule, config, error) && !fs::exists(config / "openhd/settings.json");
    auto corrupted = capsule; corrupted.back() ^= 1;
    ok &= !sysutil::apply_provisioning(corrupted, config, error);
    ok &= openhd_provisioning::encode({{"../escape", "x"}}).empty();
    ok &= openhd_provisioning::encode({{"openhd/maps/../escape", "x"}}).empty();
    fs::remove(config / ".imagewriter-provisioned");
    fs::remove_all(config / "openhd"); fs::create_directory(root / "outside");
    fs::create_directory_symlink(root / "outside", config / "openhd");
    ok &= !sysutil::apply_provisioning(capsule, config, error) && fs::is_empty(root / "outside");
    fs::remove_all(root);
    if (!ok) std::cerr << "Provisioning regression failed: " << error << '\n';
    return ok ? 0 : 1;
}
