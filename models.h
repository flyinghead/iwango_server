#pragma once
#include "shared_this.h"
#include <string>
#include <memory>
#include <vector>
#include <cstring>
#include <array>

class Packet
{
public:
	static std::vector<uint8_t> createSharedMemPacket(const std::vector<uint8_t>& sharedMemBytes, const std::string& stringData)
	{
		std::vector<uint8_t> data;
		data.resize(1 + stringData.length() + sharedMemBytes.size());
		data[0] = stringData.length();
		memcpy(&data[1], &stringData[0], stringData.length());
		memcpy(&data[1 + stringData.length()], &sharedMemBytes[0], sharedMemBytes.size());
		return data;
	}
};

class Game
{
public:
	Game(const std::string& name = {}) : name(name) {
	}

	std::string name;
	int unknown = 0;
};

class Lobby;
class Player;
class Team;
class LobbyConnection;

class Lobby : public SharedThis<Lobby>
{
public:
	void addPlayer(std::shared_ptr<Player> player);
	void removePlayer(std::shared_ptr<Player> player);
	void sendChat(const std::string& from, const std::string& message);
	std::shared_ptr<Team> createTeam(std::shared_ptr<Player> creator, const std::string& name, unsigned capacity, const std::string& type);
	void deleteTeam(std::shared_ptr<Team> team);
	std::shared_ptr<Team> getTeam(const std::string& name);

	std::string name;
	unsigned flags = 0;
	bool hasSharedMem = false;
	std::string sharedMem;
	const Game& game;
	unsigned capacity;
	std::vector<std::shared_ptr<Player>> members;
	std::vector<std::shared_ptr<Team>> teams;

private:
	Lobby(const Game& game, const std::string& name, unsigned capacity)
		: name(name), game(game), capacity(capacity) {}

	friend super;
};

class Player : public SharedThis<Player>
{
public:
	std::string getIp();
	uint32_t getIpUint32();
	std::array<uint8_t, 4> getIpBytes();
	uint16_t getPort();
	void disconnect(bool sendDCPacket = true);
	void setSharedMem(const std::vector<uint8_t>& data);
	std::vector<uint8_t> getSendDataPacket();

	void joinLobby(Lobby::Ptr lobby)
	{
		if (lobby != nullptr) {
			this->lobby = lobby;
			lobby->addPlayer(shared_from_this());
		}
		else {
			// TODO Some Error
		}
	}
	void leaveLobby()
	{
		if (lobby != nullptr)
		{
			lobby->removePlayer(shared_from_this());
			lobby = nullptr;
		}
		else {
			// TODO Some Error
		}
	}

	void createTeam(const std::string& name, unsigned capacity, const std::string& type);
	void joinTeam(const std::string& name);
	void leaveTeam();

	void sendExtraMem(const uint8_t* extraMem, int offset, int length);
	int send(uint16_t opcode, const std::string& payload = {}) {
		return send(opcode, (const uint8_t *)&payload[0], payload.length());
	}
	int send(uint16_t opcode, const std::vector<uint8_t>& payload) {
		return send(opcode, &payload[0], payload.size());
	}
	void receive(uint16_t opcode, const std::vector<uint8_t> payload);

	std::string name;
	unsigned flags = 0;
	std::vector<uint8_t> sharedMem;
	Lobby::Ptr lobby;
	std::shared_ptr<Team> team;
	const Game *game = nullptr;

private:
	Player(std::shared_ptr<LobbyConnection> connection)
		: sharedMem(0x1e), connection(connection) {}
	int send(uint16_t opcode, const uint8_t *payload, unsigned length);
	std::vector<uint8_t> makePacket(uint16_t opcode, const uint8_t *payload, unsigned length);

	bool disconnected = false;
	std::shared_ptr<LobbyConnection> connection;
	std::vector<uint8_t> lastRecvPacket;
	friend super;
};

class Team : public SharedThis<Team>
{
public:
	void setSharedMem(std::string memAsStr)
	{
		sharedMem = memAsStr;
		for (auto& player : members)
			player->send(0x34, name + " " + sharedMem);
	}
	bool addPlayer(Player::Ptr player)
	{
		if (members.size() == capacity)
			return false;

		members.push_back(player);

		// Build player string
		std::string players = "";
		for (auto& p : members)
			players += " " + p->name;
		players = players.substr(1);

		// Send packet to all members
		for (auto& p : player->lobby->members)
			p->send(0x29, name + " " + players);

		return true;
	}
	bool removePlayer(Player::Ptr player)
	{
		for (auto it = members.begin(); it != members.end(); ++it)
		{
			if ((*it) == player)
			{
				members.erase(it);

				// Change host
				if (host == player && !members.empty())
					host = members[0];

				// Send Packets
				for (auto& p : player->lobby->members)
					p->send(0x3B, name + " " + player->name);

				// Team deleted?
				if (members.empty())
					parent->deleteTeam(shared_from_this());
				return true;
			}
		}
		fprintf(stderr, "Player %s not found in team %s\n", player->name.c_str(), name.c_str());
		return false;
	}

	void sendChat(const std::string& from, const std::string& message) {
		for (auto& player : members)
			player->send(0x43, from + " " + message);
	}

	void sendSharedMemPlayer(Player::Ptr owner, const std::vector<uint8_t>& data) {
		for (auto& player : members)
			player->send(0x42, Packet::createSharedMemPacket(data, owner->name));
	}

	void sendGameServer(Player::Ptr p) {
		for (auto& player : members)
			player->send(0x3d, "192.168.1.31 9501");	// FIXME
	}

	void launchGame(Player::Ptr p)
	{
		std::string data = std::to_string(members.size());
		for (auto& player : members) {
			data += " " + std::string(host == player ? "*" : "") + player->name + " " + player->getIp();
		}
		p->send(0x3e, data);
	}

	std::string name;
	unsigned capacity;
	std::shared_ptr<Player> host;
	std::string sharedMem;
	std::vector<std::shared_ptr<Player>> members;
	unsigned flags = 0;

private:
	Team(Lobby::Ptr parent, std::string name, unsigned capacity, std::shared_ptr<Player> host)
		: name(name), capacity(capacity), host(host), parent(parent) {
		members.push_back(host);
	}

	std::shared_ptr<Lobby> parent;
	friend super;
};

class Server
{
public:
	void addGame(const std::string& name) {
		games.emplace_back(name);
	}
	const std::vector<Game>& getGames() {
		return games;
	}
	const Game *getGame(const std::string& name)
	{
		for (const auto& game : games)
			if (game.name == name)
				return &game;
		return nullptr;
	}

	Lobby::Ptr createLobby(const std::string& name, unsigned capacity)
	{
		if (getLobby(name) == nullptr && capacity > 0)
		{
			Lobby::Ptr lobby = Lobby::create(games[0], name, capacity);
			lobbies.push_back(lobby);
			return lobby;
		}
		return nullptr;
	}
	void deleteLobby(std::string name)
	{
		for (auto it = lobbies.begin(); it != lobbies.end(); ++it) {
			if ((*it)->name == name) {
				lobbies.erase(it);
				break;
			}
		}
	}
	Lobby::Ptr getLobby(const std::string& name)
	{
		for (auto& lobby : lobbies)
			if (lobby->name == name)
				return lobby;
		return nullptr;
	}
	const std::vector<Lobby::Ptr>& getLobbyList() {
		return lobbies;
	}

	Player::Ptr getPlayer(const std::string& name)
	{
		for (auto& player : players)
			if (player->name == name)
				return player;
		return nullptr;
	}
	Player::Ptr IsIPUnique(Player::Ptr me)
	{
		std::string myIp = me->getIp();
		for (auto& player : players)
			if (player->getIp() == myIp && player != me)
				return player;
		return nullptr;
	}
	void addPlayer(Player::Ptr player)
	{
		players.push_back(player);
	}
	void removePlayer(Player::Ptr player)
	{
		for (auto it = players.begin(); it != players.end(); ++it) {
			if ((*it) == player) {
				players.erase(it);
				break;
			}
		}
	}

	const std::string& getName() const {
		return name;
	}

	static Server& instance() {
		static Server server;
		return server;
	}

private:
	std::string name = "Daytona_USA_Emu_#1";	// TODO
	std::vector<Player::Ptr> players;
	std::vector<Lobby::Ptr> lobbies;
	std::vector<Game> games;
};
