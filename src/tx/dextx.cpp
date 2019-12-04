// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2017-2019 The WaykiChain Developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "dextx.h"

#include "config/configuration.h"
#include "main.h"

#include <algorithm>

using uint128_t = unsigned __int128;

///////////////////////////////////////////////////////////////////////////////
// class CDEXBuyLimitOrderTx

bool CDEXOrderBaseTx::CheckOrderAmountRange(CValidationState &state, const string &title,
                                          const TokenSymbol &symbol, const int64_t amount) {
    // TODO: should check the min amount of order by symbol
    static_assert(MIN_DEX_ORDER_AMOUNT < INT64_MAX, "minimum dex order amount out of range");
    if (amount < (int64_t)MIN_DEX_ORDER_AMOUNT)
        return state.DoS(100, ERRORMSG("%s amount is too small, symbol=%s, amount=%llu, min_amount=%llu",
                        title, symbol, amount, MIN_DEX_ORDER_AMOUNT), REJECT_INVALID, "order-amount-too-small");

    if (!CheckCoinRange(symbol, amount))
        return state.DoS(100, ERRORMSG("%s amount is out of range, symbol=%s, amount=%llu",
                        title, symbol, amount), REJECT_INVALID, "invalid-order-amount-range");

    return true;
}

bool CDEXOrderBaseTx::CheckOrderPriceRange(CValidationState &state, const string &title,
                          const TokenSymbol &coin_symbol, const TokenSymbol &asset_symbol,
                          const int64_t price) {
    // TODO: should check the price range??
    if (price <= 0)
        return state.DoS(100, ERRORMSG("%s price out of range,"
                        " coin_symbol=%s, asset_symbol=%s, price=%llu",
                        title, coin_symbol, asset_symbol, price),
                        REJECT_INVALID, "invalid-price-range");

    return true;
}

bool CDEXOrderBaseTx::CheckOrderSymbols(CValidationState &state, const string &title,
                          const TokenSymbol &coinSymbol, const TokenSymbol &assetSymbol) {
    if (coinSymbol.empty() || coinSymbol.size() > MAX_TOKEN_SYMBOL_LEN || kCoinTypeSet.count(coinSymbol) == 0) {
        return state.DoS(100, ERRORMSG("%s invalid order coin symbol=%s", title, coinSymbol),
                        REJECT_INVALID, "invalid-order-coin-symbol");
    }

    if (assetSymbol.empty() || assetSymbol.size() > MAX_TOKEN_SYMBOL_LEN || kCoinTypeSet.count(assetSymbol) == 0) {
        return state.DoS(100, ERRORMSG("%s invalid order asset symbol=%s", title, assetSymbol),
                        REJECT_INVALID, "invalid-order-asset-symbol");
    }

    if (kTradingPairSet.count(make_pair(assetSymbol, coinSymbol)) == 0) {
        return state.DoS(100, ERRORMSG("%s not support the trading pair! coin_symbol=%s, asset_symbol=%s",
            title, coinSymbol, assetSymbol), REJECT_INVALID, "invalid-trading-pair");
    }

    return true;
}

uint64_t CDEXOrderBaseTx::CalcCoinAmount(uint64_t assetAmount, const uint64_t price) {
    uint128_t coinAmount = assetAmount * (uint128_t)price / PRICE_BOOST;
    assert(coinAmount < ULLONG_MAX);
    return (uint64_t)coinAmount;
}

///////////////////////////////////////////////////////////////////////////////
// class CDEXBuyLimitOrderTx

string CDEXBuyLimitOrderTx::ToString(CAccountDBCache &accountCache) {
    return strprintf(
        "txType=%s, hash=%s, ver=%d, valid_height=%d, txUid=%s, llFees=%llu, "
        "coin_symbol=%s, asset_symbol=%s, amount=%llu, price=%llu",
        GetTxType(nTxType), GetHash().GetHex(), nVersion, valid_height, txUid.ToString(), llFees,
        coin_symbol, asset_symbol, asset_amount, bid_price);
}

Object CDEXBuyLimitOrderTx::ToJson(const CAccountDBCache &accountCache) const {
    Object result = CBaseTx::ToJson(accountCache);
    result.push_back(Pair("coin_symbol",      coin_symbol));
    result.push_back(Pair("asset_symbol",     asset_symbol));
    result.push_back(Pair("asset_amount",   asset_amount));
    result.push_back(Pair("price",          bid_price));
    return result;
}

bool CDEXBuyLimitOrderTx::CheckTx(CTxExecuteContext &context) {
    IMPLEMENT_DEFINE_CW_STATE;
    IMPLEMENT_DISABLE_TX_PRE_STABLE_COIN_RELEASE;
    IMPLEMENT_CHECK_TX_REGID_OR_PUBKEY(txUid.type());
    IMPLEMENT_CHECK_TX_FEE;

    if (!CheckOrderSymbols(state, "CDEXBuyLimitOrderTx::CheckTx,", coin_symbol, asset_symbol)) return false;

    if (!CheckOrderAmountRange(state, "CDEXBuyLimitOrderTx::CheckTx, asset,", asset_symbol, asset_amount)) return false;

    if (!CheckOrderPriceRange(state, "CDEXBuyLimitOrderTx::CheckTx,", coin_symbol, asset_symbol, bid_price)) return false;

    CAccount srcAccount;
    if (!cw.accountCache.GetAccount(txUid, srcAccount))
        return state.DoS(100, ERRORMSG("CDEXBuyLimitOrderTx::CheckTx, read account failed"), REJECT_INVALID,
                         "bad-getaccount");

    CPubKey pubKey = (txUid.type() == typeid(CPubKey) ? txUid.get<CPubKey>() : srcAccount.owner_pubkey);
    IMPLEMENT_CHECK_TX_SIGNATURE(pubKey);

    return true;
}

bool CDEXBuyLimitOrderTx::ExecuteTx(CTxExecuteContext &context) {
    CCacheWrapper &cw = *context.pCw; CValidationState &state = *context.pState;
    CAccount srcAccount;
    if (!cw.accountCache.GetAccount(txUid, srcAccount)) {
        return state.DoS(100, ERRORMSG("CDEXBuyLimitOrderTx::ExecuteTx, read source addr account info error"),
                         READ_ACCOUNT_FAIL, "bad-read-accountdb");
    }

    if (!GenerateRegID(context, srcAccount)) {
        return false;
    }

    if (!srcAccount.OperateBalance(fee_symbol, SUB_FREE, llFees)) {
        return state.DoS(100, ERRORMSG("CDEXBuyLimitOrderTx::ExecuteTx, account has insufficient funds"),
                         UPDATE_ACCOUNT_FAIL, "operate-minus-account-failed");
    }
    // should freeze user's coin for buying the asset
    uint64_t coinAmount = CalcCoinAmount(asset_amount, bid_price);

    if (!srcAccount.OperateBalance(coin_symbol, FREEZE, coinAmount)) {
        return state.DoS(100, ERRORMSG("CDEXBuyLimitOrderTx::ExecuteTx, account has insufficient funds"),
                         UPDATE_ACCOUNT_FAIL, "operate-dex-order-account-failed");
    }

    if (!cw.accountCache.SetAccount(CUserID(srcAccount.keyid), srcAccount))
        return state.DoS(100, ERRORMSG("CDEXBuyLimitOrderTx::ExecuteTx, set account info error"),
                         WRITE_ACCOUNT_FAIL, "bad-write-accountdb");

    assert(!srcAccount.regid.IsEmpty());
    const uint256 &txid = GetHash();
    CDEXOrderDetail orderDetail;
    orderDetail.generate_type = USER_GEN_ORDER;
    orderDetail.order_type    = ORDER_LIMIT_PRICE;
    orderDetail.order_side    = ORDER_BUY;
    orderDetail.coin_symbol   = coin_symbol;
    orderDetail.asset_symbol  = asset_symbol;
    orderDetail.coin_amount   = CalcCoinAmount(asset_amount, bid_price);
    orderDetail.asset_amount  = asset_amount;
    orderDetail.price         = bid_price;
    orderDetail.tx_cord       = CTxCord(context.height, context.index);
    orderDetail.user_regid    = srcAccount.regid;
    // other fields keep the default value

    if (!cw.dexCache.CreateActiveOrder(txid, orderDetail))
        return state.DoS(100, ERRORMSG("CDEXBuyLimitOrderTx::ExecuteTx, create active buy order failed"),
                         WRITE_ACCOUNT_FAIL, "bad-write-dexdb");

    return true;
}

///////////////////////////////////////////////////////////////////////////////
// class CDEXSellLimitOrderTx

string CDEXSellLimitOrderTx::ToString(CAccountDBCache &accountCache) {
    return strprintf(
        "txType=%s, hash=%s, ver=%d, valid_height=%d, txUid=%s, llFees=%llu, "
        "coin_symbol=%s, asset_symbol=%s, amount=%llu, price=%llu",
        GetTxType(nTxType), GetHash().GetHex(), nVersion, valid_height, txUid.ToString(), llFees,
        coin_symbol, asset_symbol, asset_amount, ask_price);
}

Object CDEXSellLimitOrderTx::ToJson(const CAccountDBCache &accountCache) const {
    Object result = CBaseTx::ToJson(accountCache);
    result.push_back(Pair("coin_symbol",    coin_symbol));
    result.push_back(Pair("asset_symbol",   asset_symbol));
    result.push_back(Pair("asset_amount",   asset_amount));
    result.push_back(Pair("price",          ask_price));
    return result;
}

bool CDEXSellLimitOrderTx::CheckTx(CTxExecuteContext &context) {
    IMPLEMENT_DEFINE_CW_STATE;
    IMPLEMENT_DISABLE_TX_PRE_STABLE_COIN_RELEASE;
    IMPLEMENT_CHECK_TX_REGID_OR_PUBKEY(txUid.type());
    IMPLEMENT_CHECK_TX_FEE;

    if (!CheckOrderSymbols(state, "CDEXSellLimitOrderTx::CheckTx,", coin_symbol, asset_symbol)) return false;

    if (!CheckOrderAmountRange(state, "CDEXSellLimitOrderTx::CheckTx, asset,", asset_symbol, asset_amount)) return false;

    if (!CheckOrderPriceRange(state, "CDEXSellLimitOrderTx::CheckTx,", coin_symbol, asset_symbol, ask_price)) return false;

    CAccount srcAccount;
    if (!cw.accountCache.GetAccount(txUid, srcAccount))
        return state.DoS(100, ERRORMSG("CDEXSellLimitOrderTx::CheckTx, read account failed"), REJECT_INVALID,
                         "bad-getaccount");

    CPubKey pubKey = ( txUid.type() == typeid(CPubKey) ? txUid.get<CPubKey>() : srcAccount.owner_pubkey );
    IMPLEMENT_CHECK_TX_SIGNATURE(pubKey);

    return true;
}

bool CDEXSellLimitOrderTx::ExecuteTx(CTxExecuteContext &context) {
    CCacheWrapper &cw = *context.pCw; CValidationState &state = *context.pState;
    CAccount srcAccount;
    if (!cw.accountCache.GetAccount(txUid, srcAccount)) {
        return state.DoS(100, ERRORMSG("CDEXSellLimitOrderTx::ExecuteTx, read source addr account info error"),
                         READ_ACCOUNT_FAIL, "bad-read-accountdb");
    }

    if (!GenerateRegID(context, srcAccount)) {
        return false;
    }

    if (!srcAccount.OperateBalance(fee_symbol, SUB_FREE, llFees)) {
        return state.DoS(100, ERRORMSG("CDEXSellLimitOrderTx::ExecuteTx, account has insufficient funds"),
                         UPDATE_ACCOUNT_FAIL, "operate-minus-account-failed");
    }

    // freeze user's asset for selling.
    if (!srcAccount.OperateBalance(asset_symbol, FREEZE, asset_amount)) {
        return state.DoS(100, ERRORMSG("CDEXSellLimitOrderTx::ExecuteTx, account has insufficient funds"),
                         UPDATE_ACCOUNT_FAIL, "operate-dex-order-account-failed");
    }

    if (!cw.accountCache.SetAccount(CUserID(srcAccount.keyid), srcAccount))
        return state.DoS(100, ERRORMSG("CDEXSellLimitOrderTx::ExecuteTx, set account info error"),
                         WRITE_ACCOUNT_FAIL, "bad-write-accountdb");

    assert(!srcAccount.regid.IsEmpty());
    const uint256 &txid = GetHash();
    CDEXOrderDetail orderDetail;
    orderDetail.generate_type = USER_GEN_ORDER;
    orderDetail.order_type    = ORDER_LIMIT_PRICE;
    orderDetail.order_side    = ORDER_SELL;
    orderDetail.coin_symbol   = coin_symbol;
    orderDetail.asset_symbol  = asset_symbol;
    orderDetail.coin_amount   = CalcCoinAmount(asset_amount, ask_price);
    orderDetail.asset_amount  = asset_amount;
    orderDetail.price         = ask_price;
    orderDetail.tx_cord       = CTxCord(context.height, context.index);
    orderDetail.user_regid = srcAccount.regid;
    // other fields keep the default value

    if (!cw.dexCache.CreateActiveOrder(txid, orderDetail))
        return state.DoS(100, ERRORMSG("CDEXSellLimitOrderTx::ExecuteTx, create active sell order failed"),
                         WRITE_ACCOUNT_FAIL, "bad-write-dexdb");

    return true;
}


///////////////////////////////////////////////////////////////////////////////
// class CDEXBuyMarketOrderTx

string CDEXBuyMarketOrderTx::ToString(CAccountDBCache &accountCache) {
    return strprintf(
        "txType=%s, hash=%s, ver=%d, valid_height=%d, txUid=%s, llFees=%llu, "
        "coin_symbol=%s, asset_symbol=%s, amount=%llu",
        GetTxType(nTxType), GetHash().GetHex(), nVersion, valid_height, txUid.ToString(), llFees,
        coin_symbol, asset_symbol, coin_amount);
}

Object CDEXBuyMarketOrderTx::ToJson(const CAccountDBCache &accountCache) const {
    Object result = CBaseTx::ToJson(accountCache);
    result.push_back(Pair("coin_symbol",    coin_symbol));
    result.push_back(Pair("asset_symbol",   asset_symbol));
    result.push_back(Pair("coin_amount",    coin_amount));

    return result;
}

bool CDEXBuyMarketOrderTx::CheckTx(CTxExecuteContext &context) {
    IMPLEMENT_DEFINE_CW_STATE;
    IMPLEMENT_DISABLE_TX_PRE_STABLE_COIN_RELEASE;
    IMPLEMENT_CHECK_TX_REGID_OR_PUBKEY(txUid.type());
    IMPLEMENT_CHECK_TX_FEE;

    if (!CheckOrderSymbols(state, "CDEXBuyMarketOrderTx::CheckTx,", coin_symbol, asset_symbol)) return false;

    if (!CheckOrderAmountRange(state, "CDEXBuyMarketOrderTx::CheckTx, coin,", coin_symbol, coin_amount)) return false;

    CAccount srcAccount;
    if (!cw.accountCache.GetAccount(txUid, srcAccount))
        return state.DoS(100, ERRORMSG("CDEXBuyMarketOrderTx::CheckTx, read account failed"), REJECT_INVALID,
                         "bad-getaccount");

    CPubKey pubKey = (txUid.type() == typeid(CPubKey) ? txUid.get<CPubKey>() : srcAccount.owner_pubkey);
    IMPLEMENT_CHECK_TX_SIGNATURE(pubKey);

    return true;
}

bool CDEXBuyMarketOrderTx::ExecuteTx(CTxExecuteContext &context) {
    CCacheWrapper &cw = *context.pCw; CValidationState &state = *context.pState;
    CAccount srcAccount;
    if (!cw.accountCache.GetAccount(txUid, srcAccount)) {
        return state.DoS(100, ERRORMSG("CDEXBuyMarketOrderTx::ExecuteTx, read source addr account info error"),
                         READ_ACCOUNT_FAIL, "bad-read-accountdb");
    }

    if (!GenerateRegID(context, srcAccount)) {
        return false;
    }

    if (!srcAccount.OperateBalance(fee_symbol, SUB_FREE, llFees)) {
        return state.DoS(100, ERRORMSG("CDEXBuyMarketOrderTx::ExecuteTx, account has insufficient funds"),
                         UPDATE_ACCOUNT_FAIL, "operate-minus-account-failed");
    }
    // should freeze user's coin for buying the asset
    if (!srcAccount.OperateBalance(coin_symbol, FREEZE, coin_amount)) {
        return state.DoS(100, ERRORMSG("CDEXBuyMarketOrderTx::ExecuteTx, account has insufficient funds"),
                         UPDATE_ACCOUNT_FAIL, "operate-dex-order-account-failed");
    }

    if (!cw.accountCache.SetAccount(CUserID(srcAccount.keyid), srcAccount))
        return state.DoS(100, ERRORMSG("CDEXBuyMarketOrderTx::ExecuteTx, set account info error"),
                         WRITE_ACCOUNT_FAIL, "bad-write-accountdb");

    assert(!srcAccount.regid.IsEmpty());
    const uint256 &txid = GetHash();
    CDEXOrderDetail orderDetail;
    orderDetail.generate_type = USER_GEN_ORDER;
    orderDetail.order_type    = ORDER_MARKET_PRICE;
    orderDetail.order_side    = ORDER_BUY;
    orderDetail.coin_symbol   = coin_symbol;
    orderDetail.asset_symbol  = asset_symbol;
    orderDetail.coin_amount   = coin_amount;
    orderDetail.asset_amount  = 0; // unkown in buy market price order
    orderDetail.price         = 0; // unkown in buy market price order
    orderDetail.tx_cord       = CTxCord(context.height, context.index);
    orderDetail.user_regid = srcAccount.regid;
    // other fields keep the default value

    if (!cw.dexCache.CreateActiveOrder(txid, orderDetail)) {
        return state.DoS(100, ERRORMSG("CDEXBuyMarketOrderTx::ExecuteTx, create active buy order failed"),
                         WRITE_ACCOUNT_FAIL, "bad-write-dexdb");
    }

    return true;
}

///////////////////////////////////////////////////////////////////////////////
// class CDEXSellMarketOrderTx

string CDEXSellMarketOrderTx::ToString(CAccountDBCache &accountCache) {
    return strprintf(
        "txType=%s, hash=%s, ver=%d, valid_height=%d, txUid=%s, llFees=%llu, "
        "coin_symbol=%s, asset_symbol=%s, amount=%llu",
        GetTxType(nTxType), GetHash().GetHex(), nVersion, valid_height, txUid.ToString(), llFees,
        coin_symbol, asset_symbol, asset_amount);
}

Object CDEXSellMarketOrderTx::ToJson(const CAccountDBCache &accountCache) const {
    Object result = CBaseTx::ToJson(accountCache);
    result.push_back(Pair("coin_symbol",    coin_symbol));
    result.push_back(Pair("asset_symbol",   asset_symbol));
    result.push_back(Pair("asset_amount",   asset_amount));
    return result;
}

bool CDEXSellMarketOrderTx::CheckTx(CTxExecuteContext &context) {
    IMPLEMENT_DEFINE_CW_STATE;
    IMPLEMENT_DISABLE_TX_PRE_STABLE_COIN_RELEASE;
    IMPLEMENT_CHECK_TX_REGID_OR_PUBKEY(txUid.type());
    IMPLEMENT_CHECK_TX_FEE;

    if (!CheckOrderSymbols(state, "CDEXSellMarketOrderTx::CheckTx,", coin_symbol, asset_symbol))
        return false;

    if (!CheckOrderAmountRange(state, "CDEXBuyMarketOrderTx::CheckTx, asset,", asset_symbol, asset_amount))
        return false;

    CAccount srcAccount;
    if (!cw.accountCache.GetAccount(txUid, srcAccount))
        return state.DoS(100, ERRORMSG("CDEXSellMarketOrderTx::CheckTx, read account failed"), REJECT_INVALID,
                         "bad-getaccount");

    CPubKey pubKey = (txUid.type() == typeid(CPubKey) ? txUid.get<CPubKey>() : srcAccount.owner_pubkey);
    IMPLEMENT_CHECK_TX_SIGNATURE(pubKey);

    return true;
}

bool CDEXSellMarketOrderTx::ExecuteTx(CTxExecuteContext &context) {
    CCacheWrapper &cw = *context.pCw; CValidationState &state = *context.pState;
    CAccount srcAccount;
    if (!cw.accountCache.GetAccount(txUid, srcAccount)) {
        return state.DoS(100, ERRORMSG("CDEXSellMarketOrderTx::ExecuteTx, read source addr account info error"),
                         READ_ACCOUNT_FAIL, "bad-read-accountdb");
    }

    if (!GenerateRegID(context, srcAccount)) {
        return false;
    }

    if (!srcAccount.OperateBalance(fee_symbol, SUB_FREE, llFees)) {
        return state.DoS(100, ERRORMSG("CDEXSellMarketOrderTx::ExecuteTx, account has insufficient funds"),
                         UPDATE_ACCOUNT_FAIL, "operate-minus-account-failed");
    }
    // should freeze user's asset for selling
    if (!srcAccount.OperateBalance(asset_symbol, FREEZE, asset_amount)) {
        return state.DoS(100, ERRORMSG("CDEXSellMarketOrderTx::ExecuteTx, account has insufficient funds"),
                         UPDATE_ACCOUNT_FAIL, "operate-dex-order-account-failed");
    }

    if (!cw.accountCache.SetAccount(CUserID(srcAccount.keyid), srcAccount))
        return state.DoS(100, ERRORMSG("CDEXSellMarketOrderTx::ExecuteTx, set account info error"),
                         WRITE_ACCOUNT_FAIL, "bad-write-accountdb");


    assert(!srcAccount.regid.IsEmpty());
    const uint256 &txid = GetHash();
    CDEXOrderDetail orderDetail;
    orderDetail.generate_type = USER_GEN_ORDER;
    orderDetail.order_type    = ORDER_MARKET_PRICE;
    orderDetail.order_side    = ORDER_SELL;
    orderDetail.coin_symbol   = coin_symbol;
    orderDetail.asset_symbol  = asset_symbol;
    orderDetail.coin_amount   = 0; // unkown in sell market price order
    orderDetail.asset_amount  = asset_amount;
    orderDetail.price         = 0; // unkown in sell market price order
    orderDetail.tx_cord       = CTxCord(context.height, context.index);
    orderDetail.user_regid    = srcAccount.regid;
    // other fields keep the default value

    if (!cw.dexCache.CreateActiveOrder(txid, orderDetail)) {
        return state.DoS(100, ERRORMSG("CDEXSellMarketOrderTx::ExecuteTx, create active sell order failed"),
                         WRITE_ACCOUNT_FAIL, "bad-write-dexdb");
    }

    return true;
}

///////////////////////////////////////////////////////////////////////////////
// class CDEXCancelOrderTx

string CDEXCancelOrderTx::ToString(CAccountDBCache &accountCache) {
    return strprintf(
        "txType=%s, hash=%s, ver=%d, valid_height=%d, txUid=%s, llFees=%llu, orderId=%s",
        GetTxType(nTxType), GetHash().GetHex(), nVersion, valid_height, txUid.ToString(), llFees,
        orderId.GetHex());
}

Object CDEXCancelOrderTx::ToJson(const CAccountDBCache &accountCache) const {
    Object result = CBaseTx::ToJson(accountCache);
    result.push_back(Pair("order_id", orderId.GetHex()));

    return result;
}

bool CDEXCancelOrderTx::CheckTx(CTxExecuteContext &context) {
    IMPLEMENT_DEFINE_CW_STATE;
    IMPLEMENT_DISABLE_TX_PRE_STABLE_COIN_RELEASE;
    IMPLEMENT_CHECK_TX_REGID_OR_PUBKEY(txUid.type());
    IMPLEMENT_CHECK_TX_FEE;

    if (orderId.IsEmpty())
        return state.DoS(100, ERRORMSG("CDEXCancelOrderTx::CheckTx, order_id is empty"), REJECT_INVALID,
                         "invalid-order-id");
    CAccount srcAccount;
    if (!cw.accountCache.GetAccount(txUid, srcAccount))
        return state.DoS(100, ERRORMSG("CDEXCancelOrderTx::CheckTx, read account failed"), REJECT_INVALID,
                         "bad-getaccount");

    CPubKey pubKey = (txUid.type() == typeid(CPubKey) ? txUid.get<CPubKey>() : srcAccount.owner_pubkey);
    IMPLEMENT_CHECK_TX_SIGNATURE(pubKey);

    return true;
}

bool CDEXCancelOrderTx::ExecuteTx(CTxExecuteContext &context) {
    CCacheWrapper &cw       = *context.pCw;
    CValidationState &state = *context.pState;

    CAccount srcAccount;
    if (!cw.accountCache.GetAccount(txUid, srcAccount)) {
        return state.DoS(100, ERRORMSG("CDEXCancelOrderTx::ExecuteTx, read source addr account info error"),
                         READ_ACCOUNT_FAIL, "bad-read-accountdb");
    }

    if (!GenerateRegID(context, srcAccount)) {
        return false;
    }

    if (!srcAccount.OperateBalance(fee_symbol, SUB_FREE, llFees)) {
        return state.DoS(100, ERRORMSG("CDEXCancelOrderTx::ExecuteTx, account has insufficient funds"),
                         UPDATE_ACCOUNT_FAIL, "operate-minus-account-failed");
    }

    CDEXOrderDetail activeOrder;
    if (!cw.dexCache.GetActiveOrder(orderId, activeOrder)) {
        return state.DoS(100, ERRORMSG("CDEXCancelOrderTx::ExecuteTx, the order is inactive or not existed"),
                        REJECT_INVALID, "order-inactive");
    }

    if (activeOrder.generate_type != USER_GEN_ORDER) {
        return state.DoS(100, ERRORMSG("CDEXCancelOrderTx::ExecuteTx, the order is not generate by tx of user"),
                        REJECT_INVALID, "order-inactive");
    }

    if (srcAccount.regid.IsEmpty() || srcAccount.regid != activeOrder.user_regid) {
        return state.DoS(100, ERRORMSG("CDEXCancelOrderTx::ExecuteTx, can not cancel other user's order tx"),
                        REJECT_INVALID, "user-unmatched");
    }

    // get frozen money
    vector<CReceipt> receipts;
    TokenSymbol frozenSymbol;
    uint64_t frozenAmount = 0;
    if (activeOrder.order_side == ORDER_BUY) {
        frozenSymbol = activeOrder.coin_symbol;
        frozenAmount = activeOrder.coin_amount - activeOrder.total_deal_coin_amount;

        receipts.emplace_back(CUserID::NULL_ID, activeOrder.user_regid, frozenSymbol, frozenAmount,
                              ReceiptCode::DEX_UNFREEZE_COIN_TO_BUYER);
    } else if(activeOrder.order_side == ORDER_SELL) {
        frozenSymbol = activeOrder.asset_symbol;
        frozenAmount = activeOrder.asset_amount - activeOrder.total_deal_asset_amount;

        receipts.emplace_back(CUserID::NULL_ID, activeOrder.user_regid, frozenSymbol, frozenAmount,
                              ReceiptCode::DEX_UNFREEZE_ASSET_TO_SELLER);
    } else {
        assert(false && "Order side must be ORDER_BUY|ORDER_SELL");
    }

    if (!srcAccount.OperateBalance(frozenSymbol, UNFREEZE, frozenAmount)) {
        return state.DoS(100, ERRORMSG("CDEXCancelOrderTx::ExecuteTx, account has insufficient frozen amount to unfreeze"),
                         UPDATE_ACCOUNT_FAIL, "unfreeze-account-coin");
    }

    if (!cw.accountCache.SetAccount(CUserID(srcAccount.keyid), srcAccount))
        return state.DoS(100, ERRORMSG("CDEXCancelOrderTx::ExecuteTx, set account info error"),
                         WRITE_ACCOUNT_FAIL, "bad-write-accountdb");

    if (!cw.dexCache.EraseActiveOrder(orderId, activeOrder)) {
        return state.DoS(100, ERRORMSG("CDEXCancelOrderTx::ExecuteTx, erase active order failed! order_id=%s", orderId.ToString()),
                         REJECT_INVALID, "order-erase-failed");
    }

    if (!cw.txReceiptCache.SetTxReceipts(GetHash(), receipts))
        return state.DoS(100, ERRORMSG("CDEXCancelOrderTx::ExecuteTx, write tx receipt failed! txid=%s", GetHash().ToString()),
                         REJECT_INVALID, "write-tx-receipt-failed");

    return true;
}


///////////////////////////////////////////////////////////////////////////////
// class DEXDealItem
string DEXDealItem::ToString() const {
    return strprintf(
        "buy_order_id=%s, sell_order_id=%s, price=%llu, coin_amount=%llu, asset_amount=%llu",
        buyOrderId.ToString(), sellOrderId.ToString(), dealPrice, dealCoinAmount, dealAssetAmount);
}

///////////////////////////////////////////////////////////////////////////////
// class CDEXSettleTx
string CDEXSettleTx::ToString(CAccountDBCache &accountCache) {
    string dealInfo="";
    for (const auto &item : dealItems) {
        dealInfo += "{" + item.ToString() + "},";
    }

    return strprintf(
        "txType=%s, hash=%s, ver=%d, valid_height=%d, txUid=%s, llFees=%llu, deal_items=[%s]",
        GetTxType(nTxType), GetHash().GetHex(), nVersion, valid_height, txUid.ToString(), llFees,
        dealInfo);
}

Object CDEXSettleTx::ToJson(const CAccountDBCache &accountCache) const {
    Array arrayItems;
    for (const auto &item : dealItems) {
        Object subItem;
        subItem.push_back(Pair("buy_order_id",      item.buyOrderId.GetHex()));
        subItem.push_back(Pair("sell_order_id",     item.sellOrderId.GetHex()));
        subItem.push_back(Pair("coin_amount",       item.dealCoinAmount));
        subItem.push_back(Pair("asset_amount",      item.dealAssetAmount));
        subItem.push_back(Pair("price",             item.dealPrice));
        arrayItems.push_back(subItem);
    }

    Object result = CBaseTx::ToJson(accountCache);
    result.push_back(Pair("deal_items",  arrayItems));

    return result;
}

bool CDEXSettleTx::CheckTx(CTxExecuteContext &context) {
    IMPLEMENT_DEFINE_CW_STATE;
    IMPLEMENT_DISABLE_TX_PRE_STABLE_COIN_RELEASE;
    IMPLEMENT_CHECK_TX_REGID(txUid.type());
    IMPLEMENT_CHECK_TX_FEE;

    if (txUid.get<CRegID>() != SysCfg().GetDexMatchSvcRegId()) {
        return state.DoS(100, ERRORMSG("CDEXSettleTx::CheckTx, account regId is not authorized dex match-svc regId"),
                         REJECT_INVALID, "unauthorized-settle-account");
    }

    if (dealItems.empty() || dealItems.size() > MAX_SETTLE_ITEM_COUNT)
        return state.DoS(100, ERRORMSG("CDEXSettleTx::CheckTx, deal items is empty or count=%d is too large than %d",
            dealItems.size(), MAX_SETTLE_ITEM_COUNT), REJECT_INVALID, "invalid-deal-items");

    for (size_t i = 0; i < dealItems.size(); i++) {
        const DEXDealItem & dealItem = dealItems.at(i);
        if (dealItem.buyOrderId.IsEmpty() || dealItem.sellOrderId.IsEmpty())
            return state.DoS(100, ERRORMSG("CDEXSettleTx::CheckTx, deal_items[%d], buy_order_id or sell_order_id is empty",
                i), REJECT_INVALID, "invalid-deal-item");
        if (dealItem.buyOrderId == dealItem.sellOrderId)
            return state.DoS(100, ERRORMSG("CDEXSettleTx::CheckTx, deal_items[%d], buy_order_id cannot equal to sell_order_id",
                i), REJECT_INVALID, "invalid-deal-item");
        if (dealItem.dealCoinAmount == 0 || dealItem.dealAssetAmount == 0 || dealItem.dealPrice == 0)
            return state.DoS(100, ERRORMSG("CDEXSettleTx::CheckTx, deal_items[%d],"
                " deal_coin_amount or deal_asset_amount or deal_price is zero",
                i), REJECT_INVALID, "invalid-deal-item");
    }

    CAccount srcAccount;
    if (!cw.accountCache.GetAccount(txUid, srcAccount))
        return state.DoS(100, ERRORMSG("CDEXSettleTx::CheckTx, read account failed"), REJECT_INVALID,
                         "bad-getaccount");
    if (txUid.type() == typeid(CRegID) && !srcAccount.HaveOwnerPubKey())
        return state.DoS(100, ERRORMSG("CDEXSettleTx::CheckTx, account unregistered"),
                         REJECT_INVALID, "bad-account-unregistered");

    IMPLEMENT_CHECK_TX_SIGNATURE(srcAccount.owner_pubkey);

    return true;
}


/* process flow for settle tx
1. get and check buyDealOrder and sellDealOrder
    a. get and check active order from db
    b. get and check order detail
        I. if order is USER_GEN_ORDER:
            step 1. get and check order tx object from block file
            step 2. get order detail from tx object
        II. if order is SYS_GEN_ORDER:
            step 1. get sys order object from dex db
            step 2. get order detail from sys order object
2. get account of order's owner
    a. get buyOderAccount from account db
    b. get sellOderAccount from account db
3. check coin type match
    buyOrder.coin_symbol == sellOrder.coin_symbol
4. check asset type match
    buyOrder.asset_symbol == sellOrder.asset_symbol
5. check price match
    a. limit type <-> limit type
        I.   dealPrice <= buyOrder.price
        II.  dealPrice >= sellOrder.price
    b. limit type <-> market type
        I.   dealPrice == buyOrder.price
    c. market type <-> limit type
        I.   dealPrice == sellOrder.price
    d. market type <-> market type
        no limit
6. check and operate deal amount
    a. check: dealCoinAmount == CalcCoinAmount(dealAssetAmount, price)
    b. else check: (dealCoinAmount / 10000) == (CalcCoinAmount(dealAssetAmount, price) / 10000)
    c. operate total deal:
        buyActiveOrder.total_deal_coin_amount  += dealCoinAmount
        buyActiveOrder.total_deal_asset_amount += dealAssetAmount
        sellActiveOrder.total_deal_coin_amount  += dealCoinAmount
        sellActiveOrder.total_deal_asset_amount += dealAssetAmount
7. check the order limit amount and get residual amount
    a. buy order
        if market price order {
            limitCoinAmount = buyOrder.coin_amount
            check: limitCoinAmount >= buyActiveOrder.total_deal_coin_amount
            residualAmount = limitCoinAmount - buyActiveOrder.total_deal_coin_amount
        } else { //limit price order
            limitAssetAmount = buyOrder.asset_amount
            check: limitAssetAmount >= buyActiveOrder.total_deal_asset_amount
            residualAmount = limitAssetAmount - buyActiveOrder.total_deal_asset_amount
        }
    b. sell order
        limitAssetAmount = sellOrder.limitAmount
        check: limitAssetAmount >= sellActiveOrder.total_deal_asset_amount
        residualAmount = limitAssetAmount - dealCoinAmount

8. subtract the buyer's coins and seller's assets
    a. buyerFrozenCoins     -= dealCoinAmount
    b. sellerFrozenAssets   -= dealAssetAmount
9. calc deal fees
    buyerReceivedAssets = dealAssetAmount
    if buy order is USER_GEN_ORDER
        dealAssetFee = dealAssetAmount * 0.04%
        buyerReceivedAssets -= dealAssetFee
        add dealAssetFee to totalFee of tx
    sellerReceivedAssets = dealCoinAmount
    if buy order is SYS_GEN_ORDER
        dealCoinFee = dealCoinAmount * 0.04%
        sellerReceivedCoins -= dealCoinFee
        add dealCoinFee to totalFee of tx
10. add the buyer's assets and seller's coins
    a. buyerAssets          += dealAssetAmount - dealAssetFee
    b. sellerCoins          += dealCoinAmount - dealCoinFee
11. check order fullfiled or save residual amount
    a. buy order
        if buy order is fulfilled {
            if buy limit order {
                residualCoinAmount = buyOrder.coin_amount - buyActiveOrder.total_deal_coin_amount
                if residualCoinAmount > 0 {
                    buyerUnfreeze(residualCoinAmount)
                }
            }
            erase active order from dex db
        } else {
            update active order to dex db
        }
    a. sell order
        if sell order is fulfilled {
            erase active order from dex db
        } else {
            update active order to dex db
        }
*/
bool CDEXSettleTx::ExecuteTx(CTxExecuteContext &context) {
    CCacheWrapper &cw = *context.pCw; CValidationState &state = *context.pState;
    vector<CReceipt> receipts;

    shared_ptr<CAccount> pSrcAccount = make_shared<CAccount>();
   if (!cw.accountCache.GetAccount(txUid, *pSrcAccount)) {
        return state.DoS(100, ERRORMSG("CDEXSettleTx::ExecuteTx, read source addr account info error"),
                         READ_ACCOUNT_FAIL, "bad-read-accountdb");
    }

    if (!pSrcAccount->OperateBalance(fee_symbol, SUB_FREE, llFees)) {
        return state.DoS(100, ERRORMSG("CDEXSettleTx::ExecuteTx, account has insufficient funds"),
                         UPDATE_ACCOUNT_FAIL, "operate-minus-account-failed");
    }

    map<CRegID, shared_ptr<CAccount>> accountMap = {
        {pSrcAccount->regid, pSrcAccount}
    };
    for (size_t i = 0; i < dealItems.size(); i++) {
        auto &dealItem = dealItems[i];
        //1. get and check buyDealOrder and sellDealOrder
        CDEXOrderDetail buyOrder, sellOrder;
        if (!GetDealOrder(cw, state, i, dealItem.buyOrderId, ORDER_BUY, buyOrder))
            return false;

        if (!GetDealOrder(cw, state, i, dealItem.sellOrderId, ORDER_SELL, sellOrder))
            return false;

        // 2. get account of order
        shared_ptr<CAccount> pBuyOrderAccount = nullptr;
        auto buyOrderAccountIt = accountMap.find(buyOrder.user_regid);
        if (buyOrderAccountIt != accountMap.end()) {
            pBuyOrderAccount = buyOrderAccountIt->second;
        } else {
            pBuyOrderAccount = make_shared<CAccount>();
            if (!cw.accountCache.GetAccount(buyOrder.user_regid, *pBuyOrderAccount)) {
                return state.DoS(100, ERRORMSG("%s(), i[%d] read buy order account info error! order_id=%s, regid=%s",
                    __FUNCTION__, i, dealItem.buyOrderId.ToString(), buyOrder.user_regid.ToString()),
                    READ_ACCOUNT_FAIL, "bad-read-accountdb");
            }
            accountMap[pBuyOrderAccount->regid] = pBuyOrderAccount;
        }

        shared_ptr<CAccount> pSellOrderAccount = nullptr;
        auto sellOrderAccountIt = accountMap.find(sellOrder.user_regid);
        if (sellOrderAccountIt != accountMap.end()) {
            pSellOrderAccount = sellOrderAccountIt->second;
        } else {
            pSellOrderAccount = make_shared<CAccount>();
            if (!cw.accountCache.GetAccount(sellOrder.user_regid, *pSellOrderAccount)) {
            return state.DoS(100, ERRORMSG("%s(), i[%d] read sell order account info error! order_id=%s, regid=%s",
                __FUNCTION__, i, dealItem.sellOrderId.ToString(), sellOrder.user_regid.ToString()),
                READ_ACCOUNT_FAIL, "bad-read-accountdb");
            }
            accountMap[pSellOrderAccount->regid] = pSellOrderAccount;
        }

        // 3. check coin type match
        if (buyOrder.coin_symbol != sellOrder.coin_symbol) {
            return state.DoS(100, ERRORMSG("%s(), i[%d] coin symbol unmatch! buyer coin_symbol=%s, " \
                "seller coin_symbol=%s", __FUNCTION__, i, buyOrder.coin_symbol, sellOrder.coin_symbol),
                REJECT_INVALID, "coin-symbol-unmatch");
        }
        // 4. check asset type match
        if (buyOrder.asset_symbol != sellOrder.asset_symbol) {
            return state.DoS(100, ERRORMSG("%s(), i[%d] asset symbol unmatch! buyer asset_symbol=%s, " \
                "seller asset_symbol=%s", __FUNCTION__, i, buyOrder.asset_symbol, sellOrder.asset_symbol),
                REJECT_INVALID, "asset-symbol-unmatch");
        }

        // 5. check price match
        if (buyOrder.order_type == ORDER_LIMIT_PRICE && sellOrder.order_type == ORDER_LIMIT_PRICE) {
            if ( buyOrder.price < dealItem.dealPrice
                || sellOrder.price > dealItem.dealPrice ) {
                return state.DoS(100, ERRORMSG("%s(), i[%d] the expected price unmatch! buyer limit price=%llu, "
                    "seller limit price=%llu, deal_price=%llu",
                    __FUNCTION__, i, buyOrder.price, sellOrder.price, dealItem.dealPrice),
                    REJECT_INVALID, "deal-price-unmatch");
            }
        } else if (buyOrder.order_type == ORDER_LIMIT_PRICE && sellOrder.order_type == ORDER_MARKET_PRICE) {
            if (dealItem.dealPrice != buyOrder.price) {
                return state.DoS(100, ERRORMSG("%s(), i[%d] the expected price unmatch! buyer limit price=%llu, "
                    "seller market price, deal_price=%llu",
                    __FUNCTION__, i, buyOrder.price, dealItem.dealPrice),
                    REJECT_INVALID, "deal-price-unmatch");
            }
        } else if (buyOrder.order_type == ORDER_MARKET_PRICE && sellOrder.order_type == ORDER_LIMIT_PRICE) {
            if (dealItem.dealPrice != sellOrder.price) {
                return state.DoS(100, ERRORMSG("%s(), i[%d] the expected price unmatch! buyer market price, "
                    "seller limit price=%llu, deal_price=%llu",
                    __FUNCTION__, i, sellOrder.price, dealItem.dealPrice),
                    REJECT_INVALID, "deal-price-unmatch");
            }
        } else {
            assert(buyOrder.order_type == ORDER_MARKET_PRICE && sellOrder.order_type == ORDER_MARKET_PRICE);
            // no limit
        }

        // 6. check and operate deal amount
        uint64_t calcCoinAmount = CDEXOrderBaseTx::CalcCoinAmount(dealItem.dealAssetAmount, dealItem.dealPrice);
        int64_t dealAmountDiff = calcCoinAmount - dealItem.dealCoinAmount;
        bool isCoinAmountMatch = false;
        if (buyOrder.order_type == ORDER_MARKET_PRICE) {
            isCoinAmountMatch = (std::abs(dealAmountDiff) <= std::max<int64_t>(1, (1 * dealItem.dealPrice / PRICE_BOOST)));
        } else {
            isCoinAmountMatch = (dealAmountDiff == 0);
        }
        if (!isCoinAmountMatch)
            return state.DoS(100, ERRORMSG("%s(), i[%d] the deal_coin_amount unmatch!"
                " deal_info={%s}, calcCoinAmount=%llu",
                __FUNCTION__, i, dealItem.ToString(), calcCoinAmount),
                REJECT_INVALID, "deal-coin-amount-unmatch");

        buyOrder.total_deal_coin_amount += dealItem.dealCoinAmount;
        buyOrder.total_deal_asset_amount += dealItem.dealAssetAmount;
        sellOrder.total_deal_coin_amount += dealItem.dealCoinAmount;
        sellOrder.total_deal_asset_amount += dealItem.dealAssetAmount;

        // 7. check the order amount limits and get residual amount
        uint64_t buyResidualAmount  = 0;
        uint64_t sellResidualAmount = 0;

        if (buyOrder.order_type == ORDER_MARKET_PRICE) {
            uint64_t limitCoinAmount = buyOrder.coin_amount;
            if (limitCoinAmount < buyOrder.total_deal_coin_amount) {
                return state.DoS(100, ERRORMSG( "%s(), i[%d] the total_deal_coin_amount=%llu exceed the buyer's "
                    "coin_amount=%llu", __FUNCTION__, i, buyOrder.total_deal_coin_amount, limitCoinAmount),
                    REJECT_INVALID, "buy-deal-coin-amount-exceeded");
            }

            buyResidualAmount = limitCoinAmount - buyOrder.total_deal_coin_amount;
        } else {
            uint64_t limitAssetAmount = buyOrder.asset_amount;
            if (limitAssetAmount < buyOrder.total_deal_asset_amount) {
                return state.DoS(
                    100,
                    ERRORMSG("%s(), i[%d] the total_deal_asset_amount=%llu exceed the "
                             "buyer's asset_amount=%llu",
                             __FUNCTION__, i, buyOrder.total_deal_asset_amount, limitAssetAmount),
                    REJECT_INVALID, "buy-deal-amount-exceeded");
            }
            buyResidualAmount = limitAssetAmount - buyOrder.total_deal_asset_amount;
        }

        {
            // get and check sell order residualAmount
            uint64_t limitAssetAmount = sellOrder.asset_amount;
            if (limitAssetAmount < sellOrder.total_deal_asset_amount) {
                return state.DoS(
                    100,
                    ERRORMSG("%s(), i[%d] the total_deal_asset_amount=%llu exceed the "
                             "seller's asset_amount=%llu",
                             __FUNCTION__, i, sellOrder.total_deal_asset_amount, limitAssetAmount),
                    REJECT_INVALID, "sell-deal-amount-exceeded");
            }
            sellResidualAmount = limitAssetAmount - sellOrder.total_deal_asset_amount;
        }

        // 8. subtract the buyer's coins and seller's assets
        // - unfree and subtract the coins from buyer account
        if (   !pBuyOrderAccount->OperateBalance(buyOrder.coin_symbol, UNFREEZE, dealItem.dealCoinAmount)
            || !pBuyOrderAccount->OperateBalance(buyOrder.coin_symbol, SUB_FREE, dealItem.dealCoinAmount)) {// - subtract buyer's coins
            return state.DoS(100, ERRORMSG("%s(), i[%d] subtract coins from buyer account failed!"
                " deal_info={%s}, coin_symbol=%s",
                __FUNCTION__, i, dealItem.ToString(), buyOrder.coin_symbol),
                REJECT_INVALID, "operate-account-failed");
        }
        // - unfree and subtract the assets from seller account
        if (   !pSellOrderAccount->OperateBalance(sellOrder.asset_symbol, UNFREEZE, dealItem.dealAssetAmount)
            || !pSellOrderAccount->OperateBalance(sellOrder.asset_symbol, SUB_FREE, dealItem.dealAssetAmount)) { // - subtract seller's assets
            return state.DoS(100, ERRORMSG("%s(), i[%d] subtract assets from seller account failed!"
                " deal_info={%s}, asset_symbol=%s",
                __FUNCTION__, i, dealItem.ToString(), sellOrder.asset_symbol),
                REJECT_INVALID, "operate-account-failed");
        }

        // 9. calc deal fees
        uint64_t dexDealFeeRatio;
        if (!cw.sysParamCache.GetParam(DEX_DEAL_FEE_RATIO, dexDealFeeRatio)) {
            return state.DoS(100, ERRORMSG("%s(), i[%d] read DEX_DEAL_FEE_RATIO error", __FUNCTION__, i),
                                READ_SYS_PARAM_FAIL, "read-sysparamdb-error");
        }
        uint64_t buyerReceivedAssets = dealItem.dealAssetAmount;
        // 9.1 buyer pay the fee from the received assets to settler
        if (buyOrder.generate_type == USER_GEN_ORDER) {

            uint64_t dealAssetFee = dealItem.dealAssetAmount * dexDealFeeRatio / RATIO_BOOST;
            buyerReceivedAssets = dealItem.dealAssetAmount - dealAssetFee;
            // pay asset fee from seller to settler
            if (!pSrcAccount->OperateBalance(buyOrder.asset_symbol, ADD_FREE, dealAssetFee)) {
                return state.DoS(100, ERRORMSG("%s(), i[%d] pay asset fee from buyer to settler failed!"
                    " deal_info={%s}, asset_symbol=%s, asset_fee=%llu",
                    __FUNCTION__, i, dealItem.ToString(), buyOrder.asset_symbol, dealAssetFee),
                    REJECT_INVALID, "operate-account-failed");
            }

            receipts.emplace_back(pBuyOrderAccount->regid, pSrcAccount->regid, buyOrder.asset_symbol,
                               dealAssetFee, ReceiptCode::DEX_ASSET_FEE_TO_SETTLER);
        }
        // 9.2 seller pay the fee from the received coins to settler
        uint64_t sellerReceivedCoins = dealItem.dealCoinAmount;
        if (sellOrder.generate_type == USER_GEN_ORDER) {
            uint64_t dealCoinFee = dealItem.dealCoinAmount * dexDealFeeRatio / RATIO_BOOST;
            sellerReceivedCoins = dealItem.dealCoinAmount - dealCoinFee;
            // pay coin fee from buyer to settler
            if (!pSrcAccount->OperateBalance(sellOrder.coin_symbol, ADD_FREE, dealCoinFee)) {
                return state.DoS(100, ERRORMSG("%s(), i[%d] pay coin fee from seller to settler failed!"
                    " deal_info={%s}, coin_symbol=%s, coin_fee=%llu",
                    __FUNCTION__, i, dealItem.ToString(), sellOrder.coin_symbol, dealCoinFee),
                    REJECT_INVALID, "operate-account-failed");
            }
            receipts.emplace_back(pSellOrderAccount->regid, pSrcAccount->regid, sellOrder.coin_symbol,
                                  dealCoinFee, ReceiptCode::DEX_COIN_FEE_TO_SETTLER);
        }

        // 10. add the buyer's assets and seller's coins
        if (   !pBuyOrderAccount->OperateBalance(buyOrder.asset_symbol, ADD_FREE, buyerReceivedAssets)    // + add buyer's assets
            || !pSellOrderAccount->OperateBalance(sellOrder.coin_symbol, ADD_FREE, sellerReceivedCoins)){ // + add seller's coin
            return state.DoS(100,ERRORMSG("%s(), i[%d] add assets to buyer or add coins to seller failed!"
                " deal_info={%s}, asset_symbol=%s, assets=%llu, coin_symbol=%s, coins=%llu",
                __FUNCTION__, i, dealItem.ToString(), buyOrder.asset_symbol,
                buyerReceivedAssets, sellOrder.coin_symbol, sellerReceivedCoins),
                REJECT_INVALID, "operate-account-failed");
        }
        receipts.emplace_back(pSellOrderAccount->regid, pBuyOrderAccount->regid, buyOrder.asset_symbol,
                              buyerReceivedAssets, ReceiptCode::DEX_ASSET_TO_BUYER);
        receipts.emplace_back(pBuyOrderAccount->regid, pSellOrderAccount->regid, buyOrder.coin_symbol,
                              sellerReceivedCoins, ReceiptCode::DEX_COIN_TO_SELLER);

        // 11. check order fullfiled or save residual amount
        if (buyResidualAmount == 0) { // buy order fulfilled
            if (buyOrder.order_type == ORDER_LIMIT_PRICE) {
                if (buyOrder.coin_amount > buyOrder.total_deal_coin_amount) {
                    uint64_t residualCoinAmount = buyOrder.coin_amount - buyOrder.total_deal_coin_amount;

                    if (!pBuyOrderAccount->OperateBalance(buyOrder.coin_symbol, UNFREEZE, residualCoinAmount)) {
                        return state.DoS(100, ERRORMSG("%s(), i[%d] unfreeze buyer's residual coins failed!"
                            " deal_info={%s}, coin_symbol=%s, residual_coins=%llu",
                            __FUNCTION__, i, dealItem.ToString(), buyOrder.coin_symbol, residualCoinAmount),
                            REJECT_INVALID, "operate-account-failed");
                    }
                } else {
                    assert(buyOrder.coin_amount == buyOrder.total_deal_coin_amount);
                }
            }
            // erase active order
            if (!cw.dexCache.EraseActiveOrder(dealItem.buyOrderId, buyOrder)) {
                return state.DoS(100, ERRORMSG("%s(), i[%d] finish the active buy order failed! deal_info={%s}",
                    __FUNCTION__, i, dealItem.ToString()),
                    REJECT_INVALID, "write-dexdb-failed");
            }
        } else {
            if (!cw.dexCache.UpdateActiveOrder(dealItem.buyOrderId, buyOrder)) {
                return state.DoS(100, ERRORMSG("%s(), i[%d] update active buy order failed! deal_info={%s}",
                    __FUNCTION__, i, dealItem.ToString()),
                    REJECT_INVALID, "write-dexdb-failed");
            }
        }

        if (sellResidualAmount == 0) { // sell order fulfilled
            // erase active order
            if (!cw.dexCache.EraseActiveOrder(dealItem.sellOrderId, sellOrder)) {
                return state.DoS(100, ERRORMSG("%s(), i[%d] finish active sell order failed! deal_info={%s}",
                    __FUNCTION__, i, dealItem.ToString()),
                    REJECT_INVALID, "write-dexdb-failed");
            }
        } else {
            if (!cw.dexCache.UpdateActiveOrder(dealItem.sellOrderId, sellOrder)) {
                return state.DoS(100, ERRORMSG("%s(), i[%d] update active sell order failed! deal_info={%s}",
                    __FUNCTION__, i, dealItem.ToString()),
                    REJECT_INVALID, "write-dexdb-failed");
            }
        }
    }

    // save accounts, include tx account
    for (auto accountItem : accountMap) {
        auto pAccount = accountItem.second;
        if (!cw.accountCache.SetAccount(pAccount->keyid, *pAccount))
            return state.DoS(100, ERRORMSG("CDEXSettleTx::ExecuteTx, set account info error! regid=%s, addr=%s",
                pAccount->regid.ToString(), pAccount->keyid.ToAddress()),
                WRITE_ACCOUNT_FAIL, "bad-write-accountdb");
    }

    if(!cw.txReceiptCache.SetTxReceipts(GetHash(), receipts))
        return state.DoS(100, ERRORMSG("CDEXSettleTx::ExecuteTx, set tx receipts failed!! txid=%s",
                        GetHash().ToString()), REJECT_INVALID, "set-tx-receipt-failed");
    return true;
}

bool CDEXSettleTx::GetDealOrder(CCacheWrapper &cw, CValidationState &state, uint32_t index, const uint256 &orderId,
                                const OrderSide orderSide, CDEXOrderDetail &dealOrder) {
    if (!cw.dexCache.GetActiveOrder(orderId, dealOrder))
        return state.DoS(100, ERRORMSG("CDEXSettleTx::GetDealOrder, get active order failed! i=%d, orderId=%s",
            index, orderId.ToString()), REJECT_INVALID,
            strprintf("get-active-order-failed, i=%d, order_id=%s", index, orderId.ToString()));

    if (dealOrder.order_side != orderSide)
        return state.DoS(100, ERRORMSG("CDEXSettleTx::GetDealOrder, expected order_side=%s "
                "but got order_side=%s! i=%d, orderId=%s", GetOrderSideName(orderSide),
                GetOrderSideName(dealOrder.order_side), index, orderId.ToString()),
                REJECT_INVALID,
                strprintf("order-side-unmatched, i=%d, order_id=%s", index, orderId.ToString()));

    return true;
}

////////////////////////////////////////////////////////////////////////////////
// ProcessAssetFee


static const string EXCHANGE_ACTION_REGISTER = "register";
static const string EXCHANGE_ACTION_UPDATE = "update";

static bool ProcessExchangeFee(CCacheWrapper &cw, CValidationState &state, const string &action,
    CAccount &txAccount, vector<CReceipt> &receipts) {

    uint64_t exchangeFee = 0;
    if (action == EXCHANGE_ACTION_REGISTER) {
        if (!cw.sysParamCache.GetParam(EXCHANGE_REGISTER_FEE, exchangeFee))
            return state.DoS(100, ERRORMSG("%s(), read param EXCHANGE_ACTION_ISSUE error", __func__),
                            REJECT_INVALID, "read-sysparam-error");
    } else {
        assert(action == EXCHANGE_ACTION_UPDATE);
        if (!cw.sysParamCache.GetParam(EXCHANGE_UPDATE_FEE, exchangeFee))
            return state.DoS(100, ERRORMSG("%s(), read param ASSET_UPDATE_FEE error", __func__),
                            REJECT_INVALID, "read-sysparam-error");
    }

    if (!txAccount.OperateBalance(SYMB::WICC, BalanceOpType::SUB_FREE, exchangeFee))
        return state.DoS(100, ERRORMSG("%s(), insufficient funds in tx account for exchange %s fee=%llu, tx_regid=%s",
                        __func__, action, exchangeFee, txAccount.regid.ToString()), UPDATE_ACCOUNT_FAIL, "insufficent-funds");

    uint64_t riskFee       = exchangeFee * ASSET_RISK_FEE_RATIO / RATIO_BOOST;
    uint64_t minerTotalFee = exchangeFee - riskFee;

    CAccount fcoinGenesisAccount;
    if (!cw.accountCache.GetFcoinGenesisAccount(fcoinGenesisAccount))
        return state.DoS(100, ERRORMSG("%s(), get risk riserve account failed", __func__),
                        READ_ACCOUNT_FAIL, "get-account-failed");

    if (!fcoinGenesisAccount.OperateBalance(SYMB::WICC, BalanceOpType::ADD_FREE, riskFee)) {
        return state.DoS(100, ERRORMSG("%s(), operate balance failed! add %s asset fee=%llu to risk riserve account error",
            __func__, action, riskFee), UPDATE_ACCOUNT_FAIL, "update-account-failed");
    }
    if (action == EXCHANGE_ACTION_REGISTER)
        receipts.emplace_back(txAccount.regid, fcoinGenesisAccount.regid, SYMB::WICC, riskFee, ReceiptCode::DEX_EXCHANGE_REG_FEE_TO_RISK);
    else
        receipts.emplace_back(txAccount.regid, fcoinGenesisAccount.regid, SYMB::WICC, riskFee, ReceiptCode::DEX_EXCHANGE_UPDATED_FEE_TO_RISK);

    if (!cw.accountCache.SetAccount(fcoinGenesisAccount.keyid, fcoinGenesisAccount))
        return state.DoS(100, ERRORMSG("%s(), write risk riserve account error, regid=%s",
            __func__, fcoinGenesisAccount.regid.ToString()), UPDATE_ACCOUNT_FAIL, "bad-read-accountdb");

    VoteDelegateVector delegates;
    if (!cw.delegateCache.GetActiveDelegates(delegates)) {
        return state.DoS(100, ERRORMSG("%s(), GetActiveDelegates failed", __func__),
            REJECT_INVALID, "get-delegates-failed");
    }
    assert(delegates.size() != 0 && delegates.size() == IniCfg().GetTotalDelegateNum());

    for (size_t i = 0; i < delegates.size(); i++) {
        const CRegID &delegateRegid = delegates[i].regid;
        CAccount delegateAccount;
        if (!cw.accountCache.GetAccount(CUserID(delegateRegid), delegateAccount)) {
            return state.DoS(100, ERRORMSG("%s(), get delegate account info failed! delegate regid=%s",
                __func__, delegateRegid.ToString()), UPDATE_ACCOUNT_FAIL, "bad-read-accountdb");
        }
        uint64_t minerUpdatedFee = minerTotalFee / delegates.size();
        if (i == 0) minerUpdatedFee += minerTotalFee % delegates.size(); // give the dust amount to topmost miner

        if (!delegateAccount.OperateBalance(SYMB::WICC, BalanceOpType::ADD_FREE, minerUpdatedFee)) {
            return state.DoS(100, ERRORMSG("%s(), add %s asset fee to miner failed, miner regid=%s",
                __func__, action, delegateRegid.ToString()), UPDATE_ACCOUNT_FAIL, "operate-account-failed");
        }

        if (!cw.accountCache.SetAccount(delegateRegid, delegateAccount))
            return state.DoS(100, ERRORMSG("%s(), write delegate account info error, delegate regid=%s",
                __func__, delegateRegid.ToString()), UPDATE_ACCOUNT_FAIL, "bad-read-accountdb");

        if (action == EXCHANGE_ACTION_REGISTER)
            receipts.emplace_back(txAccount.regid, delegateRegid, SYMB::WICC, minerUpdatedFee, ReceiptCode::DEX_EXCHANGE_REG_FEE_TO_MINER);
        else
            receipts.emplace_back(txAccount.regid, delegateRegid, SYMB::WICC, minerUpdatedFee, ReceiptCode::DEX_EXCHANGE_UPDATED_FEE_TO_MINER);
    }
    return true;
}

////////////////////////////////////////////////////////////////////////////////
// class CDEXExchangeRegisterTx

string CDEXExchangeRegisterTx::ToString(CAccountDBCache &accountCache) {

}

Object CDEXExchangeRegisterTx::ToJson(const CAccountDBCache &accountCache) const {

}

bool CDEXExchangeRegisterTx::CheckTx(CTxExecuteContext &context) {
    IMPLEMENT_DEFINE_CW_STATE;
    IMPLEMENT_DISABLE_TX_PRE_STABLE_COIN_RELEASE;
    IMPLEMENT_CHECK_TX_REGID(txUid.type());
    IMPLEMENT_CHECK_TX_FEE;

    if (!exchange.owner_uid.is<CRegID>()) {
        return state.DoS(100, ERRORMSG("%s, exchange owner_uid must be regid", __FUNCTION__), REJECT_INVALID,
            "owner-uid-type-error");
    }

    if (!exchange.match_uid.is<CRegID>()) {
        return state.DoS(100, ERRORMSG("%s, exchange match_uid must be regid", __FUNCTION__), REJECT_INVALID,
            "match-uid-type-error");
    }

    static const uint32_t MAX_DOMAIN_NAME_LEN = 100;
    if (exchange.domain_name.empty() || exchange.domain_name.size() > MAX_DOMAIN_NAME_LEN) {
        return state.DoS(100, ERRORMSG("CDEXExchangeRegisterTx::CheckTx, domain_name is empty or len=%d greater than %d",
            exchange.domain_name.size(), MAX_DOMAIN_NAME_LEN), REJECT_INVALID, "invalid-domain-name");
    }

    static const uint32_t MAX_MATCH_FEE_RATIO_TYPE = 100;
    static const uint64_t MAX_MATCH_FEE_RATIO_VALUE = 10000;

    for (auto item : exchange.match_fee_ratio_map) {
        if (item.first > MAX_MATCH_FEE_RATIO_TYPE) {
            return state.DoS(100, ERRORMSG("CDEXExchangeRegisterTx::CheckTx, match fee ratio_type=%d is greater than %d",
                item.first, MAX_MATCH_FEE_RATIO_TYPE), REJECT_INVALID, "invalid-match-fee-ratio-type");
        }

        if (item.second > MAX_MATCH_FEE_RATIO_VALUE)
            return state.DoS(100, ERRORMSG("CDEXExchangeRegisterTx::CheckTx, match fee ratio_vale=%d is greater than %d! ratio_type=%d",
                item.second, MAX_MATCH_FEE_RATIO_TYPE, item.first), REJECT_INVALID, "invalid-match-fee-ratio-value");
    }

    // if ((txUid.type() == typeid(CPubKey)) && !txUid.get<CPubKey>().IsFullyValid())
    //     return state.DoS(100, ERRORMSG("CDEXExchangeRegisterTx::CheckTx, public key is invalid"), REJECT_INVALID,
    //                      "bad-publickey");

    CAccount txAccount;
    if (!cw.accountCache.GetAccount(txUid, txAccount))
        return state.DoS(100, ERRORMSG("CDEXExchangeRegisterTx::CheckTx, read account failed! tx account not exist, txUid=%s",
                     txUid.ToDebugString()), REJECT_INVALID, "bad-getaccount");

    if (!txAccount.IsRegistered() || !txUid.get<CRegID>().IsMature(context.height))
        return state.DoS(100, ERRORMSG("CDEXExchangeRegisterTx::CheckTx, account unregistered or immature"),
                         REJECT_INVALID, "account-unregistered-or-immature");

    IMPLEMENT_CHECK_TX_SIGNATURE(txAccount.owner_pubkey);

    return true;
}
bool CDEXExchangeRegisterTx::ExecuteTx(CTxExecuteContext &context) {
    CCacheWrapper &cw = *context.pCw; CValidationState &state = *context.pState;
    vector<CReceipt> receipts;
    shared_ptr<CAccount> pTxAccount = make_shared<CAccount>();
    if (pTxAccount == nullptr || !cw.accountCache.GetAccount(txUid, *pTxAccount))
        return state.DoS(100, ERRORMSG("CDEXExchangeRegisterTx::ExecuteTx, read tx account by txUid=%s error",
            txUid.ToDebugString()), UPDATE_ACCOUNT_FAIL, "bad-read-accountdb");

    if (!pTxAccount->OperateBalance(fee_symbol, BalanceOpType::SUB_FREE, llFees)) {
        return state.DoS(100, ERRORMSG("CDEXExchangeRegisterTx::ExecuteTx, insufficient funds in account to sub fees, fees=%llu, txUid=%s",
                        llFees, txUid.ToDebugString()), UPDATE_ACCOUNT_FAIL, "insufficent-funds");
    }

    //TODO: make exchange_id


    // if (cw.assetCache.HaveAsset(asset.symbol))
    //     return state.DoS(100, ERRORMSG("CDEXExchangeRegisterTx::ExecuteTx, the asset has been issued! symbol=%s",
    //         asset.symbol), REJECT_INVALID, "asset-existed-error");

    shared_ptr<CAccount> pOwnerAccount;
    if (pTxAccount->IsMyUid(exchange.owner_uid)) {
        pOwnerAccount = pTxAccount;
    } else {
        pOwnerAccount = make_shared<CAccount>();
        if (pOwnerAccount == nullptr || !cw.accountCache.GetAccount(exchange.owner_uid, *pOwnerAccount))
            return state.DoS(100, ERRORMSG("CDEXExchangeRegisterTx::CheckTx, read owner account failed! owner_uid=%s",
                exchange.owner_uid.ToDebugString()), REJECT_INVALID, "owner-account-not-exist");
    }

    if (pOwnerAccount->regid.IsEmpty() || !pOwnerAccount->regid.IsMature(context.height)) {
        return state.DoS(100, ERRORMSG("CDEXExchangeRegisterTx::CheckTx, owner account is unregistered or immature! regid=%s",
            exchange.owner_uid.get<CRegID>().ToString()), REJECT_INVALID, "owner-account-unregistered-or-immature");
    }

    shared_ptr<CAccount> pMatchAccount;
    if (pTxAccount->IsMyUid(exchange.match_uid)) {
        pMatchAccount = pTxAccount;
    } else if (pOwnerAccount->IsMyUid(exchange.match_uid)) {
        pMatchAccount = pOwnerAccount;
    } else {
        pMatchAccount = make_shared<CAccount>();
        if (pMatchAccount == nullptr || !cw.accountCache.GetAccount(exchange.match_uid, *pMatchAccount))
            return state.DoS(100, ERRORMSG("CDEXExchangeRegisterTx::CheckTx, get match account failed! match_uid=%s",
                exchange.match_uid.ToDebugString()), REJECT_INVALID, "match-account-not-exist");
    }

    if (pMatchAccount->regid.IsEmpty() || !pMatchAccount->regid.IsMature(context.height)) {
        return state.DoS(100, ERRORMSG("CDEXExchangeRegisterTx::CheckTx, match account is unregistered or immature! regid=%s",
            exchange.match_uid.get<CRegID>().ToString()), REJECT_INVALID, "match-account-unregistered-or-immature");
    }

    // TODO: process asset fee
    if (!ProcessExchangeFee(cw, state, EXCHANGE_ACTION_REGISTER, *pTxAccount, receipts)) {
        return false;
    }

    // if (!pOwnerAccount->OperateBalance(asset.symbol, BalanceOpType::ADD_FREE, asset.total_supply)) {
    //     return state.DoS(100, ERRORMSG("CAssetIssueTx::ExecuteTx, fail to add total_supply to issued account! total_supply=%llu, txUid=%s",
    //                     asset.total_supply, txUid.ToDebugString()), UPDATE_ACCOUNT_FAIL, "insufficent-funds");
    // }

    if (!cw.accountCache.SetAccount(txUid, *pTxAccount))
        return state.DoS(100, ERRORMSG("CAssetIssueTx::ExecuteTx, set tx account to db failed! txUid=%s",
            txUid.ToDebugString()), UPDATE_ACCOUNT_FAIL, "bad-set-accountdb");

    if (pOwnerAccount != pTxAccount) {
         if (!cw.accountCache.SetAccount(pOwnerAccount->keyid, *pOwnerAccount))
            return state.DoS(100, ERRORMSG("CAssetIssueTx::ExecuteTx, set asset owner account to db failed! owner_uid=%s",
                asset.owner_uid.ToDebugString()), UPDATE_ACCOUNT_FAIL, "bad-set-accountdb");
    }
    CAsset savedAsset(&asset);
    savedAsset.owner_uid = pOwnerAccount->regid;
    if (!cw.assetCache.SaveAsset(savedAsset))
        return state.DoS(100, ERRORMSG("CAssetIssueTx::ExecuteTx, save asset failed! txUid=%s",
            txUid.ToDebugString()), UPDATE_ACCOUNT_FAIL, "save-asset-failed");

    if(!cw.txReceiptCache.SetTxReceipts(GetHash(), receipts))
        return state.DoS(100, ERRORMSG("CAssetIssueTx::ExecuteTx, set tx receipts failed!! txid=%s",
                        GetHash().ToString()), REJECT_INVALID, "set-tx-receipt-failed");
    return true;
}
