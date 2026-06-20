#include "imgui/imconfig.h"
#include "imgui/imgui.h"

#include "Utils.h"

bool ImGui::InputText(const char* label, Utils::String& buf, unsigned int flags)
{
	if (buf.empty())
		buf = "";

	bool result = ImGui::InputText(label, buf.data(), buf.capacity() + 1, flags | ImGuiInputTextFlags_CallbackResize, [](ImGuiInputTextCallbackData* data) -> int
		{
			if (data->EventFlag != ImGuiInputTextFlags_CallbackResize)
				return 0;

			Utils::String* str = (Utils::String*)data->UserData;

			str->reserve(data->BufSize);
			data->Buf = str->data();
			data->BufSize = (int)str->capacity() + 1;

			return 0;
		}, &buf);

	return result;
}

bool ImGui::InputTextMultiline(const char* label, Utils::String& buf, unsigned int flags)
{
	if (buf.empty())
		buf = "";

	bool result = ImGui::InputTextMultiline(label, buf.data(), buf.capacity() + 1, ImVec2(0, 0), flags | ImGuiInputTextFlags_CallbackResize, [](ImGuiInputTextCallbackData* data) -> int
		{
			if (data->EventFlag != ImGuiInputTextFlags_CallbackResize)
				return 0;

			Utils::String* str = (Utils::String*)data->UserData;

			str->reserve(data->BufSize);
			data->Buf = str->data();
			data->BufSize = (int)str->capacity() + 1;

			return 0;
		}, &buf);

	return result;
}