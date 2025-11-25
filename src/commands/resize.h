#pragma once

#include <string>

bool runResize(const std::string &partitionUuid, const std::string &partitionNumber,
               bool forceResizeRequest = false);
