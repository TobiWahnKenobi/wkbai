// wkbai - the client's day and night, seasons and weather (EnvironmentView.cpp)
// Licensed under the GNU General Public License 3

#pragma once

#include "../Environment.h"
#include <array>
#include <string>
#include <utility>
#include <vector>

struct Client;
struct Scene;
struct Camera;
struct IRenderer;

// every frame before the scene is drawn: g_environment from the server's state, the sun and fog
// written into the terrain (the map's own put back when it is off) and the scene
void UpdateEnvironmentView(Client* client, Scene* scene);
// the HUD's readout (the HUD script draws it under the resources): time, season, weather,
// temperature, flood, ice; "" when off
std::string EnvironmentHudText(Client* client);
// on the ground, before the particles: the flood water spreading over the land beside the
// rivers and lakes (as the server floods it), and the footprints in snow and mud (High and Ultra)
void DrawGroundWeather(IRenderer* gfx, Client* client, const Camera& camera);
// over the minimap's map rectangle (under the fog of war): the lying snow on the flatter land,
// the ice on the water
void DrawMinimapWeather(IRenderer* gfx, Client* client, void* drawList, float x0, float y0, float x1, float y1);
// every frame: the waterfalls heard (the nearest few of g_waterfallSoundSpots, looping), as loud
// and as deep as the water they carry (WaterfallFlow: a flood roars, a drought trickles)
void UpdateWaterfallSounds(Client* client);
// the rain, the snow, the lightning bolt and the falling leaves, in the particles' pass
void DrawWeather(IRenderer* gfx, Client* client, const Camera& camera);
// the broadleaf trees in winter: each loses its leaves at its own moment early in winter (in the
// first 12% of it, by a hash of its id, so a replay looks the same), all bare after that
bool TreeBareInWinter(Client* client, uint32_t id);
// a fir under snow (the game set's "Snowy Tree" look, SnowyFirModel): once the lying snow is deep
// enough, each at its own depth (a cover between 0.15 and 0.45, by a hash of its id) so the forest
// whitens tree by tree as the snow builds up and greens again as it melts
bool TreeSnowLaden(Client* client, uint32_t id);
// a broadleaf tree in leaf drawn this frame (its bounding sphere): its leaves fall around it in
// autumn (DrawWeather, the nearest ones)
void AddLeafTree(Client* client, const Vector3& centre, float radius, uint32_t id);
// at night, a warm light at each built building near the camera (the fire lights' list: distance
// squared, then x y z radius r g b; ranked after every fire)
void AddNightLights(Client* client, const Vector3& camera, std::vector<std::pair<float, std::array<float, 7>>>& list);
