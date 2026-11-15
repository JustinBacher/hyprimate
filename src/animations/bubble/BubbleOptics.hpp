#pragma once

#include <vector>

namespace liquid_glass {

// Linear-RGB reflectance of a soap film in air on a thickness x incidence-cosine grid,
// row-major by cosine: rgb[3 * (cosineIndex * thicknessSamples + thicknessIndex) + channel].
// Thickness spans [0, maxThickness] nm and the cosine [0, 1], both inclusive.
struct BubbleThinFilmTable {
    int                thicknessSamples = 0;
    int                cosineSamples = 0;
    double             maxThickness = 0;
    std::vector<float> rgb;
};

BubbleThinFilmTable bubbleThinFilmTable(int thicknessSamples = 512, int cosineSamples = 32, double maxThickness = 1500);

} // namespace liquid_glass
