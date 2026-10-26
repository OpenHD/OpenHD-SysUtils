// OpenHD provisioning capsule v1. Shared wire format with OpenHD-SysUtils.
// SPDX-License-Identifier: Apache-2.0
#ifndef OPENHD_PROVISIONING_H
#define OPENHD_PROVISIONING_H
#include <cstdint>
#include <string>
#include <vector>
#include <set>

namespace openhd_provisioning {
constexpr std::size_t max_size = 16 * 126976; // Reserved static UBI volume capacity.
struct File { std::string path, data; };
inline uint32_t crc32(const char *data, std::size_t size) {
    uint32_t crc = 0xffffffffU;
    for (std::size_t i = 0; i < size; ++i) {
        crc ^= static_cast<unsigned char>(data[i]);
        for (int bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ (0xedb88320U & (0U - (crc & 1)));
    }
    return crc ^ 0xffffffffU;
}
inline uint32_t get32(const char *p) {
    return (uint32_t(uint8_t(p[0])) << 24) | (uint32_t(uint8_t(p[1])) << 16) |
           (uint32_t(uint8_t(p[2])) << 8) | uint8_t(p[3]);
}
inline void put32(std::string &s, uint32_t n) {
    for (int shift = 24; shift >= 0; shift -= 8) s.push_back(char(n >> shift));
}
inline bool allowed(const std::string &path) {
    if (path == "openhd/air.txt" || path == "openhd/ground.txt" ||
        path == "openhd/settings.json" || path == "openhd/QOpenHD.conf" ||
        path == "openhd/premium_certificate.ohdcert") return true;
    const std::string prefix = "openhd/maps/";
    if (path.compare(0, prefix.size(), prefix) != 0) return false;
    const auto leaf = path.substr(prefix.size());
    return !leaf.empty() && leaf.size() <= 240 && leaf != "." && leaf != ".." &&
        leaf.find_first_of("/\\:") == std::string::npos &&
        leaf.find('\0') == std::string::npos && leaf.find_first_of("\r\n") == std::string::npos;
}
inline std::string encode(const std::vector<File> &files) {
    if (files.size() > 128) return {};
    std::string body;
    put32(body, uint32_t(files.size()));
    std::set<std::string> names;
    for (const auto &file : files) {
        if (!allowed(file.path) || !names.insert(file.path).second || file.data.size() > max_size) return {};
        put32(body, uint32_t(file.path.size())); put32(body, uint32_t(file.data.size()));
        body += file.path; body += file.data;
        if (body.size() > max_size - 16) return {};
    }
    std::string capsule("OHDCFG1\0", 8);
    put32(capsule, uint32_t(body.size())); put32(capsule, crc32(body.data(), body.size()));
    return capsule + body;
}
inline bool decode(const std::string &capsule, std::vector<File> &files) {
    files.clear();
    if (capsule.size() < 20 || capsule.size() > max_size || capsule.compare(0, 8, "OHDCFG1\0", 8)) return false;
    const uint32_t length = get32(capsule.data() + 8);
    if (length != capsule.size() - 16 || get32(capsule.data() + 12) != crc32(capsule.data() + 16, length)) return false;
    const uint32_t count = get32(capsule.data() + 16);
    if (count > 128) return false;
    std::size_t pos = 20;
    std::set<std::string> names;
    std::vector<File> parsed;
    for (uint32_t i = 0; i < count; ++i) {
        if (capsule.size() - pos < 8) return false;
        const auto pathLength = get32(capsule.data() + pos), dataLength = get32(capsule.data() + pos + 4);
        pos += 8;
        if (pathLength > capsule.size() - pos) return false;
        const std::string path = capsule.substr(pos, pathLength); pos += pathLength;
        if (!allowed(path) || !names.insert(path).second || dataLength > capsule.size() - pos) return false;
        parsed.push_back({path, capsule.substr(pos, dataLength)}); pos += dataLength;
    }
    if (pos != capsule.size()) return false;
    files.swap(parsed);
    return true;
}
}
#endif
