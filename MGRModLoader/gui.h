#pragma once

namespace gui
{
	inline bool bShow = false;

	void RenderWindow();

	namespace OnReset
	{
		void Before();
		void After();
	}

	void TextCentered(const char* text);
	void LoadStyle();

	void OnEndScene();
}