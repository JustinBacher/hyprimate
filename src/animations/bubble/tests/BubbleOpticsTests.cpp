#include <algorithm>
#include <cmath>

#include "Check.hpp"
#include "animations/bubble/BubbleOptics.hpp"

int main() {
    const auto TABLE = hyprimate::bubbleThinFilmTable(301, 33, 1500);
    CHECK(TABLE.rgb.size() == 301 * 33 * 3);
    const auto SAMPLE = [&](int d, int c, int channel) {
        return TABLE.rgb[(3 * ((c * 301) + (d / 5))) + channel];
    };
    for (const float VALUE : TABLE.rgb) {
        CHECK(std::isfinite(VALUE) && VALUE >= 0 && VALUE <= 1.05);
    }
    for (int cosineSample = 0; cosineSample < 33; ++cosineSample) {
        for (int k = 0; k < 3; ++k) {
            CHECK(SAMPLE(0, cosineSample, k) == 0);
        }
    }
    for (int k = 0; k < 3; ++k) {
        CHECK(SAMPLE(200, 0, k) > 0.98);
    }
    float maximum = 0;
    for (int thickness_index = 50; thickness_index <= 200; thickness_index += 5) {
        maximum = std::max(maximum,
            (0.2126F * SAMPLE(thickness_index, 32, 0)) +
                (0.7152F * SAMPLE(thickness_index, 32, 1)) +
                (0.0722F * SAMPLE(thickness_index, 32, 2)));
    }
    CHECK(maximum > 0.06 && maximum < 0.09);
    const auto SPREAD = [&](int d) {
        const auto [MINIMUM, MAXIMUM] =
            std::minmax({SAMPLE(d, 32, 0), SAMPLE(d, 32, 1), SAMPLE(d, 32, 2)});
        return MAXIMUM - MINIMUM;
    };
    CHECK(std::max(SPREAD(450), SPREAD(500)) > SPREAD(200));
    CHECK(hyprimate::bubbleThinFilmTable(1, 2, 100).rgb.empty());
    return g_failures == 0 ? 0 : 1;
}
