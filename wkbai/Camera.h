// wkbre2 - WK Engine Reimplementation
// (C) 2021 AdrienTD
// Licensed under the GNU General Public License 3

#pragma once

#include "util/vecmat.h"

// A sub-pixel offset of the projection this frame, in clip space (TAA's jitter, set by the
// renderer before the frame's camera is updated; 0 otherwise). Not in orthographic views.
extern float g_projectionJitter[2];

struct Camera {
	Vector3 position, orientation;
	float aspect = 4.0f/3.0f, nearDist = 0.1f, farDist = 250.0f;
	float fovY = 0.9f; // vertical field of view, radians
	bool orthoMode = false;
	Matrix sceneMatrix, viewMatrix, projMatrix;
	Vector3 direction;
	void updateMatrix();
	Camera() = default;
	Camera(const Vector3 &position, const Vector3 &orientation) : position(position), orientation(orientation) {}
};