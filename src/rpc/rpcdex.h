// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2017-2019 The WaykiChain Developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef RPC_DEX_H
#define RPC_DEX_H

#include "commons/json/json_spirit_utils.h"
#include "commons/json/json_spirit_value.h"

using namespace std;
using namespace json_spirit;

////////////////////////////////////////////////////////////////////////////////
// submit dex tx

extern Value submitdexbuylimitordertx(const Array& params, bool fHelp);
extern Value submitdexselllimitordertx(const Array& params, bool fHelp);
extern Value submitdexbuymarketordertx(const Array& params, bool fHelp);
extern Value submitdexsellmarketordertx(const Array& params, bool fHelp);
extern Value submitdexcancelordertx(const Array& params, bool fHelp);
extern Value submitdexsettletx(const Array& params, bool fHelp);

extern Value submitdexexchangeregistertx(const Array& params, bool fHelp);

////////////////////////////////////////////////////////////////////////////////
// query api
extern Value getdexorder(const Array& params, bool fHelp);
extern Value getdexorders(const Array& params, bool fHelp);
extern Value getdexsysorders(const Array& params, bool fHelp);

#endif /* RPC_DEX_H */
