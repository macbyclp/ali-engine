#pragma once
#include "aicontrol/commands.hpp"

namespace eng {
// `engine --microbench N`: times the CPU-side scene machinery on an N-entity scene and
// prints one JSON line (world transforms, incremental mesh resolve, spawn into a big
// scene, undo history). Used to measure before/after of performance work.
int run_microbench(int n, CommandContext& ctx);
} // namespace eng
