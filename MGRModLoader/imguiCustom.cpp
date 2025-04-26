#include "imgui/imconfig.h"
#include "imgui/imgui.h"

#include "Utils.h"

#pragma warning(push)

#pragma warning(disable : 4996)

bool ImGui::InputText(const char* label, Utils::String& buf, unsigned int flags)
{
	static char buffer[1024];

	buffer[0] = '\0';

	if (buf.data())
		strcpy(buffer, buf.c_str());
	bool result = ImGui::InputText(label, buffer, sizeof(buffer), flags);
	if (result)
		buf.resize(strlen(buffer) + 1);

	if (buf.data() && result)
	{
		strcpy(buf.data(), buffer);
		buf.resize();
	}

	return result;
}

bool ImGui::InputTextMultiline(const char* label, Utils::String& buf, unsigned int flags)
{
	static char buffer[1024];

	buffer[0] = '\0';

	if (buf.data())
		strcpy(buffer, buf.c_str());
	bool result = ImGui::InputTextMultiline(label, buffer, sizeof(buffer));
	if (result)
		buf.resize(strlen(buffer) + 1);

	if (buf.data() && result)
	{
		strcpy(buf.data(), buffer);
		buf.resize();
	}

	return result;
}

#pragma warning(pop)