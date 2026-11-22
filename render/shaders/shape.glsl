float coverage(float distance) {
    return clamp(0.5 - distance / max(fwidth(distance), 0.0001), 0, 1);
}

float shape_coverage(vec2 p, vec4 rect, float kind, vec4 radii) {
    if (any(lessThanEqual(rect.zw, vec2(0))))
        return 0;
    vec2 half_size = rect.zw * 0.5;
    vec2 delta = p - rect.xy - half_size;
    vec2 distance_to_center = abs(delta);
    if (kind == 2) {
        vec2 radius = max(half_size, vec2(1.17549435e-38));
        vec2 unit = min(distance_to_center / radius, vec2(4));
        float alpha = coverage(length(unit) - 1);
        // Keep the ellipse's AA footprint within the conservative clip bounds.
        vec2 limit = half_size + 2 * fwidth(p);
        return all(lessThanEqual(distance_to_center, limit)) ? alpha : 0;
    }
    float radius = 0;
    if (kind == 1) {
        if (delta.y < 0)
            radius = delta.x < 0 ? radii.x : radii.y;
        else
            radius = delta.x < 0 ? radii.w : radii.z;
    }
    vec2 q = distance_to_center - (half_size - radius);
    return coverage(length(max(q, 0)) + min(max(q.x, q.y), 0) - radius);
}
