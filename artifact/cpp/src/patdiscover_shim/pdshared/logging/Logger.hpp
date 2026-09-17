// shim: no-op logger, drops spdlog
// PD_INFO is only reached from the CKKS scheme-switch path, which the
// approximate-only binding never enters
#ifndef LOGGER_HPP
#define LOGGER_HPP

#include <iostream>

#define PD_TRACE(...) (void)0
#define PD_INFO(...)  (void)0
#define PD_WARN(...)  (void)0
#define PD_ERROR(...) (void)0

#endif //LOGGER_HPP
