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
#include <sqlite3.h>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <algorithm>

static std::string databasePath = "iwango.db";

void setDatabasePath(const std::string& databasePath) {
	if (!databasePath.empty())
		::databasePath = databasePath;
}

static void throwSqlError(sqlite3 *db)
{
	if (sqlite3_extended_errcode(db) == SQLITE_CONSTRAINT_UNIQUE)
		throw AlreadyExistsException();
	const char *msg = sqlite3_errmsg(db);
	if (msg != nullptr)
		throw std::runtime_error(msg);
	else
		throw std::runtime_error("SQL Error");
}

class Database
{
public:
	Database()
	{
		if (sqlite3_open(databasePath.c_str(), &db) != SQLITE_OK)
		{
			std::string what = "Can't open database " + databasePath + std::string(": ") + sqlite3_errmsg(db);
			sqlite3_close(db);
			throw std::runtime_error(what);
		}
		sqlite3_busy_timeout(db, 1000);
		/*
		sqlite3_trace_v2(db, SQLITE_TRACE_STMT, [](unsigned t, void *c, void *p, void *x) -> int {
			if (x == nullptr)
				return 0;
			char *stmt = (char *)x;
			if ((stmt[0] == '-' && stmt[1] == '-') || p == nullptr)
				printf("sql: %s\n", stmt);
			else
			{
				stmt = sqlite3_expanded_sql((sqlite3_stmt *)p);
				printf("sql: %s\n", stmt);
				sqlite3_free(stmt);
			}
			return 0;
		}, nullptr);
		*/
	}
	Database(const Database&) = delete;
	Database& operator=(const Database&) = delete;

	~Database() {
		sqlite3_close(db);
	}

	void exec(const std::string& stmt) {
		if (sqlite3_exec(db, stmt.c_str(), nullptr, 0, nullptr) != SQLITE_OK)
			throwSqlError(db);
	}

public:
	sqlite3 *db;
	friend class Statement;
};

class Statement
{
public:
	Statement(Database& database, const char *sql) : db(database.db) {
		if (sqlite3_prepare_v2(db, sql, -1, &stmt, 0) != SQLITE_OK)
			throwSqlError(db);
	}
	Statement(const Statement&) = delete;
	Statement& operator=(const Statement&) = delete;

	~Statement() {
		if (stmt != nullptr)
			sqlite3_finalize(stmt);
	}

	void bind(int idx, int v) {
		if (sqlite3_bind_int(stmt, idx, v) != SQLITE_OK)
			throwSqlError(db);
	}
	void bind(int idx, const std::string& s) {
		if (sqlite3_bind_text(stmt, idx, s.c_str(), s.length(), SQLITE_TRANSIENT) != SQLITE_OK)
			throwSqlError(db);
	}
	void bind(int idx, const uint8_t *data, size_t len) {
		if (sqlite3_bind_blob(stmt, idx, data, len, SQLITE_STATIC) != SQLITE_OK)
			throwSqlError(db);
	}

	bool step()
	{
		int rc = sqlite3_step(stmt);
		if (rc == SQLITE_ROW)
			return true;
		if (rc != SQLITE_DONE && rc != SQLITE_OK)
			throwSqlError(db);
		return false;
	}

	int getIntColumn(int idx) {
		return sqlite3_column_int(stmt, idx);
	}
	std::string getStringColumn(int idx) {
		return std::string((const char *)sqlite3_column_text(stmt, idx));
	}
	std::vector<uint8_t> getBlobColumn(int idx)
	{
		std::vector<uint8_t> blob;
		blob.resize(sqlite3_column_bytes(stmt, idx));
		if (!blob.empty())
			memcpy(blob.data(), sqlite3_column_blob(stmt, idx), blob.size());
		return blob;
	}

	int changedRows() {
		return sqlite3_changes(db);
	}

private:
	sqlite3 *db;
	sqlite3_stmt *stmt = nullptr;
};

//
// Gate server
//
bool createHandle(GameId gameId, const std::string& user, int index, const std::string& handle)
{
	try {
		Database db;
		Statement stmt(db, "INSERT INTO USER_HANDLE (USER_NAME, GAME, HANDLE_INDEX, HANDLE) VALUES (?, ?, ?, ?)");
		stmt.bind(1, user);
		stmt.bind(2, (int)gameId);
		stmt.bind(3, index);
		stmt.bind(4, handle);
		stmt.step();

		return true;
	} catch (const AlreadyExistsException& e) {
		throw e;
	} catch (const std::runtime_error& e) {
		fprintf(stderr, "ERROR: createHandle: %s\n", e.what());
		return false;
	}
}

bool replaceHandle(GameId gameId, const std::string& user, int index, const std::string& handle)
{
	try {
		Database db;
		Statement stmt(db, "UPDATE USER_HANDLE SET HANDLE = ? WHERE USER_NAME = ? AND GAME = ? AND HANDLE_INDEX = ?");
		stmt.bind(1, handle);
		stmt.bind(2, user);
		stmt.bind(3, (int)gameId);
		stmt.bind(4, index);
		stmt.step();

		return true;
	} catch (const AlreadyExistsException& e) {
		throw e;
	} catch (const std::runtime_error& e) {
		fprintf(stderr, "ERROR: replaceHandle: %s\n", e.what());
		return false;
	}
}

bool deleteHandle(GameId gameId, const std::string& user, int index)
{
	try {
		Database db;
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
		fprintf(stderr, "ERROR: deleteHandle: %s\n", e.what());
		return false;
	}
}

std::vector<std::string> getHandles(GameId gameId, const std::string& user, const std::string& defaultHandle)
{
	std::vector<std::string> handles;
	try {
		Database db;
		Statement stmt(db, "SELECT HANDLE FROM USER_HANDLE WHERE USER_NAME = ? AND GAME = ? ORDER BY HANDLE_INDEX");
		stmt.bind(1, user);
		stmt.bind(2, (int)gameId);
		while (stmt.step())
			handles.push_back(stmt.getStringColumn(0));
		if (handles.empty() && !defaultHandle.empty()) {
			try {
				if (createHandle(gameId, user, 0, defaultHandle))
					handles.push_back(defaultHandle);
			} catch (const AlreadyExistsException&) {
			}
		}
	} catch (const std::runtime_error& e) {
		fprintf(stderr, "ERROR: getHandles: %s\n", e.what());
	}
	return handles;
}

//
// Lobby server
//
void updateExtraUserMem(GameId gameId, const std::string& user, const uint8_t *data, int offset, int size)
{
	try {
		Database db;
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
		fprintf(stderr, "ERROR: updateExtraUserMem: %s\n", e.what());
	}
}

std::vector<uint8_t> getExtraUserMem(GameId gameId, const std::string& user)
{
	try {
		Database db;
		Statement stmt(db, "SELECT EXTRAMEM FROM USER_EXTRAMEM WHERE USER_NAME = ? AND GAME = ?");
		stmt.bind(1, user);
		stmt.bind(2, (int)gameId);
		if (stmt.step())
			return stmt.getBlobColumn(0);
	} catch (const std::runtime_error& e) {
		fprintf(stderr, "ERROR: getExtraUserMem: %s\n", e.what());
	}
	return {};
}
