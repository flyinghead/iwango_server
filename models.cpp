#include "models.h"
#include "lobby_server.h"
#include "discord.h"
#include "database.h"

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
	teams.push_back(team);
	creator->team = team;
	INFO_LOG(creator->gameId, "%s created team %s", creator->name.c_str(), name.c_str());

	sstream ss;
	ss << creator->fromUtf8(name) << ' ' << creator->fromUtf8(creator->name) << ' ' << capacity << " 0 " << gameName;
	std::vector<std::string> playerNames;
	for (auto& p : members) {
		p->send(S_NEW_TEAM, ss.str());
		playerNames.push_back(p->name);
	}
	discordGameCreated(creator->gameId, creator->name, name, playerNames);

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

Player::Player(LobbyConnection::Ptr connection, LobbyServer& server)
	: sharedMem(0x1e), gameId(server.getGameId()), server(server), connection(connection)
{
	const asio::ip::address address = connection->getSocket().remote_endpoint().address();
	ipAddress = address.to_string();
	ipBytes = address.to_v4().to_bytes();
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
	if (team)
		team->sendSharedMemPlayer(shared_from_this(), sharedMem);
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
void Player::joinTeam(const std::string& name)
{
	if (lobby != nullptr)
	{
		Team::Ptr team = lobby->getTeam(name);
		if (team == nullptr) {
			WARN_LOG(gameId, "joinTeam: team %s not found", name.c_str());
			return; // TODO Some Error, team didn't exist
		}

		if (team->addPlayer(shared_from_this())) {
			this->team = team;
			INFO_LOG(gameId, "Player %s joined team %s", this->name.c_str(), team->name.c_str());
		}
	}
	else {
		WARN_LOG(gameId, "joinTeam: user %s not in any lobby", this->name.c_str());
	}
}
void Player::leaveTeam()
{
	if (lobby != nullptr && team != nullptr) {
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
	Player::Ptr player = server.getPlayer(playerName);
	if (player == nullptr) {
		WARN_LOG(gameId, "Player::getExtraMem: user %s not found", playerName.c_str());
		return;
	}
	if ((int)player->extraUserMem.size() < offset + length)
		player->extraUserMem.resize(offset + length);
	send(S_EXTUSER_MEM_START);
	for (uint16_t i = 0; length > 0 && offset < (int)player->extraUserMem.size(); i++)
	{
		int chunksz = std::min(length, 200);
		std::vector<uint8_t> payload(2 + chunksz);
		*(uint16_t *)&payload[0] = i;
		memcpy(&payload[2], &player->extraUserMem[offset], chunksz);
		length -= chunksz;
		offset += chunksz;
		send(S_EXTUSER_MEM_CHUNK, payload);
	}
	send(S_EXTUSER_MEM_END);
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
	return utf8ToSjis(str, gameId == GameId::GolfShiyouyo || gameId == GameId::CuldCept);
}
