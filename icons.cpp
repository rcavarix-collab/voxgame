// icons.cpp -- see icons.h.

#include "icons.h"
#include "mesher.h"
#include <algorithm>
#include <cmath>
#include <vector>

void RenderBlockIcons(BlockTextureSet& set) {
    const int N = BLOCK_TEX_SIZE, SS = 2, R = N * SS; // render at 2x, box down to 1x
    // Same lighting the world shader applies (render.cpp).
    const float faceShade[8] = { 0.80f, 0.80f, 1.00f, 0.55f, 0.68f, 0.68f, 0.90f, 0.62f };
    const float cy = cosf(0.785398f), sy = sinf(0.785398f); // 45 degrees around
    const float cp = cosf(0.5236f), sp = sinf(0.5236f);     // 30 degrees down
    const float scale = R * 0.56f;

    std::vector<float> depth((size_t)R * R);
    std::vector<float> rgba((size_t)R * R * 4);
    std::vector<Vertex> verts; std::vector<uint16_t> idx;

    for (int id = 1; id < BLOCK_COUNT; id++) {
        // The block alone in an empty world, placed as if by a player at
        // the camera (which sits off toward -X -Z, looking down at it):
        // fronts face -Z, toward the viewer; ramps rise away.
        World w;
        uint8_t state = 0;
        if (g_blocks[id].place == PLACE_FACE_PLAYER) state = FACE_NEG_Z;
        else if (g_blocks[id].place == PLACE_AWAY) state = FACE_POS_Z;
        else if (g_blocks[id].place == PLACE_CLICKED_AXIS) state = FACE_POS_X;
        w.Set(0, 0, 0, (BlockID)id, state);
        BuildChunkMesh(w, { 0, 0, 0 }, *w.FindChunk({ 0, 0, 0 }), verts, idx);

        std::fill(depth.begin(), depth.end(), 1e9f);
        std::fill(rgba.begin(), rgba.end(), 0.0f);
        auto project = [&](const Vertex& v, float& X, float& Y, float& Z) {
            float x = v.x / 8.0f - 0.5f, y = v.y / 8.0f - 0.5f, z = v.z / 8.0f - 0.5f;
            float rx = x * cy - z * sy, rz = x * sy + z * cy; // smaller rz = nearer the camera
            X = R * 0.5f + rx * scale;
            Y = R * 0.5f - (y * cp + rz * sp) * scale;
            Z = rz * cp - y * sp;
        };
        for (size_t i = 0; i + 2 < idx.size(); i += 3) {
            const Vertex* tv[3] = { &verts[idx[i]], &verts[idx[i + 1]], &verts[idx[i + 2]] };
            float X[3], Y[3], Z[3];
            for (int k = 0; k < 3; k++) project(*tv[k], X[k], Y[k], Z[k]);
            float den = (Y[1] - Y[2]) * (X[0] - X[2]) + (X[2] - X[1]) * (Y[0] - Y[2]);
            if (fabsf(den) < 1e-6f) continue;
            int x0 = std::max(0, (int)std::min({ X[0], X[1], X[2] })), x1 = std::min(R - 1, (int)std::max({ X[0], X[1], X[2] }) + 1);
            int y0 = std::max(0, (int)std::min({ Y[0], Y[1], Y[2] })), y1 = std::min(R - 1, (int)std::max({ Y[0], Y[1], Y[2] }) + 1);
            const uint8_t* tex = set.mips[0].data() + (size_t)tv[0]->layer * N * N * 4;
            float light = faceShade[VertexFace(*tv[0])];
            for (int py = y0; py <= y1; py++)
                for (int px = x0; px <= x1; px++) {
                    float fx = px + 0.5f, fy = py + 0.5f;
                    float a = ((Y[1] - Y[2]) * (fx - X[2]) + (X[2] - X[1]) * (fy - Y[2])) / den;
                    float b = ((Y[2] - Y[0]) * (fx - X[2]) + (X[0] - X[2]) * (fy - Y[2])) / den;
                    float c = 1.0f - a - b;
                    if (a < 0 || b < 0 || c < 0) continue;
                    float z = a * Z[0] + b * Z[1] + c * Z[2];
                    float& d = depth[(size_t)py * R + px];
                    if (z >= d) continue;
                    d = z;
                    float u = (a * tv[0]->u + b * tv[1]->u + c * tv[2]->u) / 8.0f;
                    float v = (a * tv[0]->v + b * tv[1]->v + c * tv[2]->v) / 8.0f;
                    int tx = (((int)floorf(u * N)) % N + N) % N, ty = (((int)floorf(v * N)) % N + N) % N;
                    const uint8_t* t = tex + ((size_t)ty * N + tx) * 4;
                    float* o = &rgba[((size_t)py * R + px) * 4];
                    o[0] = t[0] * light; o[1] = t[1] * light; o[2] = t[2] * light; o[3] = 255.0f;
                }
        }
        // 2x2 box down into the icon cell (soft silhouette edges).
        for (int y = 0; y < N; y++)
            for (int x = 0; x < N; x++) {
                float acc[4] = { 0, 0, 0, 0 };
                for (int sy2 = 0; sy2 < SS; sy2++)
                    for (int sx2 = 0; sx2 < SS; sx2++) {
                        const float* s = &rgba[((size_t)(y * SS + sy2) * R + (x * SS + sx2)) * 4];
                        for (int c = 0; c < 3; c++) acc[c] += s[c] * (s[3] / 255.0f);
                        acc[3] += s[3];
                    }
                uint8_t* o = set.icons.data() + ((size_t)y * set.iconsW + (size_t)id * N + x) * 4;
                float alpha = acc[3] / (SS * SS);
                float cov = alpha / 255.0f;
                for (int c = 0; c < 3; c++) o[c] = (uint8_t)(cov > 0 ? std::min(255.0f, acc[c] / (SS * SS) / cov) : 0.0f);
                o[3] = (uint8_t)std::min(255.0f, alpha);
            }
    }
}
