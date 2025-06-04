#pragma once
#include "shared_this.h"
#include "common.h"
#include <string>
#include <memory>
#include <vector>
#include <cstring>
#include <array>
#include <sstream>
#include <cassert>
#include <algorithm>

enum SRVOpcode : uint16_t
{
	S_PONG = 0x00,
	S_TEAM_NAME_EXISTS = 0x03,
	S_LOBBY_FULL = 0x05,
	S_SEARCH_RESULT = 0x07,
	S_MOTD = 0x0A,
	S_LOGIN_OK = 0x11,
	S_JOIN_LOBBY_ACK = 0x13,
	S_DISCONNECTED = 0x16,
	S_DO_DISCONNECT = 0x17,
	S_LOBBY_LIST_ITEM = 0x18,
	S_LOBBY_LIST_END = 0x19,
	S_GAME_LIST_ITEM = 0x1B,
	S_GAME_LIST_END = 0x1C,
	S_GAME_SEL_ACK = 0x1D,
	S_RECONNECT_ACK = 0x1F,
	S_LICENSE = 0x22,
	S_NEW_TEAM = 0x28,
	S_TEAM_JOINED = 0x29,
	S_LOBBY_CREATED = 0x2A,
	S_LOBBY_LEFT = 0x2C,
	S_LOBBY_CHAT = 0x2D,
	S_LOBBY_DM = 0x2E,
	S_PLAYER_LIST_ITEM = 0x30,
	S_PLAYER_LIST_END = 0x31,
	S_TEAM_LIST_ITEM = 0x32,
	S_TEAM_LIST_END = 0x33,
	S_TEAM_SHARED_MEM = 0x34,
	S_TEAM_DELETED = 0x3A,
	S_TEAM_LEFT = 0x3B,
	S_GAME_SERVER = 0x3D,
	S_LAUNCH_ACK = 0x3E,
	S_LOBBY_SHARED_MEM = 0x41,
	S_PLAYER_SHARED_MEM = 0x42,
	S_TEAM_CHAT = 0x43,
	S_TEAM_DM = 0x44,
	S_EXTUSER_MEM_ACK = 0x4F,
	S_EXTUSER_MEM_START = 0x50,
	S_EXTUSER_MEM_CHUNK = 0x51,
	S_EXTUSER_MEM_END = 0x52,
	S_LEAVE_LOBBY_ACK = 0xCB,
	S_SENDLOG_ACK = 0xCF,
	S_LOBBY_PLAYER_LIST_END = 0xD9,
	S_LOBBY_PLAYER_LIST_ITEM = 0xDA,
};

using sstream = std::stringstream;
class Player;
class Team;
class LobbyServer;
class LobbyConnection;

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

class Lobby : public SharedThis<Lobby>
{
public:
	void addPlayer(std::shared_ptr<Player> player);
	void removePlayer(std::shared_ptr<Player> player);
	void sendChat(const std::string& from, const std::string& message);
	std::shared_ptr<Team> createTeam(std::shared_ptr<Player> creator, const std::string& name, unsigned capacity, const std::string& type);
	void deleteTeam(std::shared_ptr<Team> team);
	std::shared_ptr<Team> getTeam(const std::string& name);
	void setSharedMem(const std::string& data);
	std::string getSjisName() const;

	std::string name;
	unsigned flags = 0;
	bool hasSharedMem = false;
	std::string sharedMem;
	const std::string gameName;
	unsigned capacity;
	std::vector<std::shared_ptr<Player>> members;
	std::vector<std::shared_ptr<Team>> teams;

private:
	Lobby(LobbyServer& parent, const std::string& gameName, const std::string& name, unsigned capacity, bool permanent)
		: name(name), gameName(gameName), capacity(capacity), permanent(permanent), parent(parent) {}

	bool permanent;
	LobbyServer& parent;
	friend super;
};

class Player : public SharedThis<Player>
{
public:
	void login(const std::string& name);
	std::string getIp();
	std::array<uint8_t, 4> getIpBytes();
	void disconnect(bool sendDCPacket = true);
	void setSharedMem(const std::vector<uint8_t>& data);
	std::vector<uint8_t> getSendDataPacket();

	void joinLobby(Lobby::Ptr lobby)
	{
		if (lobby != nullptr) {
			INFO_LOG(gameId, "%s joined lobby %s", name.c_str(), lobby->name.c_str());
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
			INFO_LOG(gameId, "%s left lobby %s", name.c_str(), lobby->name.c_str());
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

	void getExtraMem(const std::string& playerName, int offset, int length);
	void startExtraMem(int offset, int length);
	void setExtraMem(int index, const uint8_t *data, int size);
	void endExtraMem();

	int send(uint16_t opcode, const std::string& payload = {}) {
		//printf("send: %04x [%s]\n", opcode, payload.c_str());
		return send(opcode, (const uint8_t *)&payload[0], payload.length());
	}
	int send(uint16_t opcode, const std::vector<uint8_t>& payload) {
		return send(opcode, &payload[0], payload.size());
	}
	void receive(uint16_t opcode, const std::vector<uint8_t> payload);
	std::string toUtf8(const std::string& str) const;
	std::string fromUtf8(const std::string& str) const;

	std::string name;
	unsigned flags = 0;
	std::vector<uint8_t> sharedMem;
	Lobby::Ptr lobby;
	std::shared_ptr<Team> team;
	GameId gameId;
	LobbyServer& server;

private:
	Player(std::shared_ptr<LobbyConnection> connection, LobbyServer& server);
	int send(uint16_t opcode, const uint8_t *payload, unsigned length);
	std::vector<uint8_t> makePacket(uint16_t opcode, const uint8_t *payload, unsigned length);

	bool disconnected = false;
	std::shared_ptr<LobbyConnection> connection;
	std::vector<uint8_t> lastRecvPacket;
	std::vector<uint8_t> extraUserMem;
	int extraMemOffset = 0;
	int extraMemEnd = 0;
	std::string ipAddress;
	std::array<uint8_t, 4> ipBytes;
	friend super;
};

class Team : public SharedThis<Team>
{
public:
	void setSharedMem(std::string memAsStr)
	{
		sharedMem = memAsStr;
		for (auto& player : members)
			player->send(S_TEAM_SHARED_MEM, player->fromUtf8(name) + " " + sharedMem);
	}
	bool addPlayer(Player::Ptr player)
	{
		if (members.size() == capacity)
			return false;

		members.push_back(player);

		// Build player string
		sstream ss;
		ss << name;
		for (auto& p : members)
			ss << ' ' << p->name;

		// Send packet to all members
		for (auto& p : player->lobby->members)
			p->send(S_TEAM_JOINED, p->fromUtf8(ss.str()));

		return true;
	}
	bool removePlayer(Player::Ptr player)
	{
		auto it = std::find(members.begin(), members.end(), player);
		if (it != members.end())
		{
			members.erase(it);

			// Change host
			if (host == player && !members.empty())
				host = members[0];

			// Send Packets
			for (auto& p : player->lobby->members)
				p->send(S_TEAM_LEFT, p->fromUtf8(name + " " + player->name));

			// Team deleted?
			if (members.empty())
				parent->deleteTeam(shared_from_this());
			return true;
		}
		else {
			WARN_LOG(player->gameId, "Player %s not found in team %s", player->name.c_str(), name.c_str());
			return false;
		}
	}

	void sendChat(const std::string& from, const std::string& message)
	{
		INFO_LOG(host->gameId, "%s team chat: %s", from.c_str(), message.c_str());
		for (auto& player : members)
			player->send(S_TEAM_CHAT, player->fromUtf8(from + " " + message));
	}

	void sendSharedMemPlayer(Player::Ptr owner, const std::vector<uint8_t>& data) {
		for (auto& player : members)
			player->send(S_PLAYER_SHARED_MEM, Packet::createSharedMemPacket(data, player->fromUtf8(owner->name)));
	}

	void sendGameServer(Player::Ptr p) {
		for (auto& player : members)
			player->send(S_GAME_SERVER, "172.20.100.100 9503");	// FIXME?
	}

	void launchGame(Player::Ptr p)
	{
		sstream ss;
		ss << members.size();
		for (auto& player : members)
			ss << ' ' << (host == player ? "*" : "") << player->fromUtf8(player->name) << ' ' << player->getIp();
		p->send(S_LAUNCH_ACK, ss.str());
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

class LobbyServer
{
public:
	LobbyServer(GameId gameId, const std::string& name = {})
		: gameId(gameId)
	{
		if (!name.empty())
			this->name = name;
		servers.push_back(this);
		switch (gameId)
		{
		case GameId::AeroDancingI:
			createLobby("QLADI", 100);
			createLobby("NLADI", 100);
			createLobby("PLADI", 100);
			break;
		case GameId::AeroDancingF:
			createLobby("Main_Lobby", 100);
			break;
		case GameId::HundredSwords:
			createLobby("Red", 100);
			createLobby("Yellow", 100);
			createLobby("Blue", 100);
			createLobby("Green", 100);
			createLobby("Purple", 100);
			createLobby("Orange", 100);
			break;
		case GameId::CuldCept:
			{
				createLobby("KANSEN", 100);
				Lobby::Ptr lobby = createLobby("Beginer_A", 100);
				lobby->setSharedMem("000001000000000000000000000000000000000000000000000000000000");
				lobby = createLobby("Beginer_B", 100);
				lobby->setSharedMem("000001000000000000000000000000000000000000000000000000000000");
				lobby = createLobby("_unNormal_A", 100);
				lobby->setSharedMem("000001000000000000000000000000000000000000000000000000000000");
				lobby = createLobby("_unNormal_B", 100);
				lobby->setSharedMem("000001000000000000000000000000000000000000000000000000000000");
				lobby = createLobby("_ueExpert_A", 100);
				lobby->setSharedMem("000001000000000000000000000000000000000000000000000000000000");
				lobby = createLobby("_ueExpert_B", 100);
				lobby->setSharedMem("000001000000000000000000000000000000000000000000000000000000");
				break;
			}
		default:
			createLobby("2P_Red", 100);
			createLobby("4P_Yellow", 100);
			createLobby("2P_Blue", 100);
			createLobby("2P_Green", 100);
			createLobby("4P_Purple", 100);
			createLobby("4P_Orange", 100);
			break;
		}
	}

	Lobby::Ptr createLobby(const std::string& name, unsigned capacity, bool permanent = true)
	{
		if (capacity <= 1)
			capacity = 100;
		if (getLobby(name) == nullptr)
		{
			Lobby::Ptr lobby = Lobby::create(*this, getGameName(), name, capacity, permanent);
			lobbies.push_back(lobby);
			return lobby;
		}
		return nullptr;
	}

	void deleteLobby(std::string name)
	{
		auto it = std::find_if(lobbies.begin(), lobbies.end(), [&name](const Lobby::Ptr& lobby) {
			return lobby->name == name;
		});
		if (it != lobbies.end())
			lobbies.erase(it);
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

	Player::Ptr getPlayer(const std::string& name, Player::Ptr except = {})
	{
		for (auto& player : players)
			if (player != except && player->name == name)
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
	void addPlayer(Player::Ptr player) {
		players.push_back(player);
	}
	void removePlayer(Player::Ptr player)
	{
		auto it = std::find(players.begin(), players.end(), player);
		if (it != players.end())
			players.erase(it);
	}

	const std::string& getName() const {
		return name;
	}

	GameId getGameId() const {
		return gameId;
	}

	uint16_t getIpPort() const
	{
		switch (gameId)
		{
		case GameId::Daytona: return 9501;
		case GameId::Tetris: return 9502;
		case GameId::GolfShiyouyo: return 9503;
		case GameId::AeroDancingI: return 9504;
		case GameId::HundredSwords: return 9505;
		case GameId::AeroDancingF: return 9506;
		case GameId::CuldCept: return 9507;
		default: assert(false); return 0;
		}
	}

	std::string getGameName() const
	{
		switch (gameId)
		{
		case GameId::Daytona: return "Daytona";
		case GameId::Tetris: return "Tetris";
		case GameId::GolfShiyouyo: return "Golf";
		case GameId::AeroDancingI: return "T-6807M";
		case GameId::AeroDancingF: return "T-6805M";
		case GameId::HundredSwords: return "Hundred";
		case GameId::CuldCept: return "Culdcept";
		default: assert(false); return "???";
		}
	}

	const std::string& getMotd() const {
		return motd;
	}
	void setMotd(const std::string& motd) {
		this->motd = motd;
	}

	static LobbyServer *getServer(GameId gameId)
	{
		for (LobbyServer *server : servers)
			if (server->getGameId() == gameId
					|| (server->getGameId() == GameId::Daytona && gameId == GameId::DaytonaJP))
				return server;
		return nullptr;
	}

private:
	GameId gameId;
	std::string name = "IWANGO_Server_1";
	std::string motd = "Welcome to IWANGO Emulator by Ioncannon";
	std::vector<Player::Ptr> players;
	std::vector<Lobby::Ptr> lobbies;
	static std::vector<LobbyServer *> servers;
};
