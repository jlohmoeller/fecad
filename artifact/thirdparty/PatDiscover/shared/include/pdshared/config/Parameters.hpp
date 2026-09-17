#ifndef SERVER_PARAMETERS_HPP
#define SERVER_PARAMETERS_HPP

#include "pdshared/logging/Logger.hpp"

#define PD_ASSERT(cond, ...) if (!(cond)) { PD_ERROR(__VA_ARGS__); std::raise(SIGTRAP); }

// We need at least this ring dimension to encrypt using packed plaintexts
constexpr uint32_t k_BFVRingDimension = 32768;
constexpr uint32_t k_CKKSRingDimension = 131072;

// Packing
constexpr uint32_t k_CKKSBatchSize = k_CKKSRingDimension / 2; // Must be a power of two
constexpr uint32_t k_BFVBatchSize = k_BFVRingDimension;
constexpr uint32_t k_CKKSSchemeSwitchBatchSize = 1024;

constexpr uint32_t k_BFVPlaintextMod = 65537;
constexpr uint32_t k_BinFhePlaintextMod = 20000;

constexpr double k_ContinuousApproxMaxValue = 1.2;
constexpr double k_ContinuousApproxMinDiff = 0.008;

constexpr double k_DistanceApproxMaxValue = 1.25;
constexpr double k_DistanceApproxMinDiff = 0.0001;

#endif //SERVER_PARAMETERS_HPP
