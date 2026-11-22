// wkbre2 - WK Engine Reimplementation
// (C) 2021 AdrienTD
// Licensed under the GNU General Public License 3

#include "help/Help.h"
#include "AgentUi.h"
#include "imguiimpl.h"
#include "imgui/imgui.h"
#include "gfx/renderer.h"
#include "gfx/bitmap.h"
#include "window.h"
#include "AgentInput.h"
#include "AgentServer.h"
#include <algorithm>
#include <cstdint>
#include <SDL3/SDL.h>


uint32_t iglasttime;
char *imguiFontFile = 0;
float imguiFontSize = 12;

#define BGRA_TO_RGBA(x) ( (((x)&0xFF)<<16) | (((x)&0xFF0000)>>16) | ((x)&0xFF00FF00) )

void ImGuiImpl_RenderDrawLists(ImDrawData *dr, IRenderer *renderer)
{
	ImGuiIO &io = ImGui::GetIO();
	//if(winMinimized) return;

	renderer->BeginBatchDrawing();
	renderer->EnableScissor();

	// A draw list over 65536 vertices (the minimap's dots of a map with some 16,000 objects or more:
	// a forest of 20,000 trees) comes in parts: ImGui gives each command a VtxOffset its 16-bit
	// indices count from (ImGuiBackendFlags_RendererHasVtxOffset, set in ImGuiImpl_Init). Each run of
	// commands with the same VtxOffset gets its own vertex and index buffers (before, the indices
	// wrapped past 65535 and the UI's panels filled with stray triangles)
	for(int i = 0; i < dr->CmdListsCount; i++)
	{
		ImDrawList *cl = dr->CmdLists[i];
		for(int j = 0; j < cl->CmdBuffer.size(); )
		{
			const unsigned int vtxOffset = cl->CmdBuffer[j].VtxOffset;
			int k = j;
			while(k < cl->CmdBuffer.size() && cl->CmdBuffer[k].VtxOffset == vtxOffset)
				k++;
			// the run's vertices: from its offset to the next run's (or the end), at most 65536
			const unsigned int vtxEnd = k < cl->CmdBuffer.size() && cl->CmdBuffer[k].VtxOffset > vtxOffset ? cl->CmdBuffer[k].VtxOffset : (unsigned int)cl->VtxBuffer.size();
			const int nv = (int)std::min<unsigned int>(vtxEnd - vtxOffset, 65536u);
			int ni = 0;
			for(int c = j; c < k; c++)
				ni += cl->CmdBuffer[c].ElemCount;
			if(nv <= 0 || ni <= 0) { j = k; continue; }

			RVertexBuffer *vb = renderer->CreateVertexBuffer(nv);
			RIndexBuffer *ib = renderer->CreateIndexBuffer(ni);
			batchVertex *vm = vb->lock();
			for(int v = 0; v < nv; v++)
			{
				ImDrawVert *a = &cl->VtxBuffer[vtxOffset + v];
				vm[v].x = a->pos.x * g_uiScale;
				vm[v].y = a->pos.y * g_uiScale;
				vm[v].z = 0;
				vm[v].color = BGRA_TO_RGBA(a->col);
				vm[v].u = a->uv.x;
				vm[v].v = a->uv.y;
			}
			vb->unlock();
			uint16_t *im = ib->lock();
			int w = 0;
			for(int c = j; c < k; c++)
			{
				const ImDrawCmd *cmd = &cl->CmdBuffer[c];
				for(unsigned int q = 0; q < cmd->ElemCount; q++)
					im[w++] = cl->IdxBuffer[cmd->IdxOffset + q];
			}
			ib->unlock();
			renderer->SetVertexBuffer(vb);
			renderer->SetIndexBuffer(ib);

			int e = 0;
			for(int c = j; c < k; c++)
			{
				ImDrawCmd *cmd = &cl->CmdBuffer[c];
				renderer->SetTexture(0, (texture)cmd->GetTexID());
				renderer->SetScissorRect((int)(cmd->ClipRect.x * g_uiScale), (int)(cmd->ClipRect.y * g_uiScale),
							(int)((cmd->ClipRect.z - cmd->ClipRect.x) * g_uiScale),
							(int)((cmd->ClipRect.w - cmd->ClipRect.y) * g_uiScale));
				renderer->DrawBuffer(e, cmd->ElemCount);
				e += cmd->ElemCount;
			}

			delete vb; delete ib;
			j = k;
		}
	}

	renderer->DisableScissor();
}

void ImGuiImpl_CreateFontsTexture(IRenderer *gfx)
{
	ImGuiIO &io = ImGui::GetIO();
	uint8_t *pix; int w, h, bpp;
	if(imguiFontFile)
		io.Fonts->AddFontFromFileTTF(imguiFontFile, imguiFontSize);
	// the help's own fonts (help/Help.cpp), after the default font so that it stays ImGui's default
	if (io.Fonts->Fonts.empty())
		io.Fonts->AddFontDefault();
	Help::AddFonts(io.Fonts);
	io.Fonts->GetTexDataAsRGBA32(&pix, &w, &h, &bpp);
	Bitmap bm;
	bm.width = w; bm.height = h; bm.format = BMFORMAT_B8G8R8A8;
	bm.pixels.resize(w * h * 4);
	memcpy(bm.pixels.data(), pix, w * h * 4);
	texture t = gfx->CreateTexture(bm, 1);
	io.Fonts->TexID = (void*)t;
}

void ImGuiImpl_Init()
{
	ImGui::CreateContext();
	//imguienabled = true;
	ImGuiIO &io = ImGui::GetIO();
	// large draw lists in parts (ImGuiImpl_RenderDrawLists)
	io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;

	//io.ImeWindowHandle = WndGetNativeWindowHandle(); // FIXME (as in XXL Editor)

	iglasttime = SDL_GetTicks();
}

void ImGuiImpl_NewFrame()
{
	ImGuiIO &io = ImGui::GetIO();
	// in points (window.h): the draw lists are scaled to pixels when rendered
	io.DisplaySize = ImVec2((float)g_windowWidth / g_uiScale, (float)g_windowHeight / g_uiScale);

	uint32_t newtime = SDL_GetTicks();
	io.DeltaTime = std::max((newtime - iglasttime) / 1000.f, 0.001f);
	iglasttime = newtime;

	const int kmod = InputModState();
	io.KeyCtrl = kmod & SDL_KMOD_CTRL;
	io.KeyShift = kmod & SDL_KMOD_SHIFT;
	io.KeyAlt = kmod & SDL_KMOD_ALT;
	io.KeySuper = false;

	ImGui::NewFrame();
}

void ImGuiImpl_Render(IRenderer *gfx, bool agentShot)
{
	Help::Frame(gfx); // the help window and its right-click menu, over everything else
	AgentInput::DrawCursorOverlay(); // the agent's cursor and its tag, while it has the input
	ImGui::Render();
	ImGuiImpl_RenderDrawLists(ImGui::GetDrawData(), gfx);
	if (agentShot)
		AgentServer::TakeScreenshotIfAsked(gfx); // the frame as the player sees it, if the agent asked
	AgentUi::EndFrame(); // this frame's buttons and items: what the agent can aim at
}
