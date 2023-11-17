#pragma once

#include <map>
#include "EDSDKTypes.h"

void clr_screen();
EdsInt32 getvalue();
EdsInt32 getstrings(std::string& cstr, EdsInt32 number);
void pause_return();
void ListAvailValue(std::map<EdsUInt32, const char*>);
