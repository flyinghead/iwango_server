#include "models.h"
#include "lobby_server.h"
#include "discord.h"
#include "database.h"
#include <dcserver/status.hpp>

std::vector<LobbyServer *> LobbyServer::servers;

void Lobby::addPlayer(Player::Ptr player)
{
	if (members.size() == capacity) {
		player->send(S_LOBBY_FULL);
		return;
	}
	// Only add player if not already there
	auto it = std::find_if(members.begin(), members.end(), [&player](const Player::Ptr& member) {
		return member->name == player->name;
	});
	if (it == members.end())
		members.push_back(player);

	// Confirm Join Lobby
	player->send(S_JOIN_LOBBY_ACK, getSjisName() + " " + player->fromUtf8(player->name));

	std::vector<std::string> playerNames;
	// Send player info to all members
	for (auto& p : members)
	{
		playerNames.push_back(p->name);
		if (p == player)
			continue;
		p->send(S_PLAYER_LIST_ITEM, player->getSendDataPacket());
	}
	discordLobbyJoined(player->gameId, player->name, name, playerNames);
}

void Lobby::removePlayer(Player::Ptr player)
{
	auto it = std::find(members.begin(), members.end(), player);
	if (it != members.end())
	{
		// Remove player from list
		members.erase(it);

		// Confirm Leave Lobby
		player->send(S_LEAVE_LOBBY_ACK);

		// Tell all members to remove the player
		for (auto& p : members)
			p->send(S_LOBBY_LEFT, p->fromUtf8(player->name));
		if (!permanent && members.empty())
			parent.deleteLobby(name);
	}
	else {
		WARN_LOG(player->gameId, "Player %s not found in lobby %s", player->name.c_str(), name.c_str());
	}
}

void Lobby::sendChat(const std::string& from, const std::string& message)
{
	INFO_LOG(parent.getGameId(), "%s lobby chat: %s", from.c_str(), message.c_str());
	for (auto& player : members)
		player->send(S_LOBBY_CHAT, player->fromUtf8(from) + " " + player->fromUtf8(message));
}

Team::Ptr Lobby::createTeam(Player::Ptr creator, const std::string& name, unsigned capacity, const std::string& type)
{
	Team::Ptr team = Team::create(shared_from_this(), name, capacity, creator);
	if (type == "SPECTATOR")
		team->flags = 2;
	teams.push_back(team);
	creator->team = team;
	INFO_LOG(creator->gameId, "%s created team %s", creator->name.c_str(), name.c_str());

	sstream ss;
	ss << creator->fromUtf8(name) << ' ' << creator->fromUtf8(creator->name) << ' ' << capacity << ' ' << team->flags << ' ' << gameName;
	std::vector<std::string> playerNames;
	for (auto& p : members) {
		p->send(S_NEW_TEAM, ss.str());
		playerNames.push_back(p->name);
	}
	discordGameCreated(creator->gameId, creator->name, name, playerNames);
	status::createGame(getDCNetGameId(creator->gameId));

	return team;
}
void Lobby::deleteTeam(Team::Ptr team)
{
	auto it = std::find(teams.begin(), teams.end(), team);
	if (it != teams.end())
		teams.erase(it);
	// Tell all members to remove team
	for (auto& p : members)
		p->send(S_TEAM_DELETED, p->fromUtf8(team->name));
	status::deleteGame(getDCNetGameId(parent.getGameId()));
	INFO_LOG(parent.getGameId(), "team %s deleted", team->name.c_str());
}

Team::Ptr Lobby::getTeam(const std::string& name)
{
	for (auto& team : teams)
		if (team->name == name)
			return team;
	return nullptr;
}

std::string Lobby::getSjisName() const {
	// Culdcept 2 uses some ascii characters in lobby names
	return utf8ToSjis(name, parent.getGameId() == GameId::GolfShiyouyo);
}

void Lobby::setSharedMem(const std::string& data)
{
	sharedMem = data;
	hasSharedMem = !data.empty();
	if (hasSharedMem)
	{
		// send shared mem to lobby members
		sstream ss;
		ss << getSjisName() << ' ' << data;
		for (auto& p : members)
			p->send(S_LOBBY_SHARED_MEM, ss.str());
	}
}

void Lobby::sendSharedMemPlayer(Player::Ptr owner, const std::vector<uint8_t>& data) {
	for (auto& player : members)
		player->send(S_PLAYER_SHARED_MEM, Packet::createSharedMemPacket(data, player->fromUtf8(owner->name)));
}

Player::Player(LobbyConnection::Ptr connection, LobbyServer& server)
	: sharedMem(0x1e), gameId(server.getGameId()), server(server), connection(connection)
{
	const asio::ip::address address = connection->getSocket().remote_endpoint().address();
	ipAddress = address.to_string();
	ipBytes = address.to_v4().to_bytes();
	port = connection->getSocket().remote_endpoint().port();
}

void Player::login(const std::string& name)
{
	this->name = name;
	extraUserMem = getExtraUserMem(gameId, name);
}

std::string Player::getIp() {
	return ipAddress;
}
std::array<uint8_t, 4> Player::getIpBytes() {
	return ipBytes;
}

void Player::disconnect(bool sendDCPacket)
{
	if (disconnected)
		return;
	disconnected = true;

	// Tell client to d/c if actually still connected
	if (sendDCPacket)
		send(S_DO_DISCONNECT);

	status::leave(getDCNetGameId(gameId), ipAddress, port, name);

	// Remove player from everything
	if (team) {
		team->removePlayer(shared_from_this());
		team.reset();
	}
	if (lobby) {
		lobby->removePlayer(shared_from_this());
		lobby.reset();
	}
	server.removePlayer(shared_from_this());

	// Close if need be
	if (connection)
		connection->close();
		// this might have been deleted at this point
}

void Player::setSharedMem(const std::vector<uint8_t>& data)
{
	if (data.size() != 0x1e) {
		WARN_LOG(gameId, "Invalid player sharedMem size: %zd. Ignored", data.size());
		return;
	}
	memcpy(sharedMem.data(), &data[0], data.size());
	if (lobby)
		lobby->sendSharedMemPlayer(shared_from_this(), sharedMem);
}

std::vector<uint8_t> Player::getSendDataPacket()
{
	sstream ss;
	if (lobby)
		ss << lobby->getSjisName() << ' ';
	else
		ss << "# ";
	if (team && team->host == shared_from_this())
		ss << '*';
	ss << fromUtf8(name) << ' ' << flags << ' ';
	if (team)
		ss << '*' << fromUtf8(team->name);
	else
		ss << '#';
	ss << " *" << server.getGameName();
	std::string strData = ss.str();

	std::vector<uint8_t> data(1 + strData.length() + 1 + sharedMem.size() + 4);
	size_t idx = 0;
	data[idx++] = strData.length();
	memcpy(&data[idx], &strData[0], strData.length());
	idx += strData.length();
	data[idx++] = 1;
	memcpy(&data[idx], &sharedMem[0], sharedMem.size());
	idx += sharedMem.size();
	memcpy(&data[idx], getIpBytes().data(), 4);

	return data;
}

void Player::createTeam(const std::string& name, unsigned capacity, const std::string& type)
{
	if (lobby == nullptr)
		return;
	if (lobby->getTeam(name) == nullptr) {
		Team::Ptr newTeam = lobby->createTeam(shared_from_this(), name, capacity, type);
		team = newTeam;
	}
	else  {
		WARN_LOG(gameId, "createTeam: team %s already exists", name.c_str());
		send(S_TEAM_NAME_EXISTS);
	}
}
void Player::joinTeam(const std::string& name, bool spectator)
{
	if (lobby != nullptr)
	{
		Team::Ptr team = lobby->getTeam(name);
		if (team == nullptr) {
			WARN_LOG(gameId, "joinTeam: team %s not found", name.c_str());
			return; // TODO Some Error, team didn't exist
		}

		if (team->addPlayer(shared_from_this(), spectator))
		{
			this->team = team;
			this->spectator = spectator;
			INFO_LOG(gameId, "Player %s joined team %s%s", this->name.c_str(), team->name.c_str(),
					spectator ? " as spectator" : "");
		}
	}
	else {
		WARN_LOG(gameId, "joinTeam: user %s not in any lobby", this->name.c_str());
	}
}
void Player::leaveTeam()
{
	if (lobby != nullptr && team != nullptr)
	{
		team->removePlayer(shared_from_this());
		INFO_LOG(gameId, "Player %s left team %s", this->name.c_str(), team->name.c_str());
		this->team = nullptr;
	}
	else {
		WARN_LOG(gameId, "leaveTeam: user %s not in any lobby or team", this->name.c_str());
	}
}

void Player::getExtraMem(const std::string& playerName, int offset, int length)
{
	extraMemPlayer = server.getPlayer(playerName);
	if (extraMemPlayer == nullptr) {
		WARN_LOG(gameId, "Player::getExtraMem: user %s not found", playerName.c_str());
		return;
	}
	if ((int)extraMemPlayer->extraUserMem.size() < offset + length)
		extraMemPlayer->extraUserMem.resize(offset + length);
	send(S_EXTUSER_MEM_START);
	extraMemOffset = offset;
	extraMemEnd = offset + length;
	extraMemChunkNum = 0;
}

void Player::sendExtraMem()
{
	if (extraMemPlayer == nullptr)
		return;
	if (extraMemOffset >= extraMemEnd || extraMemOffset >= (int)extraMemPlayer->extraUserMem.size()) {
		send(S_EXTUSER_MEM_END);
		extraMemPlayer = nullptr;
		return;
	}
	int chunksz = std::min(extraMemEnd - extraMemOffset, 200);
	std::vector<uint8_t> payload(2 + chunksz);
	*(uint16_t *)&payload[0] = extraMemChunkNum++;
	memcpy(&payload[2], &extraMemPlayer->extraUserMem[extraMemOffset], chunksz);
	extraMemOffset += chunksz;
	send(S_EXTUSER_MEM_CHUNK, payload);
}

void Player::startExtraMem(int offset, int length)
{
	assert(offset >= 0);
	assert(length > 0);
	assert(offset + length <= 0x2000);
	extraMemOffset = offset;
	extraMemEnd = offset + length;
	if (extraMemEnd >= (int)extraUserMem.size())
		extraUserMem.resize(extraMemEnd);
	send(S_EXTUSER_MEM_ACK);
}
void Player::setExtraMem(int index, const uint8_t *data, int size)
{
	if (extraMemEnd == 0)
		return;
	memcpy(extraUserMem.data() + extraMemOffset, data, size);
	updateExtraUserMem(gameId, name, data, extraMemOffset, size);
	extraMemOffset += size;
	if (extraMemOffset >= extraMemEnd)
		extraMemEnd = 0;
	send(S_EXTUSER_MEM_ACK);
}
void Player::endExtraMem()
{
	extraMemEnd = 0;
	send(S_EXTUSER_MEM_ACK);
}

int Player::send(uint16_t opcode, const uint8_t *payload, unsigned length)
{
	if (connection == nullptr) {
		WARN_LOG(gameId, "player %s has a null connection", name.c_str());
		return 0;
	}
	std::vector<uint8_t> data = makePacket(opcode, payload, length);
	connection->send(data);
	return data.size();
}

std::vector<uint8_t> Player::makePacket(uint16_t opcode, const uint8_t *payload, unsigned length)
{
	unsigned size = length + 2;
	std::vector<uint8_t> data(size + 2);
	// Size
	data[0] = size;
	data[1] = size >> 8;
	// Opcode
	data[2] = opcode;
	data[3] = opcode >> 8;
	// Payload
	memcpy(&data[4], payload, length);
	return data;
}

void Player::receive(uint16_t opcode, const std::vector<uint8_t> payload) {
	PacketProcessor::handlePacket(shared_from_this(), opcode, payload);
}

std::string Player::toUtf8(const std::string& str) const {
	return sjisToUtf8(str);
}

std::string Player::fromUtf8(const std::string& str) const {
	return utf8ToSjis(str, gameId == GameId::GolfShiyouyo || gameId == GameId::CuldCept || gameId == GameId::RuneJade);
}

bool Team::addPlayer(Player::Ptr player, bool spectator)
{
	if (!spectator && members.size() == capacity)
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

bool Team::removePlayer(Player::Ptr player)
{
	auto it = std::find(members.begin(), members.end(), player);
	if (it != members.end())
	{
		members.erase(it);

		// Change host
		if (host == player && !members.empty())
			host = members[0];

		// Send Packets
		// FIXME player->lobby is null! yes, lobby can be null, not sure how
		if (player->lobby != nullptr)
		{
			for (auto& p : player->lobby->members)
				p->send(S_TEAM_LEFT, p->fromUtf8(name + " " + player->name));
		}
		else
		{
			for (auto& p : members)
				p->send(S_TEAM_LEFT, p->fromUtf8(name + " " + player->name));
		}

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

void Team::sendGameServer(Player::Ptr p)
{
	for (auto& player : members)
		player->send(S_GAME_SERVER, "172.20.0.1 9510");	// not implemented
}

LobbyServer::LobbyServer(GameId gameId, const std::string& name)
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

	case GameId::PowerSmash:
		createLobby("BEGINNER", 100);
		createLobby("NORMAL", 100);
		createLobby("EXPERT", 100);
		createLobby("EVENT", 100);
		break;

	case GameId::YakyuuTeam:
		createLobby("YAKYUASO-1", 100);
		createLobby("YAKYUASO-2", 100);
		createLobby("YAKYUASO-3", 100);
		createLobby("YAKYUASO-4", 100);
		createLobby("YAKYUASO-5", 100);
		break;

	case GameId::RuneJade:
		// Rune Jade creates its own lobby
		break;

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

uint16_t LobbyServer::getIpPort() const
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
	case GameId::PowerSmash: return 9508;
	case GameId::YakyuuTeam: return 9509;
	case GameId::RuneJade: return 9510;
	default: assert(false); return 0;
	}
}

std::string LobbyServer::getGameName() const
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
	case GameId::PowerSmash: return "HDR-0113";
	case GameId::YakyuuTeam: return "HDR-0091";
	case GameId::RuneJade: return "RUNEJADE";
	default: assert(false); return "???";
	}
}
