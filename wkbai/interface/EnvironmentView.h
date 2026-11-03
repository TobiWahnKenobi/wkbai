// wkbai - the client's day and night, seasons and weather (EnvironmentView.cpp)
// Licensed under the GNU General Public License 3

#pragma once

#include "../Environment.h"
#include <array>
#include <utility>
#include <vector>

struct Client;
struct Scene;
struct Camera;
struct IRenderer;

// every frame before the scene is drawn: g_environment from the server's state, the sun and fog
// written into the terrain (the map's own put back when it is off) and the scene
void UpdateEnvironmentView(Client* client, Scene* scene);
// the HUD's readout at the top of the screen (ImGui frame): time, season, weather, temperature
void DrawEnvironmentHud(Client* client, float screenWidth);
// the rain, the snow and the lightning bolt, in the particles' pass
void DrawWeather(IRenderer* gfx, Client* client, const Camera& camera);
// at night, a warm light at each built building near the camera (the fire lights' list: distance
// squared, then x y z radius r g b; ranked after every fire)
void AddNightLights(Client* client, const Vector3& camera, std::vector<std::pair<float, std::array<float, 7>>>& list);
