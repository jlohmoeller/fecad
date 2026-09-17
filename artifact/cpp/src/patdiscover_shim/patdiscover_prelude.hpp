// shim prelude, force-included
// upstream gets these symbols from the pdshared/pdserver precompiled headers,
// which we do not build
#ifndef FECAD_PATDISCOVER_PRELUDE_HPP
#define FECAD_PATDISCOVER_PRELUDE_HPP

#include <algorithm>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <ranges>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <openfhe.h>
#include <ciphertext-ser.h>
#include <cryptocontext-ser.h>
#include <key/key-ser.h>
#include <scheme/bfvrns/bfvrns-ser.h>
#include <scheme/ckksrns/ckksrns-ser.h>

#include "pdshared/config/Parameters.hpp"
#include "pdshared/model/DataTransferObjects.hpp"

#endif // FECAD_PATDISCOVER_PRELUDE_HPP
