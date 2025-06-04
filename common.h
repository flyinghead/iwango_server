#pragma once
#include <vector>
#include <string>
#include <sstream>
#include <iostream>
#include <unicode/unistr.h>

std::string getConfig(const std::string& name, const std::string& default_value);

enum class GameId
{
	Unknown = -1,
	Daytona,
	DaytonaJP,
	Tetris,
	GolfShiyouyo,
	AeroDancingI,
	HundredSwords,
	CuldCept,
	AeroDancingF,
};

inline static GameId identifyGame(const std::string& gameId)
{
	if (gameId == "S00001S0001010440110")
		return GameId::DaytonaJP;
	if (gameId == "F00001S0000810380101")
		return GameId::Tetris;
	if (gameId == "T00009T0000910430101")
		return GameId::GolfShiyouyo;
	if (gameId == "F00005T0000510410101"
		// Aero Dancing i - Jikai Saku Made Matemasen
		|| gameId == "F00005T0000510700101")
		return GameId::AeroDancingI;
	// Aero Dancing F - Todoroki Tsubasa no Hatsu Hikou
	if (gameId == "F00005T0000510420101")
		return GameId::AeroDancingF;
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

namespace Log {
enum LEVEL
{
	ERROR = 0,
	WARNING = 1,
	NOTICE = 2,
	INFO = 3,
	DEBUG = 4,
};
}

void logger(Log::LEVEL level, GameId gameId, const char *file, int line, const char *format, ...);

#define ERROR_LOG(gameId, ...)                      \
	do {                                    \
		logger(Log::ERROR, gameId, __FILE__, __LINE__, __VA_ARGS__);    \
	} while (0)

#define WARN_LOG(gameId, ...)                       \
	do {                                    \
		logger(Log::WARNING, gameId, __FILE__, __LINE__, __VA_ARGS__);    \
	} while (0)

#define NOTICE_LOG(gameId, ...)                     \
	do {                                    \
		logger(Log::NOTICE, gameId, __FILE__, __LINE__, __VA_ARGS__);    \
	} while (0)

#define INFO_LOG(gameId, ...)                       \
	do {                                    \
		logger(Log::INFO, gameId, __FILE__, __LINE__, __VA_ARGS__);    \
	} while (0)

#ifndef NDEBUG
#define DEBUG_LOG(gameId, ...)                      \
	do {                                    \
		logger(Log::DEBUG, gameId, __FILE__, __LINE__, __VA_ARGS__);    \
	} while (0)
#else
#define DEBUG_LOG(...)
#endif
