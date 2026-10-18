// =============================================================================
//  sim.cpp  -  Shared actor movement
// =============================================================================
#include "sim.h"
#include <cmath>
#include <algorithm>

MoveEvents stepMovement(const World &w, Player &p, const InputCmd &cmd, float dt)
{
    MoveEvents ev;
    float forwardInput = std::max(-1.f, std::min(1.f, cmd.forward));
    float strafeInput  = std::max(-1.f, std::min(1.f, cmd.strafe));

    // Backward movement and strafing are slower than walking forward
    float directionMult = 1.f;
    if (forwardInput < 0.f) directionMult *= 0.78f;
    if (std::fabs(strafeInput) > 0.01f) directionMult *= 0.88f;

    // Local input -> world space. Forward = (dirX, dirY), right = (-dirY, dirX)
    float moveX = p.dirX * forwardInput - p.dirY * strafeInput;
    float moveY = p.dirY * forwardInput + p.dirX * strafeInput;
    float inputLen = std::sqrt(moveX * moveX + moveY * moveY);
    if (inputLen > 1.f) { moveX /= inputLen; moveY /= inputLen; }   // no faster diagonals

    p.crouching = cmd.crouch && !cmd.jump;
    float moveSpeed = p.speedMult * (p.crouching ? (p.adsMode ? 1.3f : 1.7f) : p.sprinting ? 5.8f : p.adsMode ? 2.0f : 3.5f);
    float targetVelX = moveX * moveSpeed * directionMult;
    float targetVelY = moveY * moveSpeed * directionMult;
    bool hasMoveInput = inputLen > 0.001f;

    // Ground: responsive acceleration / strong friction. Air: limited control, keeps momentum
    float response = p.onGround ? (hasMoveInput ? 13.f : 18.f) : (hasMoveInput ? 3.5f : 0.45f);
    float blend = 1.f - std::exp(-response * dt);              // frame-rate independent
    p.velX += (targetVelX - p.velX) * blend;
    p.velY += (targetVelY - p.velY) * blend;
    if (p.onGround && !hasMoveInput && p.velX * p.velX + p.velY * p.velY < 0.0004f) { p.velX = 0.f; p.velY = 0.f; }

    // Axis-separated moves with step-up checks; stop sliding along a wall we hit head-on
    float nx = p.posX + p.velX * dt, ny = p.posY + p.velY * dt;
    bool canMoveX = w.canStep(nx, p.posY, p.footZ, 0.32f, 0.28f, p.posX, p.posY);
    bool canMoveY = w.canStep(p.posX, ny, p.footZ, 0.32f, 0.28f, p.posX, p.posY);
    if (!canMoveX && canMoveY && std::fabs(p.velY) <= 0.40f * std::fabs(p.velX)) canMoveY = false;
    if (!canMoveY && canMoveX && std::fabs(p.velX) <= 0.40f * std::fabs(p.velY)) canMoveX = false;
    if (canMoveX) p.posX = nx; else p.velX = 0.f;
    if (canMoveY) p.posY = ny; else p.velY = 0.f;

    // Jump
    if (cmd.jump && p.onGround) { p.velZ = 5.5f; p.onGround = false; ev.jumped = true; }

    // Vertical: gravity, landing, sticking to slopes when walking downhill
    float g = w.floorH(p.posX, p.posY);
    p.velZ -= 20.f * dt;
    p.footZ += p.velZ * dt;
    if (p.onGround && p.velZ <= 0 && p.footZ - g < 0.12f) { p.footZ = g; p.velZ = 0; }
    if (p.footZ <= g) {
        if (!p.onGround) { ev.landed = true; ev.landVel = p.velZ; }
        p.footZ = g; p.velZ = 0; p.onGround = true;
    } else if (p.footZ > g + 0.12f) p.onGround = false;
    return ev;
}
