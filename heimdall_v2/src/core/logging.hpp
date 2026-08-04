#pragma once

#include <iostream>

// Simple logging macros
#define LOG_I(msg) std::cout << "[INFO] " << msg << std::endl
#define LOG_W(msg) std::cout << "[WARN] " << msg << std::endl  
#define LOG_E(msg) std::cerr << "[ERROR] " << msg << std::endl
