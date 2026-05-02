-- Depth buffer visualization overlay (diagnostic post-process effect).
--
-- Single pass, reads SCENE_COLOR and SCENE_DEPTH, writes SWAPCHAIN.
-- The fragment shader composites a grayscale depth thumbnail in the
-- top-right corner on top of the unmodified scene color.

effect:pass("depthviz", "SWAPCHAIN")
effect:sampler(0, "SCENE_COLOR")
effect:sampler(1, "SCENE_DEPTH")

-- Tunable parameters (exposed as MVP_DEPTHVIZ_* config items while loaded)
effect:param_float("size",        0, 0.25, 0.05, 0.9,
    "Thumbnail width as a fraction of the screen")
effect:param_float("rawMin",      1, 0.5,  0.0,  1.0,
    "Raw depth value mapped to WHITE (nearest — lower = closer zoom to near plane)")
effect:param_float("rawMax",      2, 0.99, 0.0,  1.0,
    "Raw depth value mapped to BLACK (farthest — raise to zoom into far distance)")
effect:param_float("border",      3, 2.0,  0.0,  10.0,
    "Border thickness in pixels")
effect:param_vec4 ("borderColor", 4, 1.0, 1.0, 0.0, 1.0,
    "Color of the rectangle outline (RGBA)")
effect:param_float("gamma",       5, 1.0,  0.1,  10.0,
    "Post-gamma curve. 1 = linear; >1 brightens midtones; <1 darkens")
