// wkbre2 - WK Engine Reimplementation
// (C) 2021 AdrienTD
// Licensed under the GNU General Public License 3

#include "Camera.h"
#include "window.h"

float g_projectionJitter[2] = {};

void Camera::updateMatrix() {
	aspect = (float)g_windowWidth / g_windowHeight;
	this->direction = Vector3(
		-sin(orientation.y)*cos(orientation.x),
		sin(orientation.x),
		cos(orientation.y)*cos(orientation.x)
	);
	if (orthoMode)
		projMatrix = Matrix::getLHOrthoMatrix(position.y*aspect, position.y, nearDist, farDist);
	else {
		projMatrix = Matrix::getLHPerspectiveMatrix(fovY, aspect, nearDist, farDist);
		// clip x and y moved by the jitter times w (the view depth)
		projMatrix._31 += g_projectionJitter[0];
		projMatrix._32 += g_projectionJitter[1];
	}
	viewMatrix = Matrix::getLHLookAtViewMatrix(position, position + direction, Vector3(0, 1, 0));
	this->sceneMatrix = viewMatrix * projMatrix;
}