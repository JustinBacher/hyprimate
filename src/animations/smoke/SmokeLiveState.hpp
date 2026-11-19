#pragma once

#include "Smoke.hpp"
#include "SmokeRenderer.hpp"

struct SSmokeStyle {
    hyprimate::Smoke smoke;
    // Created on first render; dropped when the smoke settles so idle windows hold no GPU memory.
    SP<CSmokeFieldHandle> field;
};
