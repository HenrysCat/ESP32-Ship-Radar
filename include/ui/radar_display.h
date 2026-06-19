#pragma once

namespace ui {

/** Draw the static sonar/radar grid (black disc, green overlay, labels). */
void radarDisplayDraw();

/** Redraw vessels only (blits cached grid; no full-screen clear). */
void radarDisplayRefreshVessels();

}  // namespace ui
