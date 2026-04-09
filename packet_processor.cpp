#include "lobby_server.h"
#include "models.h"
#include "common.h"
#include "discord.h"
#include <dcserver/status.hpp>
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
	RECONNECT = 0x0D,
	LAUNCH_GAME_ACK = 0x0E,
	GET_TEAMS = 0x0F,
	REFRESH_PLAYERS = 0x10,
	CHAT_LOBBY = 0x11,
	SHAREDMEM_LOBBY = 0x1A,
	SHAREDMEM_PLAYER = 0x1B,
	// TODO tetris: 001c []	SendWin
	// TODO tetris: 001d []	SendLoose
	SHAREDMEM_TEAM = 0x20,
	LEAVE_TEAM = 0x21,
	LAUNCH_REQUEST = 0x22,
	CHAT_TEAM = 0x23,
	CREATE_TEAM = 0x24,
	JOIN_TEAM = 0x25,
	SEND_CTCPMSG = 0x26,
	EXTRAUSERMEM_ACK = 0x28,
	GET_EXTRAUSERMEM = 0x29,
	REGIST_EXTRAUSERMEM_START = 0x2A,
	REGIST_EXTRAUSERMEM_TRANSFER = 0x2B,
	REGIST_EXTRAUSERMEM_END = 0x2C,
	JOIN_TEAM_SPECTATOR = 0x2D,
	LEAVE_TEAM_SPECTATOR = 0x2E,
	CREATE_LOBBY = 0x3A,
	CREATE_GAME = 0x3B,
	LEAVE_LOBBY = 0x3C,
	JOIN_GROUP = 0x3F,
	LAUNCH_GAME = 0x65,
	REFRESH_USERS = 0x67,
	LAUNCH_REQUEST_SINGLE = 0x6a,
	RJ_REQUEST_RANKING = 0x6b,
};

static void loginCommand(Player::Ptr player, const std::vector<uint8_t>&, const std::string& dataAsString)
{
	std::vector<std::string> split = splitString(dataAsString, ' ');
	std::string userName = player->toUtf8(split[0]);
	if (userName.empty())
	{
		// FIXME not working no matter what I send...
		player->send(S_TEAM_NAME_EXISTS, "Empty handle");
	    player->send(0xE3);
	    player->send(S_DISCONNECTED);
	    player->disconnect(false);
		return;
	}
	player->login(userName);
	// Daytona (US) allowed characters (when searching): A-Za-z0-9_-

	// Is this handle already in the server? Handle is used as a key and HAS to be unique.
	Player::Ptr exists = player->server.getPlayer(userName, player);
	if (exists != nullptr) {
		exists->name = "";
		exists->disconnect();
	}

	// Is this IP already in the server? IP is assumed to be a WAN IP due to dial-up days. Disabled when debugging.
#ifdef NDEBUG
	exists = player->server.IsIPUnique(player);
	if (exists != nullptr) {
		exists->name = "";
		exists->disconnect();
	}
#endif
	INFO_LOG(player->gameId, "[%s] Player %s logged in", player->getIp().c_str(), player->name.c_str());
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
	player->send(S_LOGIN_OK, ss.str());
	status::join(getDCNetGameId(player->gameId), player->getIp(), player->getPort(), player->name);
}

static void login2Command(Player::Ptr player, const std::vector<uint8_t>&, const std::string& dataAsString)
{
	std::vector<std::string> split = splitString(dataAsString, ' ');
	// args:
	// 0	:key user id
	// 1	":dummy"
	// 2	:gameId
	// 3	:console id
	// 4	:1
	// 5	:0 or :1 (handle index?)
	if (split.size() > 3)
		INFO_LOG(player->gameId, "[%s] Player %s console ID: %s", player->getIp().c_str(), player->name.c_str(), split[3].substr(1).c_str());
	// response:
	// 0	auth status (0 is success)
	// 1	error num (0 is success, 1 banned user, 8 server maintenance, 16 line busy, ...)
	// 2	limit date?
	// 3	header? title? first line displayed by Rune Jade, ignored by all other games (required)
	player->send(0x0C, "0 0 999 Hello");
	player->send(S_MOTD, player->server.getMotd());
	player->send(S_EXT_MEM_READY);
}

static void refreshPlayersCommand(Player::Ptr player, const std::vector<uint8_t>&, const std::string& dataAsString)
{
	std::vector<std::string> split = splitString(dataAsString, ' ');
	if (split[0].empty())
	{
		// Get all players
		if (player->lobby != nullptr) {
			for (auto& p : player->lobby->members)
				player->send(S_PLAYER_LIST_ITEM, p->getSendDataPacket());
		}
	}
	else
	{
		// Get specific
		std::string name = player->toUtf8(split[0]);
		Player::Ptr p = player->server.getPlayer(name);
		if (p != nullptr)
			player->send(S_PLAYER_LIST_ITEM, p->getSendDataPacket());
	}
	player->send(S_PLAYER_LIST_END);
}

static void sendLobby(Player::Ptr player, uint16_t opcode, Lobby::Ptr lobby)
{
	sstream ss;
	ss << lobby->getSjisName() << ' ' << lobby->members.size()
	   << ' ' << lobby->capacity << ' ' << lobby->flags
	   << ' ' << (lobby->hasSharedMem ? "*" + lobby->sharedMem : "#")
	   << " #" << lobby->gameName;
	player->send(opcode, ss.str());
}

static void refreshLobbiesCommand(Player::Ptr player, const std::vector<uint8_t>&, const std::string& dataAsString)
{
	std::vector<std::string> split = splitString(dataAsString, ' ');
	const std::vector<Lobby::Ptr>& lobbies = player->server.getLobbyList();
	for (auto& lobby : lobbies)
		sendLobby(player, S_LOBBY_LIST_ITEM, lobby);
	player->send(S_LOBBY_LIST_END);
}

static void createOrJoinLobby(Player::Ptr player, const std::vector<uint8_t>&, const std::string& dataAsString)
{
	// name capacity [type]
	// types: RRT (0x2000), GROUP (0x800), ARCADE (0x10), TOURNAMENT (4)
	std::vector<std::string> split = splitString(dataAsString, ' ');
	if (split.size() < 2 || split.size() > 3) {
		ERROR_LOG(player->gameId, "[%s] ENTR_LOBBY: bad arg count %zd", player->name.c_str(), split.size());
		return;
	}
	std::string lobbyName = player->toUtf8(split[0]);
	uint16_t capacity = atoi(split[1].c_str());
	Lobby::Ptr lobby = player->server.getLobby(lobbyName);
	if (lobby == nullptr)
	{
		lobby = player->server.createLobby(lobbyName, capacity, false);
		if (lobby != nullptr)
			// acknowledge the creation
			sendLobby(player, S_LOBBY_CREATED, lobby);
	}
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
				if (!p->spectator)
					ss << '#';
				ss << player->fromUtf8(p->name);
            }
			ss << ' ' << player->lobby->gameName;
            player->send(S_TEAM_LIST_ITEM, ss.str());
        }
	}
	player->send(S_TEAM_LIST_END);
}

static void createTeamCommand(Player::Ptr player, const std::vector<uint8_t>&, const std::string& dataAsString)
{
	std::vector<std::string> split = splitString(dataAsString, ' ');
	if (split.size() == 3)
	{
		unsigned capacity = atoi(split[0].c_str());
		if (player->lobby != nullptr)
			player->createTeam(player->toUtf8(split[1]), capacity, split[2]);
		else
			player->disconnect();
	}
}

static void joinTeamCommand(Player::Ptr player, const std::vector<uint8_t>&, const std::string& dataAsString)
{
	std::vector<std::string> split = splitString(dataAsString, ' ');
	player->joinTeam(player->toUtf8(split[0]), false);
}
static void joinTeamSpecCommand(Player::Ptr player, const std::vector<uint8_t>&, const std::string& dataAsString)
{
	std::vector<std::string> split = splitString(dataAsString, ' ');
	player->joinTeam(player->toUtf8(split[0]), true);
}

static void leaveTeamCommand(Player::Ptr player, const std::vector<uint8_t>&, const std::string& dataAsString) {
	player->leaveTeam();
}

static void refreshGamesCommand(Player::Ptr player, const std::vector<uint8_t>&, const std::string& dataAsString)
{
	player->send(S_GAME_LIST_ITEM, "1 " + player->server.getGameName());
	player->send(S_GAME_LIST_END);
}

static void selectGameCommand(Player::Ptr player, const std::vector<uint8_t>&, const std::string& dataAsString)
{
	std::vector<std::string> split = splitString(dataAsString, ' ');
	std::string& gameName = split[0];
	player->send(S_GAME_SEL_ACK, player->fromUtf8(player->name) + " " + gameName);
}

static void getLicenseCommand(Player::Ptr player, const std::vector<uint8_t>&, const std::string& dataAsString) {
	player->send(S_LICENSE, "ABCDEFGHI");
}

static void getExtraUserMem(Player::Ptr player, const std::vector<uint8_t>&, const std::string& dataAsString)
{
	std::vector<std::string> split = splitString(dataAsString, ' ');
	if (split.size() == 3)
	{
		std::string playerName = player->toUtf8(split[0]);
		int offset = atoi(split[1].c_str());
		int length = atoi(split[2].c_str());
		player->getExtraMem(playerName, offset, length);
		/* tetris
		uint8_t mem[] {
				0x52, 0x45, 0x47, 0x41, 0x54, 0x45, 0x54, 0x52, 0x49, 0x53, 0x20, 0x31, 0x2E, 0x30, 0x30, 0x00, // SEGATETRIS 1.00
				0x0C, 0x02, 0x02, 0x00, 0x01, 0x00, 0x04, 0x00, 0x02, 0x00, 0x00, 0x00
		};
		*/
	}
	else {
		player->disconnect();
	}
}

static void extraMemAck(Player::Ptr player, const std::vector<uint8_t>&, const std::string& dataAsString) {
	player->sendExtraMem();
}

static void registerExtraUserMemStart(Player::Ptr player, const std::vector<uint8_t>& data, const std::string&)
{
	if (data.size() == 8)
	{
		int offset = *(uint32_t *)&data[0];
		int length = *(uint16_t *)&data[4];
		player->startExtraMem(offset, length);
	}
}
static void registerExtraUserMemData(Player::Ptr player, const std::vector<uint8_t>&data, const std::string&) {
	player->setExtraMem(*(uint16_t *)&data[0], &data[2], data.size() - 2);
}
static void registerExtraUserMemEnd(Player::Ptr player, const std::vector<uint8_t>&, const std::string&) {
	player->endExtraMem();
}

static void chatLobbyCommand(Player::Ptr player, const std::vector<uint8_t>&, const std::string& dataAsString)
{
	auto pos = dataAsString.find(' ');
	if (pos == std::string::npos)
		return;
	std::string recipientName = player->toUtf8(dataAsString.substr(0, pos));
	std::string message = dataAsString.substr(pos + 1);
	if (!recipientName.empty() && recipientName[0] == '#')
	{
		// general lobby message
		if (player->lobby != nullptr)
			player->lobby->sendChat(player->name, player->toUtf8(message));
	}
	else
	{
		// private DM message
		Player::Ptr recipient = player->server.getPlayer(recipientName);
		if (recipient != nullptr)
			recipient->send(S_LOBBY_DM, recipient->fromUtf8(player->name) + " " + message);
		else
			WARN_LOG(player->gameId, "Unknown private lobby DM recipient: %s", recipientName.c_str());
	}
}

static void chatTeamCommand(Player::Ptr player, const std::vector<uint8_t>&, const std::string& dataAsString) {
	if (player->team != nullptr)
		player->team->sendChat(player->name, player->toUtf8(dataAsString));
}

static void sharedMemLobbyCommand(Player::Ptr player, const std::vector<uint8_t>&, const std::string& dataAsString) {
	if (player->lobby != nullptr)
		player->lobby->setSharedMem(dataAsString);
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
	player->send(S_PONG);
}

static void disconnectCommand(Player::Ptr player, const std::vector<uint8_t>&, const std::string&)
{
    player->send(0xE3);
    player->send(S_DISCONNECTED);
    player->disconnect(false);
}

static void reconnectCommand(Player::Ptr player, const std::vector<uint8_t>& data, const std::string&) {
	player->send(S_RECONNECT_ACK);
}

static void launchRequestCommand(Player::Ptr player, const std::vector<uint8_t>&, const std::string&) {
	if (player->team != nullptr && player->team->host == player)
		player->team->sendGameServer(player);
}

static void launchGameCommand(Player::Ptr player, const std::vector<uint8_t>&, const std::string&) {
	if (player->team != nullptr)
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
	// iwengine.dll:
	// 0	ignored
	// '*'	flags |= 0x20
	// AAAn	player name
	// 0 (int) player flags
	// 0	ignored
	// 0	ignored
	// Looks like the sharedMemData should start with 0 or 1 (byte) to indicate its presence so total shared mem len is 1 or 0x1e + 1
	// also expects 4 additional at the end -> Player.field_0x4c
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
		player->send(S_LOBBY_PLAYER_LIST_ITEM, test(i));
	player->send(S_LOBBY_PLAYER_LIST_END);
}

static void searchCommand(Player::Ptr player, const std::vector<uint8_t>&, const std::string& dataAsString)
{
	Player::Ptr found = player->server.getPlayer(player->toUtf8(dataAsString));
	if (found != nullptr) {
		sstream ss;
		ss << player->fromUtf8(found->name) << " !" << player->server.getName() << ' ';
		if (found->lobby != nullptr)
			ss << '!' << found->lobby->getSjisName();
		else
			ss << '#';
		player->send(S_SEARCH_RESULT, ss.str());
	}
	player->send(0xC9, "1");	// FIXME golf seems to think it's found, but garbage name(?)
								// FIXME search and say says failed to send message although the player is found (but self so might be the issue)
}

static void sendCTCPMessage(Player::Ptr player, const std::vector<uint8_t>&, const std::string& dataAsString)
{
	auto pos = dataAsString.find(' ');
	if (pos == std::string::npos)
		return;
	std::string recipientName = player->toUtf8(dataAsString.substr(0, pos));
	std::string message = dataAsString.substr(pos + 1);
	Player::Ptr recipient = player->server.getPlayer(recipientName);
	if (recipient == nullptr)
		return;
	// CTCP message used to challenge another player
	recipient->send(S_CTCP_MSG, message);
}

static void logData(Player::Ptr player, const std::vector<uint8_t>&, const std::string& dataAsString) {
	player->send(S_SENDLOG_ACK);
}

static void nullCommand(Player::Ptr, const std::vector<uint8_t>&, const std::string&) {
}

static void launchRequestSingle(Player::Ptr player, const std::vector<uint8_t>&, const std::string&)
{
	// expects: <player count> { [*]<player name> <ip addr> }...
	// * => host
	if (player->team != nullptr)
	{
		sstream ss;
		ss << player->team->members.size();
		for (Player::Ptr p : player->team->members)
		{
			if (p == player->team->host)
				ss << " *";
			else
				ss << ' ';
			ss << player->fromUtf8(p->name) << ' ' << p->getIp();
		}
		player->send(S_LAUNCH_ACK, ss.str());
	}
}

static void rjRequestRanking(Player::Ptr player, const std::vector<uint8_t>&, const std::string& dataAsString)
{
	// [RUNEJADE_RANKING 2 HANDLE_NAME MYNICK 0 30 SEGA_ID flycast1 0 40 9 DANJON_1 7 1 CHAT_1 7 1 ITEM_1 7 1 DANJON_2 7 1 CHAT_2 7 1 ITEM_2 7 1 DANJON_3 7 1 CHAT_3 7 1 ITEM_3 7 1 ]
	// <data name> <identifier#> { <name> <value> <?> <max sz?> } ... <data item#> { <name> <?> <?> } ...
	// expects: E5 <E6 msg count> <max items per msg>
	//          E6 <number>...
	//          E7
	// returned values should be [1-100], otherwise forced to 100
	// sending all ones makes you a king
	// Looks like the returned values are levels needed to reach higher status? not sure how it could depend on the player.
	std::vector<std::string> split = splitString(dataAsString, ' ');
	int count = atoi(split[10].c_str());
	if (count < 1)
		return;
	const std::string& item1 = split[11];
	if (item1.substr(0, 7) != "DANJON_")
		return;
	int level = atoi(item1.substr(7).c_str());
	if (level < 1 || level > 16)
		return;
	player->send(S_MULTI_DATA_START, "1 9");
	std::string s;
	// scale linearly up to ~100
	float unit = 100.f / 17.f;
	for (int i = level; i < level + count / 3; i++)	{
		std::string v = std::to_string((int)(i * unit)) + ' ';
		s += v + v + v;
	}
	player->send(S_MULTI_DATA_ITEM, s);
	player->send(S_MULTI_DATA_END);
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
		{ JOIN_TEAM_SPECTATOR, joinTeamSpecCommand },
		{ LEAVE_TEAM, leaveTeamCommand },
		{ LEAVE_TEAM_SPECTATOR, leaveTeamCommand },
		{ GET_EXTRAUSERMEM,  getExtraUserMem },
		{ REGIST_EXTRAUSERMEM_START, registerExtraUserMemStart },
		{ REGIST_EXTRAUSERMEM_TRANSFER, registerExtraUserMemData },
		{ REGIST_EXTRAUSERMEM_END, registerExtraUserMemEnd },
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
		{ SEND_LOG, logData },
		{ SEND_CTCPMSG, sendCTCPMessage },
		{ EXTRAUSERMEM_ACK, extraMemAck },
		{ LAUNCH_GAME_ACK, nullCommand },
		{ SHAREDMEM_LOBBY, sharedMemLobbyCommand },
		{ LAUNCH_REQUEST_SINGLE, launchRequestSingle },
		{ RJ_REQUEST_RANKING, rjRequestRanking },
};

void PacketProcessor::handlePacket(Player::Ptr player, uint16_t opcode, const std::vector<uint8_t>& payload)
{
	std::string payloadAsString = std::string((const char *)&payload[0], (const char *)&payload[payload.size()]);
	std::vector<std::string> split = splitString(payloadAsString, ' ');

	if (CommandHandlers.count((CLIOpcode)opcode) != 0)
		CommandHandlers[(CLIOpcode)opcode](player, payload, payloadAsString);
	else
		WARN_LOG(player->gameId, "Received unknown opcode: 0x%02x -> %s", opcode, payloadAsString.c_str());
}

