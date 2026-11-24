layout(location = 0) out vec4 color;

void main() {
    vec2 uv = hypr_window_uv();
    vec4 source = hypr_sample_source(uv);
    float progress = smoothstep(0.0, 1.0, hypr_progress);
    float coverage = hypr_event == 0 ? progress : hypr_event == 1 ? 1.0 - progress : 1.0;
    if (hypr_blur_mask) {
        color = vec4(source.r * coverage);
        return;
    }

    float activity = hypr_event < 2 ? 4.0 * progress * (1.0 - progress) : 1.0 - progress;
    if (activity > 0.0 && source.a > 0.0 &&
        (hypr_wallpaper_processed_available || hypr_wallpaper_original_available)) {
        vec2 radial = 2.0 * uv - 1.0;
        vec2 offset = radial * (1.0 - 0.4 * dot(radial, radial)) * (bend * activity);
        vec2 outputUv = hypr_environment_uv(uv) + offset / max(hypr_output_size, vec2(1.0));
        vec4 wallpaper = hypr_sample_wallpaper_processed(outputUv);
        // Fall back only to the isolated wallpaper when there is no processed pixel.
        if (wallpaper.a <= 0.0) {
            wallpaper = hypr_sample_wallpaper_original(outputUv);
        }
        source.rgb = mix(source.rgb, source.a * hypr_unpremultiply(wallpaper).rgb,
                         0.42 * activity * wallpaper.a);
    }
    color = source * coverage;
}
