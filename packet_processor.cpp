#include "lobby_server.h"
#include "models.h"
#include "common.h"
#include <unordered_map>
#include <sys/time.h>

using sstream = std::stringstream;

enum CLIOpcode : uint16_t
{
	LOGIN = 0x01,
	LOGIN2 = 0x02,
	SEND_LOG = 0x03,
	ENTR_LOBBY = 0x04,
	DISCONNECT = 0x05,
	GET_LOBBIES = 0x07,
	GET_GAMES = 0x08,
	SELECT_GAME = 0x09,
	PING = 0x0A,
	SEARCH = 0x0B,
	GET_LICENSE = 0x0C,
	// TODO tetris,golf lobby: Request[41]: 000e []
	GET_TEAMS = 0x0F,
	REFRESH_PLAYERS = 0x10,
	CHAT_LOBBY = 0x11,
	SHAREDMEM_PLAYER = 0x1B,
	// TODO tetris lobby: Request[51]: 001c []
	// TODO tetris lobby: Request[54]: 001d []
	SHAREDMEM_TEAM = 0x20,
	LEAVE_TEAM = 0x21,
	// TODO tetris,golf 0x28 (no args)
	GET_EXTRAUSERMEM = 0x29,
	REGIST_EXTRAUSERMEM_START = 0x2A,
	REGIST_EXTRAUSERMEM_TRANSFER = 0x2B,	// TODO golf seems to save stuff like friends. Need db
	REGIST_EXTRAUSERMEM_END = 0x2C,
	RECONNECT = 0x0D,
	LAUNCH_REQUEST = 0x22,
	LAUNCH_GAME = 0x65,
	REFRESH_USERS = 0x67,
	CHAT_TEAM = 0x23,
	CREATE_TEAM = 0x24,
	JOIN_TEAM = 0x25,
	LEAVE_LOBBY = 0x3C
};

static void loginCommand(Player::Ptr player, const std::vector<uint8_t>&, const std::string& dataAsString)
{
	std::vector<std::string> split = splitString(dataAsString, ' ');
	player->name = player->toUtf8(split[0]);
	if (player->name.empty())
	{
		// FIXME not working no matter what I send...
		player->send(0x03, "Empty handle");
	    player->send(0xE3);
	    player->send(0x16);
	    player->disconnect(false);
		return;
	}
	// Daytona (US) allowed characters (when searching): A-Za-z0-9_-

	// Is this handle already in the server? Handle is used as a key and HAS to be unique.
	// FIXME JP users will all connect with 'flycast1'
	Player::Ptr exists = player->server.getPlayer(player->name, player);
	if (exists != nullptr) {
		exists->name = "";
		exists->disconnect();
	}

	// Is this IP already in the server? IP is assumed to be a WAN IP due to dial-up days. Disabled when debugging.
#ifndef DEBUG
	exists = player->server.IsIPUnique(player);
	if (exists != nullptr) {
		exists->name = "";
		exists->disconnect();
	}
#endif

	// We are good to continue
	time_t now;
	time(&now);
	struct tm *tm = localtime(&now);
	sstream ss;
	ss << "0100 0102 " << (tm->tm_year + 1900)
	   << ":" << (tm->tm_mon + 1)
	   << ":" << tm->tm_mday
	   << ":" << tm->tm_hour
	   << ":" << tm->tm_min
	   << ":" << tm->tm_sec;
	player->send(0x11, ss.str());
}

static void login2Command(Player::Ptr player, const std::vector<uint8_t>&, const std::string& dataAsString)
{
	//std::vector<std::string> split = splitString(dataAsString, ' ');
	// args:
	// 0	:key user id
	// 1	":dummy"
	// 2	:gameId
	// 3	:console id
	// 4	:1
	// 5	:0 or :1 (handle index?)
	player->send(0x0C, "LOB 999 999 AAA AAA");
	player->send(0x0A, player->server.getMotd());
	player->send(0xE1);	// Ext MeM ready?
}

static void refreshPlayersCommand(Player::Ptr player, const std::vector<uint8_t>&, const std::string& dataAsString)
{
	std::vector<std::string> split = splitString(dataAsString, ' ');
	if (split[0].empty()) {
		// Get all players
		for (auto& p : player->lobby->members)
			player->send(0x30, p->getSendDataPacket());
	}
	else
	{
		// Get specific
		std::string name = player->toUtf8(split[0]);
		Player::Ptr p = player->server.getPlayer(name);
		if (p != nullptr)
			player->send(0x30, p->getSendDataPacket());
	}
	player->send(0x31);
}

static void refreshLobbiesCommand(Player::Ptr player, const std::vector<uint8_t>&, const std::string& dataAsString)
{
	std::vector<std::string> split = splitString(dataAsString, ' ');
	const std::vector<Lobby::Ptr>& lobbies = player->server.getLobbyList();
	for (auto& lobby : lobbies)
	{
		sstream ss;
		ss << player->fromUtf8(lobby->name) << ' ' << lobby->members.size()
		   << ' ' << lobby->capacity << ' ' << lobby->flags
		   << ' ' << (lobby->hasSharedMem ? lobby->sharedMem : "#")
		   << " #" << lobby->gameName;
		player->send(0x18, ss.str());
	}
	player->send(0x19);
}

static void createOrJoinLobby(Player::Ptr player, const std::vector<uint8_t>&, const std::string& dataAsString)
{
	std::vector<std::string> split = splitString(dataAsString, ' ');
	if (split.size() != 2) {
		fprintf(stderr, "ENTR_LOBBY: bad arg count %zd\n", split.size());
		return;
	}
	std::string lobbyName = player->toUtf8(split[0]);
	uint16_t capacity = atoi(split[1].c_str());
	Lobby::Ptr lobby = player->server.getLobby(lobbyName);
	if (lobby == nullptr)
		lobby = player->server.createLobby(lobbyName, capacity);
	if (lobby != nullptr)
		player->joinLobby(lobby);
}

static void leaveLobbyCommand(Player::Ptr player, const std::vector<uint8_t>&, const std::string& dataAsString) {
	player->leaveLobby();
}

static void refreshTeamsCommand(Player::Ptr player, const std::vector<uint8_t>&, const std::string& dataAsString)
{
	if (player->lobby != nullptr)
	{
		std::vector<std::string> split = splitString(dataAsString, ' ');
		for (Team::Ptr& team : player->lobby->teams)
        {
			sstream ss;
			ss << player->fromUtf8(team->name)
			   << ' ' << team->members.size() << ' ' << team->capacity
			   << ' ' << team->flags << ' ';
			if (!team->sharedMem.empty())
				ss << '*' << team->sharedMem;
			else
				ss << '#';
			for (Player::Ptr& p : team->members)
			{
				ss << ' ';
				if (team->host == p)
					ss << '*';
				else
					ss << '#';
				ss << player->fromUtf8(p->name);
            }
			ss << ' ' << player->lobby->gameName;
            player->send(0x32, ss.str());
        }
	}
	player->send(0x33);
}

static void createTeamCommand(Player::Ptr player, const std::vector<uint8_t>&, const std::string& dataAsString)
{
	std::vector<std::string> split = splitString(dataAsString, ' ');
	if (split.size() == 3)
	{
		unsigned capacity = atoi(split[0].c_str());
		if (player->lobby != nullptr)
			player->lobby->createTeam(player, player->toUtf8(split[1]), capacity, split[2]);
		else
			player->disconnect();
	}
}

static void joinTeamCommand(Player::Ptr player, const std::vector<uint8_t>&, const std::string& dataAsString)
{
	std::vector<std::string> split = splitString(dataAsString, ' ');
	player->joinTeam(player->toUtf8(split[0]));
}

static void leaveTeamCommand(Player::Ptr player, const std::vector<uint8_t>&, const std::string& dataAsString) {
	player->leaveTeam();
}

static void refreshGamesCommand(Player::Ptr player, const std::vector<uint8_t>&, const std::string& dataAsString)
{
	player->send(0x1B, "1 " + player->server.getGameName());
	player->send(0x1C);
}

static void selectGameCommand(Player::Ptr player, const std::vector<uint8_t>&, const std::string& dataAsString)
{
	std::vector<std::string> split = splitString(dataAsString, ' ');
	std::string& gameName = split[0];
	player->send(0x1D, player->fromUtf8(player->name) + " " + gameName);
}

static void getLicenseCommand(Player::Ptr player, const std::vector<uint8_t>&, const std::string& dataAsString) {
	player->send(0x22, "ABCDEFGHI");
}

static void getExtraUserMem(Player::Ptr player, const std::vector<uint8_t>&, const std::string& dataAsString)
{
	std::vector<std::string> split = splitString(dataAsString, ' ');
	if (split.size() == 3)
	{
		uint8_t mem[] {
				0x52, 0x45, 0x47, 0x41, 0x54, 0x45, 0x54, 0x52, 0x49, 0x53, 0x20, 0x31, 0x2E, 0x30, 0x30, 0x00, // SEGATETRIS 1.00
				0x0C, 0x02, 0x02, 0x00, 0x01, 0x00, 0x04, 0x00, 0x02, 0x00, 0x00, 0x00
		};
		//std::string& userName = split[0];
		int offset = std::clamp<int>(atoi(split[1].c_str()), 0, sizeof(mem));
		int length = std::clamp<int>(atoi(split[2].c_str()), 0, sizeof(mem) - offset);

		player->sendExtraMem(mem, offset, length);
	}
	else {
		player->disconnect();
	}
}

static void registerExtraUserMem(Player::Ptr player, const std::vector<uint8_t>&, const std::string& dataAsString) {
	player->send(0x4F);
}

static void chatLobbyCommand(Player::Ptr player, const std::vector<uint8_t>&, const std::string& dataAsString) {
	if (player->lobby != nullptr)
		player->lobby->sendChat(player->name, player->toUtf8(dataAsString.substr(dataAsString.find(' ') + 1)));
}

static void chatTeamCommand(Player::Ptr player, const std::vector<uint8_t>&, const std::string& dataAsString) {
	if (player->team != nullptr)
		player->team->sendChat(player->name, player->toUtf8(dataAsString));
}

static void sharedMemPlayerCommand(Player::Ptr player, const std::vector<uint8_t>& data, const std::string&) {
	player->setSharedMem(data);
}

static void sharedMemTeamCommand(Player::Ptr player, const std::vector<uint8_t>&, const std::string& dataAsString)
{
	std::vector<std::string> split = splitString(dataAsString, ' ');
	//std::string& teamName = split[0];
	std::string& sharedMemStr = split[1];

	if (player->team != nullptr)
		player->team->setSharedMem(sharedMemStr);
}

static void pingCommand(Player::Ptr player, const std::vector<uint8_t>&, const std::string&) {
	player->send(0);
}

static void disconnectCommand(Player::Ptr player, const std::vector<uint8_t>&, const std::string&)
{
    player->send(0xE3);
    player->send(0x16);
    player->disconnect(false);
}

static void reconnectCommand(Player::Ptr player, const std::vector<uint8_t>& data, const std::string&) {
	player->send(0x1f);
}

static void launchRequestCommand(Player::Ptr player, const std::vector<uint8_t>&, const std::string&) {
	player->team->sendGameServer(player);
}

static void launchGameCommand(Player::Ptr player, const std::vector<uint8_t>&, const std::string&) {
	player->team->launchGame(player);
}

static std::vector<uint8_t> test(int num)
{
	std::vector<uint8_t> sharedMem(0x1E);
	sharedMem[0] = 0xFF;
	sharedMem[4] = 0xFF;
	sharedMem[8] = 0xFF;
	sharedMem[12] = 0xFF;
	sharedMem[16] = 0xFF;
	sharedMem[20] = 0xFF;
	std::vector<uint8_t> data1 = Packet::createSharedMemPacket(sharedMem, "0 *AAA" + std::to_string(num) + " 0 0 0");
	std::vector<uint8_t> testdata(data1.size() + 4);
	memcpy(&testdata[0], &data1[0], data1.size());
	testdata[testdata.size() - 4] = 0xFF;
	return testdata;
}

static void refreshUsersCommand(Player::Ptr player, const std::vector<uint8_t>&, const std::string& dataAsString)
{
	std::string lobby = player->toUtf8(dataAsString);
	int count = 0;
	Lobby::Ptr pLobby = player->server.getLobby(lobby);
	if (pLobby)
		count = pLobby->members.size();
	for (int i = 0; i < count; i++)
		player->send(0xDA, test(i));
	player->send(0xD9);
}

static void searchCommand(Player::Ptr player, const std::vector<uint8_t>&, const std::string& dataAsString)
{
	Player::Ptr found = player->server.getPlayer(player->toUtf8(dataAsString));
	if (found != nullptr) {
		sstream ss;
		ss << player->fromUtf8(found->name) << " !" << player->server.getName() << ' ';
		if (found->lobby != nullptr)
			ss << '!' << player->fromUtf8(found->lobby->name);
		else
			ss << '#';
		player->send(0x07, ss.str());
	}
	player->send(0xC9, "1");	// FIXME golf seems to think it's found, but garbage name(?)
								// FIXME search and say says failed to send message although the player is found (but self so might be the issue)
}

static void nullCommand(Player::Ptr, const std::vector<uint8_t>&, const std::string&) {
}

using CommandHandler = void(*)(Player::Ptr, const std::vector<uint8_t>&, const std::string&);
static std::unordered_map<CLIOpcode, CommandHandler> CommandHandlers = {
		{ LOGIN, loginCommand },
		{ LOGIN2, login2Command },
		{ REFRESH_PLAYERS, refreshPlayersCommand },
		{ GET_LOBBIES, refreshLobbiesCommand },
		{ ENTR_LOBBY, createOrJoinLobby },
		{ LEAVE_LOBBY, leaveLobbyCommand },
		{ GET_TEAMS, refreshTeamsCommand },
		{ CREATE_TEAM, createTeamCommand },
		{ JOIN_TEAM, joinTeamCommand },
		{ LEAVE_TEAM, leaveTeamCommand },
		{ GET_EXTRAUSERMEM,  getExtraUserMem },
		{ REGIST_EXTRAUSERMEM_START, registerExtraUserMem },
		{ REGIST_EXTRAUSERMEM_TRANSFER, registerExtraUserMem },
		{ REGIST_EXTRAUSERMEM_END, registerExtraUserMem },
		{ GET_GAMES, refreshGamesCommand },
		{ SELECT_GAME, selectGameCommand },
		{ GET_LICENSE, getLicenseCommand },
		{ CHAT_LOBBY, chatLobbyCommand },
		{ CHAT_TEAM, chatTeamCommand },
		{ SHAREDMEM_PLAYER, sharedMemPlayerCommand },
		{ SHAREDMEM_TEAM, sharedMemTeamCommand },
		{ PING, pingCommand },
		{ DISCONNECT, disconnectCommand },
		{ LAUNCH_REQUEST, launchRequestCommand },
		{ LAUNCH_GAME, launchGameCommand },
		{ REFRESH_USERS, refreshUsersCommand },
		{ RECONNECT, reconnectCommand },
		{ SEARCH, searchCommand },
		{ SEND_LOG, nullCommand },
};

void PacketProcessor::handlePacket(Player::Ptr player, uint16_t opcode, const std::vector<uint8_t>& payload)
{
	std::string payloadAsString = std::string((char *)&payload[0], (char *)&payload[payload.size()]);
	std::vector<std::string> split = splitString(payloadAsString, ' ');

	if (CommandHandlers.count((CLIOpcode)opcode) != 0)
		CommandHandlers[(CLIOpcode)opcode](player, payload, payloadAsString);
	else
		printf("Received unknown opcode: 0x%02x -> %s\n", opcode, payloadAsString.c_str());
}

