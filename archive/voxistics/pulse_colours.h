// pulse_colours.h
//
// The three pulse colours -- plain, clockwise, anticlockwise (Part VI) --
// for each colour-vision mode (Accessibility, S064). Typical vision gets
// warm yellow-amber, a strong blue and a strong red. The other modes swap
// in a trio that kind of vision tells apart, keeping clockwise and
// anticlockwise unmistakable (owner: that distinction matters most):
//   red-weak and green-weak (protan, deutan): amber and red run together
//     (both read as yellow-brown), so plain becomes a clear yellow and
//     anticlockwise a near-white; blue stays blue.
//   blue-weak (tritan): amber and red run together the other way (both
//     pinkish), so plain becomes near-white, clockwise a cyan-blue and
//     anticlockwise stays red.
//   no colour: three clearly different brightnesses.
// Spin never rests on colour alone: the store's swirl mark, a bead's turn,
// the corkscrew in the air and a twisted pipe's thread all show its hand.

#pragma once

enum ColourVision { CV_TYPICAL = 0, CV_RED_WEAK, CV_GREEN_WEAK, CV_BLUE_WEAK, CV_NO_COLOUR, CV_COUNT };

static inline const char* ColourVisionName(int m) {
    static const char* n[CV_COUNT] = { "TYPICAL", "RED-WEAK", "GREEN-WEAK", "BLUE-WEAK", "NO COLOUR" };
    return n[(m >= 0 && m < CV_COUNT) ? m : 0];
}

// out[spinIndex][rgb]: spin index 0 plain, 1 clockwise, 2 anticlockwise.
static inline void PulseColours(int mode, float out[3][3]) {
    static const float kSets[CV_COUNT][3][3] = {
        { { 1.00f, 0.82f, 0.40f }, { 0.22f, 0.42f, 1.00f }, { 1.00f, 0.20f, 0.18f } }, // typical
        { { 1.00f, 0.92f, 0.20f }, { 0.20f, 0.45f, 1.00f }, { 0.95f, 0.95f, 0.95f } }, // red-weak
        { { 1.00f, 0.92f, 0.20f }, { 0.20f, 0.45f, 1.00f }, { 0.95f, 0.95f, 0.95f } }, // green-weak
        { { 0.95f, 0.95f, 0.95f }, { 0.10f, 0.72f, 0.92f }, { 1.00f, 0.18f, 0.20f } }, // blue-weak
        { { 0.62f, 0.62f, 0.62f }, { 1.00f, 1.00f, 1.00f }, { 0.28f, 0.28f, 0.28f } }, // no colour
    };
    int m = (mode >= 0 && mode < CV_COUNT) ? mode : 0;
    for (int k = 0; k < 3; k++) for (int c = 0; c < 3; c++) out[k][c] = kSets[m][k][c];
}
