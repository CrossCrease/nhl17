#pragma once

#include <stdint.h>

static void alpha_probe_reg(GnmCommandBuffer* cmd, uint32_t index, uint32_t value) {
    *cmd->cmdptr++ = 0xc0016900u;
    *cmd->cmdptr++ = index;
    *cmd->cmdptr++ = value;
}

static void draw_alpha_probe(GnmCommandBuffer* cmd, uint32_t width, uint32_t height) {
    for (uint32_t row = 0; row < 4; ++row) {
        setupviewport(cmd, 0, row * height / 4, width, height / 4, 0.5f, 0.5f);
        alpha_probe_reg(cmd, 0x2dc, 0xaa00u | (row & 1u));
        alpha_probe_reg(cmd, 0x203, (row & 2u) ? 0x800u : 0u);
        gnmDrawCmdDrawIndexAuto(cmd, 3);
    }
    alpha_probe_reg(cmd, 0x2dc, 0xaa00u);
    alpha_probe_reg(cmd, 0x203, 0u);
}
