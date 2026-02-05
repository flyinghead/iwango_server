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
#include "database.h"
#include "common.h"
#include <cstdio>
#include <stdexcept>
#include <algorithm>

static std::string databasePath = "iwango.db";

void setDatabasePath(const std::string& databasePath)
{
	if (!databasePath.empty()) {
		::databasePath = databasePath;
		Database db(databasePath);
	}
}

//
// Gate server
//
bool createHandle(GameId gameId, const std::string& user, int index, const std::string& handle)
{
	try {
		Database db(databasePath);
		Statement stmt(db, "INSERT INTO USER_HANDLE (USER_NAME, GAME, HANDLE_INDEX, HANDLE) VALUES (?, ?, ?, ?)");
		stmt.bind(1, user);
		stmt.bind(2, (int)gameId);
		stmt.bind(3, index);
		stmt.bind(4, handle);
		stmt.step();

		return true;
	} catch (const UniqueConstraintViolation& e) {
		throw e;
	} catch (const std::runtime_error& e) {
		ERROR_LOG(gameId, "createHandle: %s", e.what());
		return false;
	}
}

bool replaceHandle(GameId gameId, const std::string& user, int index, const std::string& handle)
{
	try {
		Database db(databasePath);
		Statement stmt(db, "UPDATE USER_HANDLE SET HANDLE = ? WHERE USER_NAME = ? AND GAME = ? AND HANDLE_INDEX = ?");
		stmt.bind(1, handle);
		stmt.bind(2, user);
		stmt.bind(3, (int)gameId);
		stmt.bind(4, index);
		stmt.step();

		return true;
	} catch (const UniqueConstraintViolation& e) {
		throw e;
	} catch (const std::runtime_error& e) {
		ERROR_LOG(gameId, "replaceHandle: %s", e.what());
		return false;
	}
}

bool deleteHandle(GameId gameId, const std::string& user, int index)
{
	try {
		Database db(databasePath);
		db.exec("BEGIN TRANSACTION");
		{
			Statement stmt(db, "DELETE FROM USER_HANDLE WHERE USER_NAME = ? AND GAME = ? AND HANDLE_INDEX = ?");
			stmt.bind(1, user);
			stmt.bind(2, (int)gameId);
			stmt.bind(3, index);
			stmt.step();
		}
		for (int i = index + 1; i < 8; i++)
		{
			Statement stmt(db, "UPDATE USER_HANDLE SET HANDLE_INDEX = HANDLE_INDEX - 1 WHERE USER_NAME = ? AND GAME = ? AND HANDLE_INDEX = ?");
			stmt.bind(1, user);
			stmt.bind(2, (int)gameId);
			stmt.bind(3, i);
			stmt.step();
			if (stmt.changedRows() == 0)
				break;
		}
		db.exec("COMMIT");

		return true;
	} catch (const std::runtime_error& e) {
		ERROR_LOG(gameId, "deleteHandle: %s", e.what());
		return false;
	}
}

std::vector<std::string> getHandles(GameId gameId, const std::string& user, const std::string& defaultHandle)
{
	std::vector<std::string> handles;
	try {
		Database db(databasePath);
		Statement stmt(db, "SELECT HANDLE FROM USER_HANDLE WHERE USER_NAME = ? AND GAME = ? ORDER BY HANDLE_INDEX");
		stmt.bind(1, user);
		stmt.bind(2, (int)gameId);
		while (stmt.step())
			handles.push_back(stmt.getStringColumn(0));
		if (handles.empty() && !defaultHandle.empty()) {
			try {
				if (createHandle(gameId, user, 0, defaultHandle))
					handles.push_back(defaultHandle);
			} catch (const UniqueConstraintViolation&) {
			}
		}
	} catch (const std::runtime_error& e) {
		ERROR_LOG(gameId, "getHandles: %s", e.what());
	}
	return handles;
}

//
// Lobby server
//
void updateExtraUserMem(GameId gameId, const std::string& user, const uint8_t *data, int offset, int size)
{
	try {
		Database db(databasePath);
		std::vector<uint8_t> blob;
		bool newBlob = false;
		Statement stmt(db, "SELECT EXTRAMEM FROM USER_EXTRAMEM WHERE USER_NAME = ? AND GAME = ?");
		stmt.bind(1, user);
		stmt.bind(2, (int)gameId);
		if (stmt.step())
			blob = stmt.getBlobColumn(0);
		else
			newBlob = true;
		if ((int)blob.size() < offset + size)
			blob.resize(offset + size);
		memcpy(blob.data() + offset, data, size);
		if (newBlob)
		{
			Statement stmt(db, "INSERT INTO USER_EXTRAMEM (USER_NAME, GAME, EXTRAMEM) VALUES (?, ?, ?)");
			stmt.bind(1, user);
			stmt.bind(2, (int)gameId);
			stmt.bind(3, data, size);
			stmt.step();
		}
		else
		{
			Statement stmt(db, "UPDATE USER_EXTRAMEM SET EXTRAMEM = ? WHERE USER_NAME = ? AND GAME = ?");
			stmt.bind(1, data, size);
			stmt.bind(2, user);
			stmt.bind(3, (int)gameId);
			stmt.step();
		}
	} catch (const std::runtime_error& e) {
		ERROR_LOG(gameId, "updateExtraUserMem: %s", e.what());
	}
}

std::vector<uint8_t> getExtraUserMem(GameId gameId, const std::string& user)
{
	try {
		Database db(databasePath);
		Statement stmt(db, "SELECT EXTRAMEM FROM USER_EXTRAMEM WHERE USER_NAME = ? AND GAME = ?");
		stmt.bind(1, user);
		stmt.bind(2, (int)gameId);
		if (stmt.step())
			return stmt.getBlobColumn(0);
	} catch (const std::runtime_error& e) {
		ERROR_LOG(gameId, "getExtraUserMem: %s", e.what());
	}
	return {};
}
