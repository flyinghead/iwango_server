#pragma once
#include <vector>
#include <string>
#include <sstream>
#include <iostream>
#include <asio.hpp>

#if ASIO_VERSION < 102900
namespace asio::placeholders
{
static inline constexpr auto& error = std::placeholders::_1;
static inline constexpr auto& bytes_transferred = std::placeholders::_2;
}
#endif

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
