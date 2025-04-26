#include "Hotkey.h"

#include "imgui/imgui.h"
#include <shared.h>
#include <ini.h>
#include "Utils.h"

static const unsigned char keys[] =
{
        'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L',
        'M', 'N', 'O', 'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X',
        'Y', 'Z',
        '0', '1', '2', '3', '4', '5', '6', '7', '8', '9',
        VK_CAPITAL, VK_NUMLOCK, VK_SCROLL,
        VK_F1, VK_F2, VK_F3, VK_F4, VK_F5, VK_F6, VK_F7, VK_F8, VK_F9, VK_F10,
        VK_F11, VK_F12,
        VK_RETURN, VK_ESCAPE, VK_SPACE, VK_TAB, VK_BACK, VK_DELETE, VK_INSERT,
        VK_HOME, VK_END, VK_PRIOR, VK_NEXT,
        VK_UP, VK_DOWN, VK_LEFT, VK_RIGHT,
        VK_NUMPAD0, VK_NUMPAD1, VK_NUMPAD2, VK_NUMPAD3, VK_NUMPAD4, VK_NUMPAD5,
        VK_NUMPAD6, VK_NUMPAD7, VK_NUMPAD8, VK_NUMPAD9, VK_MULTIPLY, VK_ADD,
        VK_SEPARATOR, VK_SUBTRACT, VK_DECIMAL, VK_DIVIDE,
        VK_LBUTTON, VK_RBUTTON, VK_MBUTTON, VK_XBUTTON1, VK_XBUTTON2,
        VK_LWIN, VK_RWIN, VK_APPS, VK_SLEEP,
        VK_MEDIA_PLAY_PAUSE, VK_MEDIA_STOP, VK_MEDIA_NEXT_TRACK, VK_MEDIA_PREV_TRACK,
        VK_VOLUME_MUTE, VK_VOLUME_DOWN, VK_VOLUME_UP,
        VK_OEM_1, VK_OEM_PLUS, VK_OEM_COMMA, VK_OEM_MINUS, VK_OEM_PERIOD, VK_OEM_2,
        VK_OEM_3, VK_OEM_4, VK_OEM_5, VK_OEM_6, VK_OEM_7, VK_OEM_8, VK_OEM_102,
        VK_PROCESSKEY, VK_ATTN, VK_CRSEL, VK_EXSEL, VK_EREOF, VK_PLAY, VK_ZOOM,
        VK_NONAME, VK_PA1, VK_OEM_CLEAR
};

static const unsigned char ModifierKeys[] =
{
	VK_LSHIFT, VK_RSHIFT, VK_LCONTROL, VK_RCONTROL, VK_LMENU, VK_RMENU,
	VK_LWIN, VK_RWIN
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

    m_iKey = ini.ReadInteger("Hotkeys", m_szName, m_iDefault);
	m_iModifierKey = ini.ReadInteger("Hotkeys", Utils::format("%s.Modifier", m_szName), m_iModifierKey);
    m_eType = (eHotkeyType)ini.ReadInteger("Hotkeys", Utils::format("%s.Type", m_szName), m_eType);
}

void Hotkey::Save()
{
	IniReader ini("MGRModLoaderSettings.ini");

    ini.WriteInteger("Hotkeys", m_szName, m_iKey);
    ini.WriteInteger("Hotkeys", Utils::format("%s.Modifier", m_szName), m_iModifierKey);
    ini.WriteInteger("Hotkeys", Utils::format("%s.Type", m_szName), m_eType);
}

void Hotkey::Reset()
{
    m_iKey = m_iDefault;
    m_iModifierKey = 0;
	m_eType = HT_TOGGLE;
}

void Hotkey::Rebind()
{
    bool bModifierKeyPressed = false;
	for (const unsigned char& key : ModifierKeys)
	{
		if (shared::IsKeyPressed(key, true))
		{
			bModifierKeyPressed = true;
            m_iModifierKey = key;
			break;
		}
	}

	for (const unsigned char& key : keys)
	{
        bool keyPress = shared::IsKeyPressed(key, false);

		if (key == VK_LBUTTON || key == VK_RBUTTON || key == VK_MBUTTON)
			continue;

        if (key == VK_ESCAPE && keyPress)
        {
            m_bRebind = false;
            m_iKey = 0;
            break;
        }

        if (keyPress)
        {
            m_iKey = key;
            m_bRebind = false;
            break;
        }
	}
}

void Hotkey::Update()
{
    if (m_bRebind)
    {
        Rebind();
        return;
    };
    if (m_eType == HT_ALWAYS)
    {
        m_bToggle = true;
        if (m_callback)
            m_callback(this);
        return;
    }
    else if (m_eType == HT_OFF)
    {
        m_bToggle = false;
        return;
    }

    switch (m_eType & 0x7F)
    {
    case HT_TOGGLE:
        if (IsKeyPressed())
        {
            m_bToggle = !m_bToggle;
            if (m_callback)
                m_callback(this);
        }
        break;
    case HT_HOLD:
		m_bToggle = IsKeyPressed();
        if (m_callback)
			m_callback(this);
        break;
    default:
        break;
    }
}

bool Hotkey::IsKeyPressed()
{
    if (m_bRebind)
        return false;

    if (m_iModifierKey)
        return shared::IsKeyPressed(m_iModifierKey, true) && shared::IsKeyPressed(m_iKey, m_eType == HT_HOLD);

    return shared::IsKeyPressed(m_iKey, m_eType == HT_HOLD);
}

static int restoreModifierValue = 0;

void Hotkey::Draw(const char* label)
{
    auto GetKeyName = [](int key) -> Utils::String
        {
            if (!key)
                return "None";

            Utils::String name = "";
            name.resize(128);

            UINT scanCode = MapVirtualKey(key, MAPVK_VK_TO_VSC);
            switch (key)
            {
            case VK_LEFT: case VK_UP: case VK_RIGHT: case VK_DOWN:
            case VK_RCONTROL: case VK_RMENU:
            case VK_LWIN: case VK_RWIN: case VK_APPS:
            case VK_PRIOR: case VK_NEXT:
            case VK_END: case VK_HOME:
            case VK_INSERT: case VK_DELETE:
            case VK_DIVIDE:
            case VK_NUMLOCK:
                scanCode |= KF_EXTENDED;
                break;
            default:
                break;
            }
            GetKeyNameText(scanCode << 16, (LPSTR)name.data(), 128);
            name.resize();
            return name;
        };


	ImVec2 buttonSize = ImVec2(100, 20);

    ImGui::PushID(label);
    ImGui::TextUnformatted(label);
    ImGui::SameLine();
    if (m_bRebind && ImGui::Button(m_iModifierKey ? (GetKeyName(m_iModifierKey) + " + ...").c_str() : "...", buttonSize))
    {
        m_bRebind = false;
        m_iModifierKey = restoreModifierValue;
    }
    else if (!m_bRebind)
    {
        Utils::String keyName = "";
        if (m_iModifierKey)
		{
			keyName += GetKeyName(m_iModifierKey);
			keyName += " + ";
		}
        keyName += GetKeyName(m_iKey);
        if (ImGui::Button(keyName.c_str(), buttonSize))
        {
            restoreModifierValue = m_iModifierKey;
            m_iModifierKey = 0;
            m_bRebind = true;
        }
    }

    if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Right, false))
        ImGui::OpenPopup("HotkeyExSettings");

	if (ImGui::BeginPopup("HotkeyExSettings"))
	{
        if (!(m_eType & 0x80))
        {
            if (ImGui::RadioButton("Toggle", m_eType == HT_TOGGLE))
                m_eType = HT_TOGGLE;
            if (ImGui::RadioButton("Hold", m_eType == HT_HOLD))
                m_eType = HT_HOLD;
            if (ImGui::RadioButton("Always", m_eType == HT_ALWAYS))
                m_eType = HT_ALWAYS;
            if (ImGui::RadioButton("Off", m_eType == HT_OFF))
                m_eType = HT_OFF;
        }
        else
        {
            ImGui::Text("Not allowed to change hotkey type.");
        }
		ImGui::EndPopup();
	}

    ImGui::PopID();
}