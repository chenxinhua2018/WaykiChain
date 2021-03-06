// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2017-2019 The WaykiChain Developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "dextx.h"

#include "config/configuration.h"
#include "main.h"

#include <algorithm>

using uint128_t = unsigned __int128;

#define GetDealFeeRatio(dexDealFeeRatio) (\
    cw.sysParamCache.GetParam(DEX_DEAL_FEE_RATIO, dexDealFeeRatio)? true : \
        state.DoS(100, ERRORMSG("%s(), read DEX_DEAL_FEE_RATIO error", __FUNCTION__), \
                        READ_SYS_PARAM_FAIL, "read-sysparamdb-error") ) \


#define ERROR_TITLE(msg) (std::string(__FUNCTION__) + "(), " + msg)

///////////////////////////////////////////////////////////////////////////////
// class CDEXOrderBaseTx

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

bool CDEXOrderBaseTx::CheckDexOperatorExist(CTxExecuteContext &context) {
    if (dex_id != DEX_RESERVED_ID) {
        if (!context.pCw->dexCache.HaveDexOperator(dex_id))
            return context.pState->DoS(100, ERRORMSG("%s, dex operator does not exist! dex_id=%d", ERROR_TITLE(GetTxTypeName()), dex_id),
                REJECT_INVALID, "bad-getaccount");
    }
    return true;
}

uint64_t CDEXOrderBaseTx::CalcCoinAmount(uint64_t assetAmount, const uint64_t price) {
    uint128_t coinAmount = assetAmount * (uint128_t)price / PRICE_BOOST;
    assert(coinAmount < ULLONG_MAX);
    return (uint64_t)coinAmount;
}

///////////////////////////////////////////////////////////////////////////////
// class CDEXBuyLimitOrderBaseTx

string CDEXBuyLimitOrderBaseTx::ToString(CAccountDBCache &accountCache) {
    return strprintf(
        "txType=%s, hash=%s, ver=%d, valid_height=%d, txUid=%s, llFees=%llu, "
        "coin_symbol=%s, asset_symbol=%s, amount=%llu, price=%llu",
        GetTxType(nTxType), GetHash().GetHex(), nVersion, valid_height, txUid.ToString(), llFees,
        coin_symbol, asset_symbol, asset_amount, price);
}

Object CDEXBuyLimitOrderBaseTx::ToJson(const CAccountDBCache &accountCache) const {
    Object result = CBaseTx::ToJson(accountCache);
    result.push_back(Pair("coin_symbol",      coin_symbol));
    result.push_back(Pair("asset_symbol",     asset_symbol));
    result.push_back(Pair("asset_amount",   asset_amount));
    result.push_back(Pair("price",          price));
    return result;
}

bool CDEXBuyLimitOrderBaseTx::CheckTx(CTxExecuteContext &context) {
    IMPLEMENT_DEFINE_CW_STATE;
    IMPLEMENT_DISABLE_TX_PRE_STABLE_COIN_RELEASE;
    IMPLEMENT_CHECK_TX_REGID_OR_PUBKEY(txUid.type());
    IMPLEMENT_CHECK_TX_FEE;
    IMPLEMENT_CHECK_TX_MEMO;

    if (!CheckOrderSymbols(state, ERROR_TITLE(GetTxTypeName()), coin_symbol, asset_symbol)) return false;

    if (!CheckOrderAmountRange(state, ERROR_TITLE(GetTxTypeName() + " asset"), asset_symbol, asset_amount)) return false;

    if (!CheckOrderPriceRange(state, ERROR_TITLE(GetTxTypeName()), coin_symbol, asset_symbol, price)) return false;

    CAccount srcAccount;
    if (!cw.accountCache.GetAccount(txUid, srcAccount))
        return state.DoS(100, ERRORMSG("%s, read account failed", ERROR_TITLE(GetTxTypeName())),
            REJECT_INVALID, "bad-getaccount");

    if (!CheckDexOperatorExist(context)) return false;

    CPubKey pubKey = (txUid.type() == typeid(CPubKey) ? txUid.get<CPubKey>() : srcAccount.owner_pubkey);
    IMPLEMENT_CHECK_TX_SIGNATURE(pubKey);

    return true;
}

bool CDEXBuyLimitOrderBaseTx::ExecuteTx(CTxExecuteContext &context) {
    CCacheWrapper &cw = *context.pCw; CValidationState &state = *context.pState;
    CAccount srcAccount;
    if (!cw.accountCache.GetAccount(txUid, srcAccount)) {
        return state.DoS(100, ERRORMSG("%s, read source addr account info error", ERROR_TITLE(GetTxTypeName())),
                         READ_ACCOUNT_FAIL, "bad-read-accountdb");
    }

    if (!GenerateRegID(context, srcAccount)) {
        return false;
    }

    if (!srcAccount.OperateBalance(fee_symbol, SUB_FREE, llFees)) {
        return state.DoS(100, ERRORMSG("%s, account has insufficient funds", ERROR_TITLE(GetTxTypeName())),
                         UPDATE_ACCOUNT_FAIL, "operate-minus-account-failed");
    }
    // should freeze user's coin for buying the asset
    uint64_t coinAmount = CalcCoinAmount(asset_amount, price);

    if (!srcAccount.OperateBalance(coin_symbol, FREEZE, coinAmount)) {
        return state.DoS(100, ERRORMSG("%s, account has insufficient funds", ERROR_TITLE(GetTxTypeName())),
                         UPDATE_ACCOUNT_FAIL, "operate-dex-order-account-failed");
    }

    if (!cw.accountCache.SetAccount(CUserID(srcAccount.keyid), srcAccount))
        return state.DoS(100, ERRORMSG("%s, set account info error", ERROR_TITLE(GetTxTypeName())),
                         WRITE_ACCOUNT_FAIL, "bad-write-accountdb");

    uint64_t dexDealFeeRatio;
    if (!GetDealFeeRatio(dexDealFeeRatio)) return false;

    assert(!srcAccount.regid.IsEmpty());
    const uint256 &txid = GetHash();
    CDEXOrderDetail orderDetail;
    orderDetail.dex_id = dex_id;
    orderDetail.generate_type = USER_GEN_ORDER;
    orderDetail.order_type    = ORDER_LIMIT_PRICE;
    orderDetail.order_side    = ORDER_BUY;
    orderDetail.coin_symbol   = coin_symbol;
    orderDetail.asset_symbol  = asset_symbol;
    orderDetail.coin_amount   = CalcCoinAmount(asset_amount, price);
    orderDetail.asset_amount  = asset_amount;
    orderDetail.price         = price;
    orderDetail.fee_ratio     = dexDealFeeRatio;
    orderDetail.tx_cord       = CTxCord(context.height, context.index);
    orderDetail.user_regid    = srcAccount.regid;
    // other fields keep the default value

    if (!cw.dexCache.CreateActiveOrder(txid, orderDetail))
        return state.DoS(100, ERRORMSG("%s, create active buy order failed", ERROR_TITLE(GetTxTypeName())),
                         REJECT_INVALID, "bad-write-dexdb");

    return true;
}

///////////////////////////////////////////////////////////////////////////////
// class CDEXBuyLimitOrderTx

bool CDEXBuyLimitOrderTx::CheckTx(CTxExecuteContext &context) {
    // TODO: check version < 3
    return CDEXBuyLimitOrderBaseTx::CheckTx(context);
}

///////////////////////////////////////////////////////////////////////////////
// class CDEXBuyLimitOrderTx

bool CDEXBuyLimitOrderExTx::CheckTx(CTxExecuteContext &context) {
    // TODO: check version >= 3
    return CDEXBuyLimitOrderBaseTx::CheckTx(context);
}

///////////////////////////////////////////////////////////////////////////////
// class CDEXSellLimitOrderTx

string CDEXSellLimitOrderBaseTx::ToString(CAccountDBCache &accountCache) {
    return strprintf(
        "txType=%s, hash=%s, ver=%d, valid_height=%d, txUid=%s, llFees=%llu, "
        "coin_symbol=%s, asset_symbol=%s, amount=%llu, price=%llu",
        GetTxType(nTxType), GetHash().GetHex(), nVersion, valid_height, txUid.ToString(), llFees,
        coin_symbol, asset_symbol, asset_amount, price);
}

Object CDEXSellLimitOrderBaseTx::ToJson(const CAccountDBCache &accountCache) const {
    Object result = CBaseTx::ToJson(accountCache);
    result.push_back(Pair("coin_symbol",    coin_symbol));
    result.push_back(Pair("asset_symbol",   asset_symbol));
    result.push_back(Pair("asset_amount",   asset_amount));
    result.push_back(Pair("price",          price));
    return result;
}

bool CDEXSellLimitOrderBaseTx::CheckTx(CTxExecuteContext &context) {
    IMPLEMENT_DEFINE_CW_STATE;
    IMPLEMENT_DISABLE_TX_PRE_STABLE_COIN_RELEASE;
    IMPLEMENT_CHECK_TX_REGID_OR_PUBKEY(txUid.type());
    IMPLEMENT_CHECK_TX_FEE;
    IMPLEMENT_CHECK_TX_MEMO;

    if (!CheckOrderSymbols(state, "CDEXSellLimitOrderTx::CheckTx,", coin_symbol, asset_symbol)) return false;

    if (!CheckOrderAmountRange(state, "CDEXSellLimitOrderTx::CheckTx, asset,", asset_symbol, asset_amount)) return false;

    if (!CheckOrderPriceRange(state, "CDEXSellLimitOrderTx::CheckTx,", coin_symbol, asset_symbol, price)) return false;

    CAccount srcAccount;
    if (!cw.accountCache.GetAccount(txUid, srcAccount))
        return state.DoS(100, ERRORMSG("CDEXSellLimitOrderTx::CheckTx, read account failed"), REJECT_INVALID,
                         "bad-getaccount");

    if (!CheckDexOperatorExist(context)) return false;

    CPubKey pubKey = ( txUid.type() == typeid(CPubKey) ? txUid.get<CPubKey>() : srcAccount.owner_pubkey );
    IMPLEMENT_CHECK_TX_SIGNATURE(pubKey);

    return true;
}

bool CDEXSellLimitOrderBaseTx::ExecuteTx(CTxExecuteContext &context) {
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

    uint64_t dexDealFeeRatio;
    if (!GetDealFeeRatio(dexDealFeeRatio)) return false;

    assert(!srcAccount.regid.IsEmpty());
    const uint256 &txid = GetHash();
    CDEXOrderDetail orderDetail;
    orderDetail.generate_type = USER_GEN_ORDER;
    orderDetail.order_type    = ORDER_LIMIT_PRICE;
    orderDetail.order_side    = ORDER_SELL;
    orderDetail.coin_symbol   = coin_symbol;
    orderDetail.asset_symbol  = asset_symbol;
    orderDetail.coin_amount   = CalcCoinAmount(asset_amount, price);
    orderDetail.asset_amount  = asset_amount;
    orderDetail.price         = price;
    orderDetail.fee_ratio     = dexDealFeeRatio;
    orderDetail.tx_cord       = CTxCord(context.height, context.index);
    orderDetail.user_regid = srcAccount.regid;
    // other fields keep the default value

    if (!cw.dexCache.CreateActiveOrder(txid, orderDetail))
        return state.DoS(100, ERRORMSG("CDEXSellLimitOrderTx::ExecuteTx, create active sell order failed"),
                         WRITE_ACCOUNT_FAIL, "bad-write-dexdb");

    return true;
}


///////////////////////////////////////////////////////////////////////////////
// class CDEXBuyMarketOrderBaseTx

string CDEXBuyMarketOrderBaseTx::ToString(CAccountDBCache &accountCache) {
    return strprintf(
        "txType=%s, hash=%s, ver=%d, valid_height=%d, txUid=%s, llFees=%llu, "
        "coin_symbol=%s, asset_symbol=%s, amount=%llu",
        GetTxType(nTxType), GetHash().GetHex(), nVersion, valid_height, txUid.ToString(), llFees,
        coin_symbol, asset_symbol, coin_amount);
}

Object CDEXBuyMarketOrderBaseTx::ToJson(const CAccountDBCache &accountCache) const {
    Object result = CBaseTx::ToJson(accountCache);
    result.push_back(Pair("coin_symbol",    coin_symbol));
    result.push_back(Pair("asset_symbol",   asset_symbol));
    result.push_back(Pair("coin_amount",    coin_amount));

    return result;
}

bool CDEXBuyMarketOrderBaseTx::CheckTx(CTxExecuteContext &context) {
    IMPLEMENT_DEFINE_CW_STATE;
    IMPLEMENT_DISABLE_TX_PRE_STABLE_COIN_RELEASE;
    IMPLEMENT_CHECK_TX_REGID_OR_PUBKEY(txUid.type());
    IMPLEMENT_CHECK_TX_FEE;
    IMPLEMENT_CHECK_TX_MEMO;

    if (!CheckOrderSymbols(state, "CDEXBuyMarketOrderTx::CheckTx,", coin_symbol, asset_symbol)) return false;

    if (!CheckOrderAmountRange(state, "CDEXBuyMarketOrderTx::CheckTx, coin,", coin_symbol, coin_amount)) return false;

    CAccount srcAccount;
    if (!cw.accountCache.GetAccount(txUid, srcAccount))
        return state.DoS(100, ERRORMSG("CDEXBuyMarketOrderTx::CheckTx, read account failed"), REJECT_INVALID,
                         "bad-getaccount");

    if (!CheckDexOperatorExist(context)) return false;

    CPubKey pubKey = (txUid.type() == typeid(CPubKey) ? txUid.get<CPubKey>() : srcAccount.owner_pubkey);
    IMPLEMENT_CHECK_TX_SIGNATURE(pubKey);

    return true;
}

bool CDEXBuyMarketOrderBaseTx::ExecuteTx(CTxExecuteContext &context) {
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

    uint64_t dexDealFeeRatio;
    if (!GetDealFeeRatio(dexDealFeeRatio)) return false;

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
    orderDetail.fee_ratio     = dexDealFeeRatio;
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
// class CDEXSellMarketOrderBaseTx

string CDEXSellMarketOrderBaseTx::ToString(CAccountDBCache &accountCache) {
    return strprintf(
        "txType=%s, hash=%s, ver=%d, valid_height=%d, txUid=%s, llFees=%llu, "
        "coin_symbol=%s, asset_symbol=%s, amount=%llu",
        GetTxType(nTxType), GetHash().GetHex(), nVersion, valid_height, txUid.ToString(), llFees,
        coin_symbol, asset_symbol, asset_amount);
}

Object CDEXSellMarketOrderBaseTx::ToJson(const CAccountDBCache &accountCache) const {
    Object result = CBaseTx::ToJson(accountCache);
    result.push_back(Pair("coin_symbol",    coin_symbol));
    result.push_back(Pair("asset_symbol",   asset_symbol));
    result.push_back(Pair("asset_amount",   asset_amount));
    return result;
}

bool CDEXSellMarketOrderBaseTx::CheckTx(CTxExecuteContext &context) {
    IMPLEMENT_DEFINE_CW_STATE;
    IMPLEMENT_DISABLE_TX_PRE_STABLE_COIN_RELEASE;
    IMPLEMENT_CHECK_TX_REGID_OR_PUBKEY(txUid.type());
    IMPLEMENT_CHECK_TX_FEE;
    IMPLEMENT_CHECK_TX_MEMO;

    if (!CheckOrderSymbols(state, "CDEXSellMarketOrderTx::CheckTx,", coin_symbol, asset_symbol))
        return false;

    if (!CheckOrderAmountRange(state, "CDEXBuyMarketOrderTx::CheckTx, asset,", asset_symbol, asset_amount))
        return false;

    CAccount srcAccount;
    if (!cw.accountCache.GetAccount(txUid, srcAccount))
        return state.DoS(100, ERRORMSG("CDEXSellMarketOrderTx::CheckTx, read account failed"), REJECT_INVALID,
                         "bad-getaccount");

    if (!CheckDexOperatorExist(context)) return false;

    CPubKey pubKey = (txUid.type() == typeid(CPubKey) ? txUid.get<CPubKey>() : srcAccount.owner_pubkey);
    IMPLEMENT_CHECK_TX_SIGNATURE(pubKey);

    return true;
}

bool CDEXSellMarketOrderBaseTx::ExecuteTx(CTxExecuteContext &context) {
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

    uint64_t dexDealFeeRatio;
    if (!GetDealFeeRatio(dexDealFeeRatio)) return false;

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
    orderDetail.fee_ratio     = dexDealFeeRatio;
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

static bool CheckOrderFeeRate(CTxExecuteContext &context, const uint256 &orderId, const CDEXOrderDetail &order) {
    static_assert(DEX_ORDER_FEE_RATE_MAX < 100 * PRICE_BOOST, "fee rate must < 100%");
    if (order.fee_ratio <= DEX_ORDER_FEE_RATE_MAX)
        return context.pState->DoS(100, ERRORMSG("%s(), order fee_ratio invalid! order_id=%s, fee_rate=%llu",
            __FUNCTION__, orderId.ToString(), order.fee_ratio), REJECT_INVALID, "invalid-fee-ratio");
    return true;
}

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
        uint64_t buyerReceivedAssets = dealItem.dealAssetAmount;
        // 9.1 buyer pay the fee from the received assets to settler
        if (buyOrder.fee_ratio != 0) {
            if (!CheckOrderFeeRate(context, dealItem.buyOrderId, buyOrder)) return false;

            uint64_t dealAssetFee = dealItem.dealAssetAmount * buyOrder.fee_ratio / PRICE_BOOST;
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
        if (sellOrder.fee_ratio != 0) {
            if (!CheckOrderFeeRate(context, dealItem.sellOrderId, sellOrder)) return false;
            uint64_t dealCoinFee = dealItem.dealCoinAmount * sellOrder.fee_ratio / PRICE_BOOST;
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

static const string OPERATOR_ACTION_REGISTER = "register";
static const string OPERATOR_ACTION_UPDATE = "update";

static bool ProcessDexOperatorFee(CCacheWrapper &cw, CValidationState &state, const string &action,
    CAccount &txAccount, vector<CReceipt> &receipts) {

    uint64_t exchangeFee = 0;
    if (action == OPERATOR_ACTION_REGISTER) {
        if (!cw.sysParamCache.GetParam(DEX_OPERATOR_REGISTER_FEE, exchangeFee))
            return state.DoS(100, ERRORMSG("%s(), read param DEX_OPERATOR_REGISTER_FEE error", __func__),
                            REJECT_INVALID, "read-sysparam-error");
    } else {
        assert(action == OPERATOR_ACTION_UPDATE);
        if (!cw.sysParamCache.GetParam(DEX_OPERATOR_UPDATE_FEE, exchangeFee))
            return state.DoS(100, ERRORMSG("%s(), read param DEX_OPERATOR_UPDATE_FEE error", __func__),
                            REJECT_INVALID, "read-sysparam-error");
    }

    if (!txAccount.OperateBalance(SYMB::WICC, BalanceOpType::SUB_FREE, exchangeFee))
        return state.DoS(100, ERRORMSG("%s(), tx account insufficient funds for operator %s fee! fee=%llu, tx_addr=%s",
                        __func__, action, exchangeFee, txAccount.keyid.ToAddress()),
                        UPDATE_ACCOUNT_FAIL, "insufficent-funds");

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
    if (action == OPERATOR_ACTION_REGISTER)
        receipts.emplace_back(txAccount.regid, fcoinGenesisAccount.regid, SYMB::WICC, riskFee, ReceiptCode::DEX_OPERATOR_REG_FEE_TO_RISERVE);
    else
        receipts.emplace_back(txAccount.regid, fcoinGenesisAccount.regid, SYMB::WICC, riskFee, ReceiptCode::DEX_OPERATOR_UPDATED_FEE_TO_RISERVE);

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
        uint64_t minerFee = minerTotalFee / delegates.size();
        if (i == 0) minerFee += minerTotalFee % delegates.size(); // give the dust amount to topmost miner

        if (!delegateAccount.OperateBalance(SYMB::WICC, BalanceOpType::ADD_FREE, minerFee)) {
            return state.DoS(100, ERRORMSG("%s(), add %s asset fee to miner failed, miner regid=%s",
                __func__, action, delegateRegid.ToString()), UPDATE_ACCOUNT_FAIL, "operate-account-failed");
        }

        if (!cw.accountCache.SetAccount(delegateRegid, delegateAccount))
            return state.DoS(100, ERRORMSG("%s(), write delegate account info error, delegate regid=%s",
                __func__, delegateRegid.ToString()), UPDATE_ACCOUNT_FAIL, "bad-read-accountdb");

        if (action == OPERATOR_ACTION_REGISTER)
            receipts.emplace_back(txAccount.regid, delegateRegid, SYMB::WICC, minerFee, ReceiptCode::DEX_OPERATOR_REG_FEE_TO_MINER);
        else
            receipts.emplace_back(txAccount.regid, delegateRegid, SYMB::WICC, minerFee, ReceiptCode::DEX_OPERATOR_UPDATED_FEE_TO_MINER);
    }
    return true;
}

////////////////////////////////////////////////////////////////////////////////
// class CDEXOperatorRegisterTx

string CDEXOperatorRegisterTx::ToString(CAccountDBCache &accountCache) {
    // TODO: ...
    return "";
}

Object CDEXOperatorRegisterTx::ToJson(const CAccountDBCache &accountCache) const {
    // TODO: ...
    return Object();
}

bool CDEXOperatorRegisterTx::CheckTx(CTxExecuteContext &context) {
    IMPLEMENT_DEFINE_CW_STATE;
    IMPLEMENT_DISABLE_TX_PRE_STABLE_COIN_RELEASE;
    IMPLEMENT_CHECK_TX_REGID_OR_PUBKEY(txUid.type());
    IMPLEMENT_CHECK_TX_FEE;

    if (!data.owner_uid.is<CRegID>()) {
        return state.DoS(100, ERRORMSG("%s, owner_uid must be regid", __func__), REJECT_INVALID,
            "owner-uid-type-error");
    }

    if (!data.match_uid.is<CRegID>()) {
        return state.DoS(100, ERRORMSG("%s, match_uid must be regid", __func__), REJECT_INVALID,
            "match-uid-type-error");
    }

    static const uint32_t MAX_NAME_LEN = 32;
    if (data.name.size() > MAX_NAME_LEN) {
        return state.DoS(100, ERRORMSG("%s, name len=%d greater than %d", __func__,
            data.name.size(), MAX_NAME_LEN), REJECT_INVALID, "invalid-domain-name");
    }

    static const uint64_t MAX_MATCH_FEE_RATIO_VALUE = 50000000; // 50%

    if (data.maker_fee_ratio > MAX_MATCH_FEE_RATIO_VALUE)
        return state.DoS(100, ERRORMSG("%s, maker_fee_ratio=%d is greater than %d", __func__,
            data.maker_fee_ratio, MAX_MATCH_FEE_RATIO_VALUE), REJECT_INVALID, "invalid-match-fee-ratio-type");
    if (data.taker_fee_ratio > MAX_MATCH_FEE_RATIO_VALUE)
        return state.DoS(100, ERRORMSG("%s, taker_fee_ratio=%d is greater than %d", __func__,
            data.taker_fee_ratio, MAX_MATCH_FEE_RATIO_VALUE), REJECT_INVALID, "invalid-match-fee-ratio-type");

    CAccount txAccount;
    if (!cw.accountCache.GetAccount(txUid, txAccount))
        return state.DoS(100, ERRORMSG("CDEXOperatorRegisterTx::CheckTx, read account failed! tx account not exist, txUid=%s",
                     txUid.ToDebugString()), REJECT_INVALID, "bad-getaccount");

    CPubKey pubKey = (txUid.type() == typeid(CPubKey) ? txUid.get<CPubKey>() : txAccount.owner_pubkey);
    IMPLEMENT_CHECK_TX_SIGNATURE(pubKey);

    return true;
}
bool CDEXOperatorRegisterTx::ExecuteTx(CTxExecuteContext &context) {
    CCacheWrapper &cw = *context.pCw; CValidationState &state = *context.pState;
    vector<CReceipt> receipts;
    shared_ptr<CAccount> pTxAccount = make_shared<CAccount>();
    if (pTxAccount == nullptr || !cw.accountCache.GetAccount(txUid, *pTxAccount))
        return state.DoS(100, ERRORMSG("CDEXOperatorRegisterTx::ExecuteTx, read tx account by txUid=%s error",
            txUid.ToDebugString()), UPDATE_ACCOUNT_FAIL, "bad-read-accountdb");

    if (!pTxAccount->OperateBalance(fee_symbol, BalanceOpType::SUB_FREE, llFees)) {
        return state.DoS(100, ERRORMSG("CDEXOperatorRegisterTx::ExecuteTx, insufficient funds in account to sub fees, fees=%llu, txUid=%s",
                        llFees, txUid.ToDebugString()), UPDATE_ACCOUNT_FAIL, "insufficent-funds");
    }

    shared_ptr<CAccount> pOwnerAccount;
    if (pTxAccount->IsMyUid(data.owner_uid)) {
        pOwnerAccount = pTxAccount;
    } else {
        pOwnerAccount = make_shared<CAccount>();
        if (pOwnerAccount == nullptr || !cw.accountCache.GetAccount(data.owner_uid, *pOwnerAccount))
            return state.DoS(100, ERRORMSG("CDEXOperatorRegisterTx::CheckTx, read owner account failed! owner_uid=%s",
                data.owner_uid.ToDebugString()), REJECT_INVALID, "owner-account-not-exist");
    }
    shared_ptr<CAccount> pMatchAccount;
    if (!pTxAccount->IsMyUid(data.match_uid) && !pOwnerAccount->IsMyUid(data.match_uid)) {
        if (!cw.accountCache.HaveAccount(data.match_uid))
            return state.DoS(100, ERRORMSG("CDEXOperatorRegisterTx::CheckTx, get match account failed! match_uid=%s",
                data.match_uid.ToDebugString()), REJECT_INVALID, "match-account-not-exist");
    }

    if(cw.dexCache.HaveDexOperatorByOwner(pOwnerAccount->regid))
        return state.DoS(100, ERRORMSG("%s, the owner already has a dex operator! owner_regid=%s", __func__,
            pOwnerAccount->regid.ToString()), REJECT_INVALID, "match-account-not-exist");

    if (!ProcessDexOperatorFee(cw, state, OPERATOR_ACTION_REGISTER, *pTxAccount, receipts))
        return false;

    uint32_t new_id;
    if (!cw.dexCache.IncDexID(new_id))
        return state.DoS(100, ERRORMSG("%s, increase dex id error! txUid=%s", __func__),
            UPDATE_ACCOUNT_FAIL, "inc_dex_id_error");

    DexOperatorDetail detail = {
        data.owner_uid.get<CRegID>(),
        data.match_uid.get<CRegID>(),
        data.name,
        data.portal_url,
        data.maker_fee_ratio,
        data.taker_fee_ratio,
        data.memo
    };
    if (!cw.dexCache.CreateDexOperator(new_id, detail))
        return state.DoS(100, ERRORMSG("%s, save new dex operator error! new_id=%u", __func__, new_id),
                         UPDATE_ACCOUNT_FAIL, "inc_dex_id_error");

    if (!cw.accountCache.SetAccount(txUid, *pTxAccount))
        return state.DoS(100, ERRORMSG("CAssetIssueTx::ExecuteTx, set tx account to db failed! txUid=%s",
            txUid.ToDebugString()), UPDATE_ACCOUNT_FAIL, "bad-set-accountdb");

    if(!cw.txReceiptCache.SetTxReceipts(GetHash(), receipts))
        return state.DoS(100, ERRORMSG("CAssetIssueTx::ExecuteTx, set tx receipts failed!! txid=%s",
                        GetHash().ToString()), REJECT_INVALID, "set-tx-receipt-failed");
    return true;
}
