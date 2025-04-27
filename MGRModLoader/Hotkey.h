#pragma once

class Hotkey
{
public:
	bool m_bToggle;

	enum eHotkeyType : unsigned char
	{
		HT_TOGGLE = 0,
		HT_HOLD,
		HT_ALWAYS,
		HT_OFF,

		HT_NO_CHANGE_TYPE = 0x80
	};
private:
	bool m_bRebind;
	eHotkeyType m_eType;

	const char* m_szName;
	int m_iDefault;

	void(__fastcall* m_callback)(Hotkey*);
public:
	int m_iKey;
	int m_iModifierKey;

	Hotkey();
	Hotkey(int key, int modifierKey, const char *szName, void(__fastcall *cb)(Hotkey *) = nullptr, eHotkeyType type = HT_TOGGLE);

	inline eHotkeyType GetHotkeyType() const { return (eHotkeyType)(m_eType & 0x7F); }

	void Load();
	void Save();
	void Reset();
	void Rebind();
	void Update();
	bool IsKeyPressed();
	void Draw(const char *label);
};