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
#include <curl/curl.h>
#include "json.hpp"
#include <thread>
#include <atomic>
#include <chrono>

static std::string DiscordWebhook;
static std::atomic_int threadCount;

using namespace nlohmann;

struct {
	const char *name;
	const char *url;
} Games[] = {
	{ "Daytona USA",				"https://dcnet.flyca.st/gamepic/daytona.jpg" },
	{ "Daytona USA",				"https://dcnet.flyca.st/gamepic/daytona.jpg" },
	{ "Sega Tetris",				"https://dcnet.flyca.st/gamepic/segatetris.jpg" },
	{ "Golf Shiyou Yo 2",			"https://dcnet.flyca.st/gamepic/golfshiyou2.jpg" },
	{ "Aero Dancing i",				"https://dcnet.flyca.st/gamepic/aerodancing.jpg" },
	{ "Hundred Swords",				"https://dcnet.flyca.st/gamepic/hundredswords.jpg" },
	{ "Culdcept",					"" },
	{ "Aero Dancing F",				"https://dcnet.flyca.st/gamepic/aerodancing-fsd.jpg" },
};

class Notif
{
public:
	Notif(GameId gameId) : gameId(gameId) {}

	std::string to_json() const
	{
		json embeds;
		embeds.push_back({
			{ "author",
				{
					{ "name", Games[(int)gameId].name },
					{ "icon_url", Games[(int)gameId].url }
				},
			},
			{ "title", embed.title },
			{ "description", embed.text },
			{ "color", 9118205 },
		});

		json j = {
			{ "content", content },
			{ "embeds", embeds },
		};
		return j.dump(4);
	}

	GameId gameId;
	std::string content;
	struct {
		std::string title;
		std::string text;
	} embed;
};

static void postWebhook(Notif notif)
{
	CURL *curl = curl_easy_init();
	if (curl == nullptr)
	{
		ERROR_LOG(notif.gameId, "Can't create curl handle");
		threadCount.fetch_sub(1);
		return;
	}
	CURLcode res;
	curl_easy_setopt(curl, CURLOPT_URL, DiscordWebhook.c_str());
	curl_easy_setopt(curl, CURLOPT_USERAGENT, "DCNet-DiscordWebhook");
	curl_slist *headers = curl_slist_append(nullptr, "Content-Type: application/json");
	curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

	std::string json = notif.to_json();
	curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json.c_str());

	res = curl_easy_perform(curl);
	if (res != CURLE_OK) {
		ERROR_LOG(notif.gameId, "curl error: %d", res);
	}
	else
	{
		long code;
		curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
		if (code < 200 || code >= 300)
			ERROR_LOG(notif.gameId, "Discord error: %ld", code);
	}
	curl_slist_free_all(headers);
	curl_easy_cleanup(curl);
	threadCount.fetch_sub(1);
}

static void discordNotif(const Notif& notif)
{
	if (DiscordWebhook.empty())
		return;
	if (threadCount.fetch_add(1) >= 5) {
		threadCount.fetch_sub(1);
		WARN_LOG(notif.gameId, "Discord max thread count reached");
		return;
	}
	std::thread thread(postWebhook, notif);
	thread.detach();
}

void setDiscordWebhook(const std::string& url) {
	DiscordWebhook = url;
}

void discordLobbyJoined(GameId gameId, const std::string& username, const std::string& lobbyName, const std::vector<std::string>& playerList)
{
	using the_clock = std::chrono::steady_clock;
	static the_clock::time_point last_notif;
	the_clock::time_point now = the_clock::now();
	if (last_notif != the_clock::time_point() && now - last_notif < std::chrono::minutes(5))
		return;
	last_notif = now;
	Notif notif(gameId);
	notif.content = "Player **" + username + "** joined lobby **" + lobbyName + "**";
	notif.embed.title = "Lobby Players";
	for (const auto& player : playerList)
		notif.embed.text += player + "\n";
	discordNotif(notif);
}

void discordGameCreated(GameId gameId, const std::string& username, const std::string& gameName, const std::vector<std::string>& playerList)
{
	Notif notif(gameId);
	notif.content = "Player **" + username + "** created team **" + gameName + "**";
	notif.embed.title = "Lobby Players";
	for (const auto& player : playerList)
		notif.embed.text += player + "\n";

	discordNotif(notif);
}
