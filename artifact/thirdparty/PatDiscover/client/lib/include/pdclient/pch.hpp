#ifndef PCH_HPP
#define PCH_HPP

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <csignal>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <random>
#include <sstream>
#include <string>
#include <variant>
#include <vector>

#include <google/protobuf/stubs/common.h>
#include <nlohmann/json.hpp>

#include <openfhe.h>
#include <ciphertext-ser.h>
#include <cryptocontext-ser.h>
#include <key/key-ser.h>
#include <scheme/bfvrns/bfvrns-ser.h>
#include <scheme/ckksrns/ckksrns-ser.h>

#include "pdshared/config/Parameters.hpp"
#include "pdshared/logging/Logger.hpp"
#include "pdshared/model/DataTransferObjects.hpp"
#include "pdshared/model/MatchableAttribute.hpp"

#endif //PCH_HPP
