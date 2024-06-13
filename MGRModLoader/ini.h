#pragma once
#include <stdio.h>
#include <stdlib.h>
#include <Windows.h>
#include <string>

class IniReader
{
private:
    std::string filePath;
public:

    IniReader()
    {
        filePath.clear();
    }

    IniReader(const char* fileName) : filePath(fileName)
    {

    }

    ~IniReader()
    {
        filePath.clear();
    }

    void SetPath(const char* path)
    {
        filePath = path;
    }

    int ReadInt(const char* section, const char* key, int iDefaultValue)
    {
        return GetPrivateProfileIntA(section, key, iDefaultValue, filePath.c_str());
    }

    void WriteInt(const char* section, const char* key, int value)
    {
        char iBuff[32];

        sprintf(iBuff, "%d", value);
        WritePrivateProfileStringA(section, key, iBuff, filePath.c_str());
    }

    float ReadFloat(const char* section, const char* key, float flDefValue)
    {
        char flRes[32];
        char flDef[32];

        sprintf(flDef, "%f", flDefValue);
        GetPrivateProfileStringA(section, key, flDef, flRes, sizeof(flRes), filePath.c_str());

        return atof(flRes);
    }

    void WriteFloat(const char* section, const char* key, float flValue)
    {
        char flBuff[32];

        sprintf(flBuff, "%f", flValue);
        WritePrivateProfileStringA(section, key, flBuff, filePath.c_str());
    }

    const char* ReadString(const char* section, const char* key, const char* szDefaultValue)
    {
        char buff[512];

        GetPrivateProfileStringA(section, key, szDefaultValue, buff, sizeof(buff), filePath.c_str());

        return buff;
    }

    void WriteString(const char* section, const char* key, const char* szValue)
    {
        WritePrivateProfileStringA(section, key, szValue, filePath.c_str());
    }

    bool ReadBool(const char* section, const char* key, bool bDefaultBool)
    {
        char buff[8];
        char resBuff[8];

        sprintf(buff, "%s", bDefaultBool ? "true" : "false");

        GetPrivateProfileStringA(section, key, buff, resBuff, sizeof(resBuff), filePath.c_str());

        if (!strcmp(resBuff, "true"))
            return true;
        else if (!strcmp(resBuff, "false"))
            return false;

        return bDefaultBool;
    }

    void WriteBool(const char* section, const char* key, bool bValue)
    {
        char buff[8];

        sprintf(buff, "%s", bValue ? "true" : "false");

        WritePrivateProfileStringA(section, key, buff, filePath.c_str());
    }
};