#pragma once
#include <vector>
#include <string>
#include <sstream>
#include <iostream>
#include <unicode/unistr.h>

std::string getConfig(const std::string& name, const std::string& default_value);

enum class GameId
{
	Daytona,
	DaytonaJP,
	Tetris,
	GolfShiyouyo,
	AeroDancing,
	HundredSwords,
	CuldCept,
};

inline static GameId identifyGame(const std::string& gameId)
{
	if (gameId == "S00001S0001010440110")
		return GameId::DaytonaJP;
	if (gameId == "F00001S0000810380101")
		return GameId::Tetris;
	if (gameId == "T00009T0000910430101")
		return GameId::GolfShiyouyo;
	if (gameId == "F00005T0000510410101")
		return GameId::AeroDancing;
	if (gameId == "F00001S0000110490101")
		return GameId::HundredSwords;
	if (gameId == "T00011T0001110500101")
		return GameId::CuldCept;
	return GameId::Daytona;
}

inline static std::vector<std::string> splitString(const std::string& s, char c)
{
	std::vector<std::string> strings;
	std::istringstream f(s);
	std::string token;
	while (std::getline(f, token, c))
		strings.push_back(token);
	// behave like c# string.Split()
	if (s[s.length() - 1] == c || s.empty())
		strings.push_back("");
	return strings;
}

inline static std::string utf8ToSjis(const std::string& value, bool fullWidth)
{
    icu::UnicodeString src(value.c_str(), "utf8");
    int32_t srclen = src.length();
    if (fullWidth)
    {
    	// convert ascii to full-width
		for (int i = 0; i < srclen; i++)
			if (src[i] > ' ' && src[i] <= '~')
				src.setCharAt(i, (char16_t)(src[i] - 0x20 + 0xFF00));
    }
    int length = src.extract(0, srclen, nullptr, "shift_jis");

    std::vector<char> result(length + 1);
    src.extract(0, srclen, &result[0], "shift_jis");

    return std::string(result.begin(), result.end() - 1);
}

inline static std::string sjisToUtf8(const std::string& value)
{
    icu::UnicodeString src(value.c_str(), "shift_jis");
	// convert full-width to ascii
    int32_t srclen = src.length();
    for (int i = 0; i < srclen; i++)
    	if (src[i] > 0xFF00 && src[i] <= 0xFF5E)
    		src.setCharAt(i, (char16_t)(src[i] - 0xFF00 + 0x20));
    int length = src.extract(0, srclen, nullptr, "utf8");

    std::vector<char> result(length + 1);
    src.extract(0, srclen, &result[0], "utf8");

    return std::string(result.begin(), result.end() - 1);
}
