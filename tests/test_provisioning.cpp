#include "openhdprovisioning.h"
#include "sysutil_provisioning.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <unistd.h>
namespace fs = std::filesystem;
int main(int argc, char** argv) {
    if (argc == 4 && std::string(argv[1]) == "--apply") {
        std::ifstream input(argv[2], std::ios::binary);
        std::string capsule(16, '\0'), error;
        if (!input.read(&capsule[0], 16)) return 1;
        const auto length = openhd_provisioning::get32(capsule.data() + 8);
        if (length < 4 || length > openhd_provisioning::max_size - 16) return 1;
        capsule.resize(length + 16);
        if (!input.read(&capsule[16], length)) return 1;
        const bool ok = sysutil::apply_provisioning(capsule, argv[3], error);
        if (!ok) std::cerr << error << '\n';
        return ok ? 0 : 1;
    }
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
