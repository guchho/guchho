#pragma once

#include <cstdint>

#include "guchho/compat.hpp"
#include "guchho/logger.hpp"

namespace guchho::javascript {

    // The runtime source is always at a special index. The index is always zero
    // but this constant is always used instead to improve readability and ensure
    // all code that references this index can be discovered easily.
    constexpr uint32_t kSourceIndex = 0;

    // Generates the runtime source, including or omitting portions of the helper
    // code depending on which JavaScript features are unsupported.
    logger::Source Source(compat::JSFeature unsupported_js_features);

}
