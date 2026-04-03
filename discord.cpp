/*
    Copyright (C) 2025  Flyinghead

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/
#include "discord.h"
#include <dcserver/discord.hpp>
#include <chrono>

const char *getDCNetGameId(GameId gameId)
{
	static const char *gameIds[] {
		"daytona",
		"daytona",
		"segatetris",
		"golfshiyou2",
		"aeroi",
		"hundredswords",
		"culdcept",
		"aerof",
		"powersmash",
		"yakyuunet",
		"runejade",
	};
	if (gameId == GameId::Unknown || (size_t)gameId >= std::size(gameIds))
		return nullptr;
	else
		return gameIds[(int)gameId];
}

void discordLobbyJoined(GameId gameId, const std::string& username, const std::string& lobbyName, const std::vector<std::string>& playerList)
{
	using the_clock = std::chrono::steady_clock;
	static the_clock::time_point last_notif;
	the_clock::time_point now = the_clock::now();
	if (last_notif != the_clock::time_point() && now - last_notif < std::chrono::minutes(5))
		return;
	last_notif = now;
	Notif notif;
	notif.content = "Player **" + discordEscape(username) + "** joined lobby **" + discordEscape(lobbyName) + "**";
	notif.embed.title = "Lobby Players";
	for (const auto& player : playerList)
		notif.embed.text += discordEscape(player) + "\n";
	discordNotif(getDCNetGameId(gameId), notif);
}

void discordGameCreated(GameId gameId, const std::string& username, const std::string& gameName, const std::vector<std::string>& playerList)
{
	Notif notif;
	notif.content = "Player **" + discordEscape(username) + "** created team **" + discordEscape(gameName) + "**";
	notif.embed.title = "Lobby Players";
	for (const auto& player : playerList)
		notif.embed.text += discordEscape(player) + "\n";

	discordNotif(getDCNetGameId(gameId), notif);
}
