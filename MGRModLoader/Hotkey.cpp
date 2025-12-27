#include "Hotkey.h"

#include "imgui/imgui.h"
#include <shared.h>
#include <ini.h>
#include "Utils.h"
#include <Hw.h>
#include <map>

extern Hw::cKeyboardState g_KeyboardState;

static const unsigned char keys[] =
{
	// Letters
	Hw::KB_A, Hw::KB_B, Hw::KB_C, Hw::KB_D, Hw::KB_E, Hw::KB_F, Hw::KB_G, Hw::KB_H,
	Hw::KB_I, Hw::KB_J, Hw::KB_K, Hw::KB_L, Hw::KB_M, Hw::KB_N, Hw::KB_O, Hw::KB_P,
	Hw::KB_Q, Hw::KB_R, Hw::KB_S, Hw::KB_T, Hw::KB_U, Hw::KB_V, Hw::KB_W, Hw::KB_X,
	Hw::KB_Y, Hw::KB_Z,

	// Numbers
	Hw::KB_0, Hw::KB_1, Hw::KB_2, Hw::KB_3, Hw::KB_4, Hw::KB_5, Hw::KB_6, Hw::KB_7,
	Hw::KB_8, Hw::KB_9,

	// State Keys
	Hw::KB_CAP, Hw::KB_NUMLOCK, Hw::KB_SCRLOCK,

	// Function Keys
	Hw::KB_F1, Hw::KB_F2, Hw::KB_F3, Hw::KB_F4, Hw::KB_F5, Hw::KB_F6, Hw::KB_F7,
	Hw::KB_F8, Hw::KB_F9, Hw::KB_F10, Hw::KB_F11, Hw::KB_F12,

	// Control Keys
	Hw::KB_RET, Hw::KB_ESC, Hw::KB_SPACE, Hw::KB_TAB, Hw::KB_BS, Hw::KB_DEL, Hw::KB_INS,
	Hw::KB_HOME, Hw::KB_END, Hw::KB_PAGE_UP, Hw::KB_PAGE_DN,

	// Directional
	Hw::KB_UP, Hw::KB_DN, Hw::KB_LT, Hw::KB_RT,

	// Numpad
	Hw::KB_NUM0, Hw::KB_NUM1, Hw::KB_NUM2, Hw::KB_NUM3, Hw::KB_NUM4, Hw::KB_NUM5,
	Hw::KB_NUM6, Hw::KB_NUM7, Hw::KB_NUM8, Hw::KB_NUM9, Hw::KB_NUM_MUL, Hw::KB_NUM_ADD,
	Hw::KB_COMMA, Hw::KB_NUM_SUB, Hw::KB_NUM_DEC, Hw::KB_NUM_DIV,

	// System/Other (Mapping best fit from your enum)
	Hw::KB_MAP_INVALID, Hw::KB_MAP_INVALID, Hw::KB_MAP_INVALID, Hw::KB_MAP_INVALID, Hw::KB_MAP_INVALID, // Mouse buttons (Not in enum)
	Hw::KB_WIN_L, Hw::KB_WIN_R, Hw::KB_APPS, Hw::KB_MAP_INVALID, // Sleep (Not in enum)

	// Media Keys (Not in enum, mapped to Invalid)
	Hw::KB_MAP_INVALID, Hw::KB_MAP_INVALID, Hw::KB_MAP_INVALID, Hw::KB_MAP_INVALID,
	Hw::KB_MAP_INVALID, Hw::KB_MAP_INVALID, Hw::KB_MAP_INVALID,

	// OEM / Symbols
	Hw::KB_SEMICOLON, Hw::KB_EQ, Hw::KB_COMMA, Hw::KB_MINUS, Hw::KB_PERIOD, Hw::KB_SLASH,
	Hw::KB_GRAVE, Hw::KB_BRAC_L, Hw::KB_BACKSLASH, Hw::KB_BRAC_R, Hw::KB_APOS, Hw::KB_MAP_INVALID, Hw::KB_YEN,

	// Remaining UI Keys (Not in enum)
	Hw::KB_MAP_INVALID, Hw::KB_SYSRQ, Hw::KB_MAP_INVALID, Hw::KB_MAP_INVALID, Hw::KB_MAP_INVALID,
	Hw::KB_PAUSE, Hw::KB_MAP_INVALID, Hw::KB_MAP_INVALID, Hw::KB_MAP_INVALID, Hw::KB_MAP_INVALID
};

static const unsigned char ModifierKeys[] =
{
	Hw::KB_SHIFT_L, Hw::KB_SHIFT_R,
	Hw::KB_CTRL_L, Hw::KB_CTRL_R,
	Hw::KB_ALT_L, Hw::KB_ALT_R,
	Hw::KB_WIN_L, Hw::KB_WIN_R
};

static const std::map<Hw::KEYBOARD_MAP, const char*> KeyboardNames = {
	{Hw::KB_SPACE, "Space"},
	{Hw::KB_A, "A"}, {Hw::KB_B, "B"}, {Hw::KB_C, "C"}, {Hw::KB_D, "D"},
	{Hw::KB_E, "E"}, {Hw::KB_F, "F"}, {Hw::KB_G, "G"}, {Hw::KB_H, "H"},
	{Hw::KB_I, "I"}, {Hw::KB_J, "J"}, {Hw::KB_K, "K"}, {Hw::KB_L, "L"},
	{Hw::KB_M, "M"}, {Hw::KB_N, "N"}, {Hw::KB_O, "O"}, {Hw::KB_P, "P"},
	{Hw::KB_Q, "Q"}, {Hw::KB_R, "R"}, {Hw::KB_S, "S"}, {Hw::KB_T, "T"},
	{Hw::KB_U, "U"}, {Hw::KB_V, "V"}, {Hw::KB_W, "W"}, {Hw::KB_X, "X"},
	{Hw::KB_Y, "Y"}, {Hw::KB_Z, "Z"},

	{Hw::KB_0, "0"}, {Hw::KB_1, "1"}, {Hw::KB_2, "2"}, {Hw::KB_3, "3"},
	{Hw::KB_4, "4"}, {Hw::KB_5, "5"}, {Hw::KB_6, "6"}, {Hw::KB_7, "7"},
	{Hw::KB_8, "8"}, {Hw::KB_9, "9"},

	{Hw::KB_MINUS, "Minus"}, {Hw::KB_EQ, "Equals"},
	{Hw::KB_BRAC_L, "Left Bracket"}, {Hw::KB_BRAC_R, "Right Bracket"},
	{Hw::KB_PERIOD, "Period"}, {Hw::KB_APOS, "Apostrophe"},
	{Hw::KB_SLASH, "Slash"}, {Hw::KB_COMMA, "Comma"},
	{Hw::KB_SEMICOLON, "Semicolon"}, {Hw::KB_GRAVE, "Grave"},
	{Hw::KB_COLON, "Colon"}, {Hw::KB_AT, "At Symbol"},
	{Hw::KB_YEN, "Yen"}, {Hw::KB_CIRCUMFLEX, "Circumflex"},
	{Hw::KB_BACKSLASH, "Backslash"},

	{Hw::KB_RET, "Enter"}, {Hw::KB_TAB, "Tab"}, {Hw::KB_BS, "Backspace"},
	{Hw::KB_ESC, "Escape"}, {Hw::KB_INS, "Insert"}, {Hw::KB_DEL, "Delete"},
	{Hw::KB_HOME, "Home"}, {Hw::KB_END, "End"},
	{Hw::KB_PAGE_UP, "Page Up"}, {Hw::KB_PAGE_DN, "Page Down"},
	{Hw::KB_UP, "Up"}, {Hw::KB_DN, "Down"}, {Hw::KB_LT, "Left"}, {Hw::KB_RT, "Right"},

	{Hw::KB_F1, "F1"}, {Hw::KB_F2, "F2"}, {Hw::KB_F3, "F3"}, {Hw::KB_F4, "F4"},
	{Hw::KB_F5, "F5"}, {Hw::KB_F6, "F6"}, {Hw::KB_F7, "F7"}, {Hw::KB_F8, "F8"},
	{Hw::KB_F9, "F9"}, {Hw::KB_F10, "F10"}, {Hw::KB_F11, "F11"}, {Hw::KB_F12, "F12"},

	{Hw::KB_CAP, "Caps Lock"}, {Hw::KB_SYSRQ, "Print Screen"},
	{Hw::KB_SCRLOCK, "Scroll Lock"}, {Hw::KB_PAUSE, "Pause"},
	{Hw::KB_NUMLOCK, "Num Lock"},

	{Hw::KB_CTRL_L, "Left Control"}, {Hw::KB_CTRL_R, "Right Control"},
	{Hw::KB_ALT_L, "Left Alt"}, {Hw::KB_ALT_R, "Right Alt"},
	{Hw::KB_SHIFT_L, "Left Shift"}, {Hw::KB_SHIFT_R, "Right Shift"},
	{Hw::KB_WIN_L, "Left Windows"}, {Hw::KB_WIN_R, "Right Windows"},
	{Hw::KB_APPS, "Apps"},

	{Hw::KB_NUM0, "Numpad 0"}, {Hw::KB_NUM1, "Numpad 1"}, {Hw::KB_NUM2, "Numpad 2"},
	{Hw::KB_NUM3, "Numpad 3"}, {Hw::KB_NUM4, "Numpad 4"}, {Hw::KB_NUM5, "Numpad 5"},
	{Hw::KB_NUM6, "Numpad 6"}, {Hw::KB_NUM7, "Numpad 7"}, {Hw::KB_NUM8, "Numpad 8"},
	{Hw::KB_NUM9, "Numpad 9"},
	{Hw::KB_NUM_ADD, "Numpad Add"}, {Hw::KB_NUM_SUB, "Numpad Subtract"},
	{Hw::KB_NUM_DEC, "Numpad Decimal"}, {Hw::KB_NUM_DIV, "Numpad Divide"},
	{Hw::KB_NUM_MUL, "Numpad Multiply"}, {Hw::KB_NUM_ENT, "Numpad Enter"}
};

Hotkey::Hotkey()
{
	m_iKey = 0;
	m_bRebind = false;
	m_eType = HT_TOGGLE;

	m_szName = nullptr;
	m_iDefault = 0;
}

Hotkey::Hotkey(int key, int modifierKey, const char *szName, void(__fastcall *cb)(Hotkey *), eHotkeyType type)
{
	m_iKey = key;
	m_bRebind = false;
	m_eType = type;

	m_iModifierKey = modifierKey;

	m_callback = cb;

	m_szName = szName;
	m_iDefault = key;
}

void Hotkey::Load()
{
	IniReader ini("MGRModLoaderSettings.ini");

	bool bStrictType = IsReadOnlyType();

	m_iKey = ini.ReadInteger("Hotkeys", m_szName, m_iDefault);
	m_iModifierKey = ini.ReadInteger("Hotkeys", Utils::format("%s.Modifier", m_szName).c_str(), m_iModifierKey);
	if (bStrictType)
		m_eType = (eHotkeyType)(ini.ReadInteger("Hotkeys", Utils::format("%s.Type", m_szName).c_str(), m_eType) | 0x80);
	else
		m_eType = (eHotkeyType)ini.ReadInteger("Hotkeys", Utils::format("%s.Type", m_szName).c_str(), m_eType);
}

void Hotkey::Save()
{
	IniReader ini("MGRModLoaderSettings.ini");

	ini.WriteInteger("Hotkeys", m_szName, m_iKey);
	ini.WriteInteger("Hotkeys", Utils::format("%s.Modifier", m_szName).c_str(), m_iModifierKey);
	ini.WriteInteger("Hotkeys", Utils::format("%s.Type", m_szName).c_str(), m_eType & 0x7F);
}

void Hotkey::Reset()
{
	m_iKey = m_iDefault;
	m_iModifierKey = 0;
	m_eType = HT_TOGGLE;
}

void Hotkey::Update()
{
	if (!m_bRebind)
	{
		switch (GetHotkeyType())
		{
		case HT_OFF:
			m_bToggle = false;
			break;
		case HT_ALWAYS:
			m_bToggle = true;
			if (m_callback)
				m_callback(this);
			break;
		case HT_HOLD:
			m_bToggle = IsKeyPressed();
			if (m_callback)
				m_callback(this);
			break;
		case HT_TOGGLE:
			if (IsKeyPressed())
			{
				m_bToggle ^= true;
				if (m_callback)
					m_callback(this);
			}
			break;
		default:
			break;
		}
	}
	else
	{
		if (g_KeyboardState.trig(Hw::KB_ESC))
		{
			m_bRebind = false;
			return;
		}

		bool bWasModifierPressed = false;

		for (const unsigned char& key : ModifierKeys)
		{
			if (g_KeyboardState.on(key))
			{
				m_iModifierKey = key;
				bWasModifierPressed = true;

				break;
			}
		}

		for (const unsigned char& key : keys)
		{
			if (g_KeyboardState.trig(key))
			{
				if (!bWasModifierPressed)
					m_iModifierKey = 0;

				m_iKey = key;
				m_bRebind = false;

				break;
			}
		}
	}
}

bool Hotkey::IsKeyPressed()
{
	if (GetHotkeyType() == HT_ALWAYS)
		return true;
	else if (GetHotkeyType() == HT_OFF)
		return false;

	if (m_bRebind)
		return false;

	if (ImGui::GetIO().WantCaptureKeyboard)
		return false;

	if (m_iModifierKey)
	{
		bool result = false;
		if (g_KeyboardState.on((Hw::KEYBOARD_MAP)m_iModifierKey))
		{
			if (GetHotkeyType() == HT_HOLD)
				result = g_KeyboardState.on((Hw::KEYBOARD_MAP)m_iKey);
			else if (GetHotkeyType() == HT_TOGGLE)
				result = g_KeyboardState.trig((Hw::KEYBOARD_MAP)m_iKey);
		}
		return result; // Written like that to avoid condition fuck ups
	}

	if (GetHotkeyType() == HT_TOGGLE)
		return g_KeyboardState.trig((Hw::KEYBOARD_MAP)m_iKey);

	return g_KeyboardState.on((Hw::KEYBOARD_MAP)m_iKey);
}

static int restoreModifierValue = 0;

void Hotkey::Draw(const char* label)
{
	auto GetKeyName = [](int key) -> Utils::String
		{
			if (!key)
				return "None";

			auto it = KeyboardNames.find((Hw::KEYBOARD_MAP)key);
			if (it != KeyboardNames.end())
				return it->second;
			else
				return Utils::format("Unknown(0x%02X)", key);

			return "Invalid"; // Should not reach here
		};


	ImGui::PushID(this);  // using `this` as unique ID

	ImGui::TextUnformatted(label);
	ImGui::SameLine();
	const ImVec2 buttonSize = { 0.f, 20.0f };
	if (m_bRebind)
	{
		Utils::String keyName;
		if (m_iModifierKey && g_KeyboardState.on((Hw::KEYBOARD_MAP)m_iModifierKey))
			(void)keyName.format("%s + ...", GetKeyName(m_iModifierKey).c_str());
		else
			keyName = "...";

		if (ImGui::Button(keyName.c_str(), buttonSize))
			m_bRebind = false;
	}
	if (m_iModifierKey)
	{
		if (!m_bRebind && ImGui::Button(Utils::format("%s + %s", GetKeyName(m_iModifierKey).c_str(), GetKeyName(m_iKey).c_str()).c_str(), buttonSize))
			m_bRebind = true;
	}
	else
	{
		if (!m_bRebind && ImGui::Button(GetKeyName(m_iKey).c_str(), buttonSize))
			m_bRebind = true;
	}

	if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
		ImGui::OpenPopup("##ADV_OPT");

	if (ImGui::BeginPopup("##ADV_OPT"))
	{
		if (IsReadOnlyType())
		{
			ImGui::Text("The type of hotkey cannot be changed.");
		}
		else
		{
			if (ImGui::RadioButton("Off", GetHotkeyType() == HT_OFF))
				m_eType = HT_OFF;
			if (ImGui::RadioButton("Repeat", GetHotkeyType() == HT_HOLD))
				m_eType = HT_HOLD;
			if (ImGui::RadioButton("Toggle", GetHotkeyType() == HT_TOGGLE))
				m_eType = HT_TOGGLE;
			if (ImGui::RadioButton("Always ON", GetHotkeyType() == HT_ALWAYS))
				m_eType = HT_ALWAYS;
		}
		if (ImGui::Button("Close"))
			ImGui::CloseCurrentPopup();
		ImGui::EndPopup();
	}

	ImGui::PopID();
}