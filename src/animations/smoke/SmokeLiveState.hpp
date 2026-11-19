#pragma once

#include <memory>

#include "Smoke.hpp"

class CSmokeFieldHandle;

struct SSmokeStyle {
    hyprimate::Smoke smoke;
    hyprimate::SmokeParams close_params;
    std::shared_ptr<CSmokeFieldHandle> field;
    std::shared_ptr<CSmokeFieldHandle> capture_field;
};
