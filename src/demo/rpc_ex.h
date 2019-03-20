/*
 * rpctx.h
 *
 *  Created on: Sep 3, 2014
 *      Author: leo
 */

#ifndef RPC_EX_H_
#define RPC_EX_H_

#include <boost/assign/list_of.hpp>
#include "json/json_spirit_utils.h"
#include "json/json_spirit_value.h"

using namespace std;
using namespace boost;
using namespace boost::assign;
using namespace json_spirit;


extern Value getcontractscript(const Array& params, bool fHelp);

extern Value exportblockdata(const Array& params, bool fHelp);

//static const CRPCCommand vRPCCommands[] =
//{ //  name                      actor (function)         okSafeMode threadSafe reqWallet
#define  RPC_COMMANDS_EX \
        { "getcontractscript",      &getcontractscript,      true,      false,      true }, \
        { "exportblockdata",        &exportblockdata,        true,      false,      true },


#endif//RPC_EX_H_