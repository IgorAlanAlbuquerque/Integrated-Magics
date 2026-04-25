#pragma once

#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/spdlog.h>

#include "RE/Skyrim.h"
#include "REL/Relocation.h"
#include "SKSE/SKSE.h"

using namespace std::literals;

#define DEBUG

#ifdef DEBUG
    #define MAGIC_DEBUG_LOG(...) spdlog::info(__VA_ARGS__)
#else
    #define MAGIC_DEBUG_LOG(...) ((void)0)
#endif