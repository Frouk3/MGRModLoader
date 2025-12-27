#pragma once
#include "Hotkey.h"
#include <vadefs.h>
#include <Hw.h>

namespace gui
{
	inline Hotkey GUIHotkey = Hotkey(Hw::KB_F2, Hw::KB_ALT_L, "GUIHotkey", nullptr, Hotkey::eHotkeyType(Hotkey::HT_TOGGLE));

	void RenderWindow();

	namespace OnReset
	{
		void Before();
		void After();
	}

	void TextCentered(const char* text);
	// Creates a specified symbol text
	// If the item was hovered it will setup a tooltip
	void HelpTip(const char *description, const char* symbol = "(?)");
	// If the item was hovered, setup a tooltip
	void HintV(const char* format, va_list args);
	// If the item was hovered, setup a tooltip
	void Hint(const char* format, ...);
	void LoadStyle();

	void OnEndScene();
}