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
#pragma once
#include "common.h"
#include <string>
#include <vector>

class AlreadyExistsException : public std::runtime_error
{
public:
	AlreadyExistsException() : std::runtime_error("This name already exists") {}
};

void setDatabasePath(const std::string& databasePath);
bool createHandle(GameId gameId, const std::string& user, int index, const std::string& handle);
bool replaceHandle(GameId gameId, const std::string& user, int index, const std::string& handle);
bool deleteHandle(GameId gameId, const std::string& user, int index);
std::vector<std::string> getHandles(GameId gameId, const std::string& user, const std::string& defaultHandle);

std::vector<uint8_t> getExtraUserMem(GameId gameId, const std::string& user);
void updateExtraUserMem(GameId gameId, const std::string& user, const uint8_t *data, int offset, int size);
