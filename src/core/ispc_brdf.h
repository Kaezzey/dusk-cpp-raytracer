#pragma once
#include <cstddef>

// Batch-normalize float3 triples in-place. When compiled with ISPC this
// forwards to the ISPC kernel; otherwise a simple CPU fallback is used.
void ispc_normalize_batch(float* xyz_inout, int count);
