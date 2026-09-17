#ifndef PCH_HPP
#define PCH_HPP

// Allow attaching the private key to the crypto context in debug mode
#if PD_DEBUG
#define DEBUG_KEY
#endif

#include <cstdio>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <csignal>
#include <ctime>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <map>
#include <random>
#include <ranges>
#include <regex>
#include <sstream>
#include <string>
#include <variant>
#include <vector>

#include <google/protobuf/stubs/common.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <openfhe.h>
#include <ciphertext-ser.h>
#include <cryptocontext-ser.h>
#include <key/key-ser.h>
#include <scheme/bfvrns/bfvrns-ser.h>
#include <scheme/ckksrns/ckksrns-ser.h>

#endif //PCH_HPP
