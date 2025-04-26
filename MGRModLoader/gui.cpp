#include "gui.h"

#include "imgui/imgui.h"
#include "imgui/imgui_impl_dx9.h"
#include "imgui/imgui_impl_win32.h"

#include <Hw.h>

static WNDPROC oWndProc = NULL;

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

LRESULT CALLBACK hkWindowProc(
	_In_ HWND   hwnd,
	_In_ UINT   uMsg,
	_In_ WPARAM wParam,
	_In_ LPARAM lParam
)
{
	if (ImGui_ImplWin32_WndProcHandler(hwnd, uMsg, wParam, lParam) > 0)
		return 1L;
	return ::CallWindowProcA(oWndProc, hwnd, uMsg, wParam, lParam);
}

void gui::OnReset::Before()
{
	ImGui_ImplDX9_InvalidateDeviceObjects();
}

void gui::OnReset::After()
{
	ImGui_ImplDX9_CreateDeviceObjects();
}

void gui::OnEndScene()
{
	static bool init = false;

	if (!init)
	{
		oWndProc = (WNDPROC)::SetWindowLongPtr(Hw::OSWindow, GWLP_WNDPROC, (LONG)hkWindowProc);

		ImGui::CreateContext();
		ImGui_ImplWin32_Init(Hw::OSWindow);
		ImGui_ImplDX9_Init(Hw::GraphicDevice);

		gui::LoadStyle();

		init = true;
	}

	ImGui_ImplDX9_NewFrame();
	ImGui_ImplWin32_NewFrame();
	ImGui::NewFrame();

	gui::RenderWindow();

	ImGui::EndFrame();
	ImGui::Render();
	ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());
}

void gui::TextCentered(const char* text)
{
	ImVec2 size = ImGui::CalcTextSize(text);
	ImGui::NewLine();
	float width = ImGui::GetWindowSize().x - size.x;
	ImGui::SameLine(width / 2);
	ImGui::Text(text);
}

void gui::HelpTip(const char* description, const char* symbol)
{
	ImGui::SameLine();
	ImGui::TextDisabled(symbol);

	if (ImGui::IsItemHovered())
		ImGui::SetTooltip(description);
}

void gui::HintV(const char* format, va_list args)
{
	if (ImGui::IsItemHovered())
		ImGui::SetTooltipV(format, args);
}

void gui::Hint(const char* format, ...)
{
	va_list args;
	va_start(args, format);

	HintV(format, args);

	va_end(args);
}

void gui::LoadStyle()
{
	ImGuiStyle& style = ImGui::GetStyle();

	style.ChildRounding = 0.0f;
	style.FrameRounding = 0.0f;
	style.GrabRounding = 0.0f;
	style.PopupRounding = 0.0f;
	style.ScrollbarRounding = 0.0f;
	style.WindowRounding = 0.0f;
	style.TabRounding = 0.0f;
	
	// your style settings here
}