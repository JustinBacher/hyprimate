#pragma once

#include <vector>

namespace hyprimate {

    // Linear-RGB reflectance of a soap film in air on a thickness x incidence-cosine grid,
    // row-major by cosine: rgb[3 * (cosineIndex * thickness_samples + thicknessIndex) + channel].
    // Thickness spans [0, max_thickness] nm and the cosine [0, 1], both inclusive.
    struct BubbleThinFilmTable {
        int thickness_samples = 0;
        int cosine_samples = 0;
        double max_thickness = 0;
        std::vector<float> rgb;
    };

    // Invalid dimensions or a grid too large for the RGB vector return an empty table.
    BubbleThinFilmTable bubble_thin_film_table(
        int thickness_samples = 512, int cosine_samples = 32, double max_thickness = 1500);

}  // namespace hyprimate
