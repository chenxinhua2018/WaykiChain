// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2017-2019 The WaykiChain Developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "rpcscoin.h"

#include "commons/base58.h"
#include "config/const.h"
#include "rpc/core/rpcserver.h"
#include "rpc/core/rpccommons.h"
#include "init.h"
#include "net.h"
#include "miner/miner.h"
#include "commons/util/util.h"
#include "wallet/wallet.h"
#include "wallet/walletdb.h"
#include "tx/cdptx.h"
#include "tx/dextx.h"
#include "tx/pricefeedtx.h"
#include "tx/assettx.h"


Value submitpricefeedtx(const Array& params, bool fHelp) {
    if (fHelp || params.size() < 2 || params.size() > 3) {
        throw runtime_error(
            "submitpricefeedtx {price_feeds_json} [\"symbol:fee:unit\"]\n"
            "\nsubmit a Price Feed Tx.\n"
            "\nArguments:\n"
            "1. \"address\" :                   (string, required) Price Feeder's address\n"
            "2. \"pricefeeds\":                 (string, required) A json array of pricefeeds\n"
            " [\n"
            "   {\n"
            "      \"coin\": \"WICC|WGRT\",       (string, required) The coin type\n"
            "      \"currency\": \"USD|CNY\"      (string, required) The currency type\n"
            "      \"price\":                   (number, required) The price (boosted by 10^4) \n"
            "   }\n"
            "       ,...\n"
            " ]\n"
            "3. \"symbol:fee:unit\":            (string:numeric:string, optional) fee paid to miner, default is WICC:10000:sawi\n"
            "\nResult:\n"
            "\"txid\"                           (string) The transaction id.\n"
            "\nExamples:\n" +
            HelpExampleCli("submitpricefeedtx",
                           "\"WiZx6rrsBn9sHjwpvdwtMNNX2o31s3DEHH\" "
                           "\"[{\\\"coin\\\": \\\"WICC\\\", \\\"currency\\\": \\\"USD\\\", \\\"price\\\": 2500}]\"") +
            "\nAs json rpc call\n" +
            HelpExampleRpc("submitpricefeedtx",
                           "\"WiZx6rrsBn9sHjwpvdwtMNNX2o31s3DEHH\", [{\"coin\": \"WICC\", \"currency\": \"USD\", "
                           "\"price\": 2500}]"));
    }

    EnsureWalletIsUnlocked();

    const CUserID &feedUid = RPC_PARAM::GetUserId(params[0].get_str());

    if (!feedUid.is<CRegID>())
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Regid not exist or immature");

    Array arrPricePoints = params[1].get_array();
    vector<CPricePoint> pricePoints;
    for (auto objPp : arrPricePoints) {
        const Value& coinValue     = find_value(objPp.get_obj(), "coin");
        const Value& currencyValue = find_value(objPp.get_obj(), "currency");
        const Value& priceValue    = find_value(objPp.get_obj(), "price");
        if (coinValue.type() == null_type || currencyValue.type() == null_type || priceValue.type() == null_type) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "null type not allowed!");
        }

        string coinStr = coinValue.get_str();
        if (!kCoinTypeSet.count(coinStr)) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Invalid coin symbol: %s", coinStr));
        }

        string currencyStr = currencyValue.get_str();
        if (!kCurrencyTypeSet.count(currencyStr)) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Invalid currency type: %s", currencyStr));
        }

        int64_t price = priceValue.get_int64();
        if (price <= 0) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Invalid price: %lld", price));
        }

        CoinPricePair cpp(coinStr, currencyStr);
        CPricePoint pp(cpp, uint64_t(price));
        pricePoints.push_back(pp);
    }

    const ComboMoney &cmFee = RPC_PARAM::GetFee(params, 2, PRICE_FEED_TX);

    // Get account for checking balance
    CAccount account = RPC_PARAM::GetUserAccount(*pCdMan->pAccountCache, feedUid);
    RPC_PARAM::CheckAccountBalance(account, cmFee.symbol, SUB_FREE, cmFee.GetSawiAmount());

    int32_t validHeight = chainActive.Height();
    CPriceFeedTx tx(feedUid, validHeight, cmFee.symbol, cmFee.GetSawiAmount(), pricePoints);

    return SubmitTx(account.keyid, tx);
}

Value submitcoinstaketx(const Array& params, bool fHelp) {
    if (fHelp || params.size() < 3 || params.size() > 4) {
        throw runtime_error(
            "submitcoinstaketx \"addr\" \"coin_symbol\" \"coin_amount\" [\"symbol:fee:unit\"]\n"
            "\nstake fcoins\n"
            "\nArguments:\n"
            "1.\"addr\":                (string, required)\n"
            "2. \"stake_combo_money\":  (symbol:amount:unit, required) Combo Money to stake or unstake the CDP,"
            " default symbol=WICC, default unit=sawi\n"
            "3. \"stake_direction\":    (bool, required) stake or unstake: true stand for stake , false stand for unstake "
            "4.\"symbol:fee:unit\":     (string:numeric:string, optional) fee paid to miner, default is WICC:10000:sawi\n"
            "\nResult:\n"
            "\"txid\"               (string) The transaction id.\n"
            "\nExamples:\n"
            + HelpExampleCli("submitcoinstaketx", "\"WiZx6rrsBn9sHjwpvdwtMNNX2o31s3DEHH\" \"WICC:0.1:wi\" true")
            + "\nAs json rpc call\n"
            + HelpExampleRpc("submitcoinstaketx", "\"WiZx6rrsBn9sHjwpvdwtMNNX2o31s3DEHH\", \"WICC:0.1:wi\", true")
        );
    }

    EnsureWalletIsUnlocked();

    const CUserID& userId   = RPC_PARAM::GetUserId(params[0], true);

    ComboMoney coinAmount   = RPC_PARAM::GetComboMoney(params[1]);
    ComboMoney cmFee        = RPC_PARAM::GetFee(params, 3, UCOIN_STAKE_TX);
    int32_t validHeight     = chainActive.Height();
    BalanceOpType stakeType = params[2].get_bool() ? BalanceOpType::STAKE : BalanceOpType::UNSTAKE;

    // Get account for checking balance
    CAccount account = RPC_PARAM::GetUserAccount(*pCdMan->pAccountCache, userId);
    RPC_PARAM::CheckAccountBalance(account, cmFee.symbol, SUB_FREE, cmFee.GetSawiAmount());

    CCoinStakeTx tx(userId, validHeight, cmFee.symbol, cmFee.GetSawiAmount(), stakeType, coinAmount.symbol, coinAmount.GetSawiAmount());
    return SubmitTx(account.keyid, tx);
}

/*************************************************<< CDP >>**************************************************/
Value submitcdpstaketx(const Array& params, bool fHelp) {
    if (fHelp || params.size() < 3 || params.size() > 6) {
        throw runtime_error(
            "submitcdpstaketx \"addr\" stake_combo_money mint_combo_money [\"cdp_id\"] [symbol:fee:unit]\n"
            "\nsubmit a CDP Staking Tx.\n"
            "\nArguments:\n"
            "1. \"addr\":               (string, required) CDP Staker's account address\n"
            "2. \"stake_combo_money\":  (symbol:amount:unit, required) Combo Money to stake into the CDP,"
            " default symbol=WICC, default unit=sawi\n"
            "3. \"mint_combo_money\":   (symbol:amount:unit, required), Combo Money to mint from the CDP,"
            " default symbol=WUSD, default unit=sawi\n"
            "4. \"cdp_id\":             (string, optional) CDP ID (tx hash of the first CDP Stake Tx)\n"
            "5. \"symbol:fee:unit\":    (symbol:amount:unit, optional) fee paid to miner, default is WICC:100000:sawi\n"
            "\nResult:\n"
            "\"txid\"                   (string) The transaction id.\n"
            "\nExamples:\n" +
            HelpExampleCli(
                "submitcdpstaketx",
                "\"WiZx6rrsBn9sHjwpvdwtMNNX2o31s3DEHH\" \"WICC:20000000000:sawi\" \"WUSD:3000000:sawi\" "
                "\"b850d88bf1bed66d43552dd724c18f10355e9b6657baeae262b3c86a983bee71\" \"WICC:1000000:sawi\"\n") +
            "\nAs json rpc call\n" +
            HelpExampleRpc(
                "submitcdpstaketx",
                "\"WiZx6rrsBn9sHjwpvdwtMNNX2o31s3DEHH\", \"WICC:2000000000:sawi\", \"WUSD:3000000:sawi\", "
                "\"b850d88bf1bed66d43552dd724c18f10355e9b6657baeae262b3c86a983bee71\", \"WICC:1000000:sawi\"\n"));
    }

    EnsureWalletIsUnlocked();

    const CUserID &cdpUid = RPC_PARAM::GetUserId(params[0], true);

    ComboMoney cmBcoinsToStake, cmScoinsToMint;
    if (!ParseRpcInputMoney(params[1].get_str(), cmBcoinsToStake, SYMB::WICC))
        throw JSONRPCError(RPC_INVALID_PARAMETER, "bcoinsToStake ComboMoney format error");

    if (!ParseRpcInputMoney(params[2].get_str(), cmScoinsToMint, SYMB::WUSD))
        throw JSONRPCError(RPC_INVALID_PARAMETER, "scoinsToMint ComboMoney format error");

    int32_t validHeight = chainActive.Height();

    uint256 cdpId;
    if (params.size() > 3) {
        cdpId = RPC_PARAM::GetTxid(params[3], "cdp_id", true);
    }

    const ComboMoney &cmFee = RPC_PARAM::GetFee(params, 4, CDP_STAKE_TX);

    if (cdpId.IsEmpty()) { // new stake cdp
        if (cmBcoinsToStake.amount == 0)
            throw JSONRPCError(RPC_INVALID_PARAMETER, "stake_amount is zero!");

        if (cmScoinsToMint.amount == 0)
            throw JSONRPCError(RPC_INVALID_PARAMETER, "mint_amount is zero!");
    }

    CAccount account = RPC_PARAM::GetUserAccount(*pCdMan->pAccountCache, cdpUid);

    CCDPStakeTx tx(cdpUid, validHeight, cdpId, cmFee, cmBcoinsToStake, cmScoinsToMint);
    return SubmitTx(account.keyid, tx);
}

Value submitcdpredeemtx(const Array& params, bool fHelp) {
    if (fHelp || params.size() < 4 || params.size() > 5) {
        throw runtime_error(
            "submitcdpredeemtx \"addr\" \"cdp_id\" repay_amount redeem_amount [\"symbol:fee:unit\"]\n"
            "\nsubmit a CDP Redemption Tx\n"
            "\nArguments:\n"
            "1. \"addr\" :              (string, required) CDP redemptor's address\n"
            "2. \"cdp_id\":             (string, required) ID of existing CDP (tx hash of the first CDP Stake Tx)\n"
            "3. \"repay_amount\":       (numeric, required) scoins (E.g. WUSD) to repay into the CDP, boosted by 10^8\n"
            "4. \"redeem_amount\":      (numeric, required) bcoins (E.g. WICC) to redeem from the CDP, boosted by 10^8\n"
            "5. \"symbol:fee:unit\":    (string:numeric:string, optional) fee paid to miner, default is WICC:100000:sawi\n"
            "\nResult:\n"
            "\"txid\"                   (string) The transaction id.\n"
            "\nExamples:\n" +
            HelpExampleCli("submitcdpredeemtx",
                           "\"WiZx6rrsBn9sHjwpvdwtMNNX2o31s3DEHH\" "
                           "\"b850d88bf1bed66d43552dd724c18f10355e9b6657baeae262b3c86a983bee71\" "
                           "20000000000 40000000000 \"WICC:1000000:sawi\"\n") +
            "\nAs json rpc call\n" +
            HelpExampleRpc("submitcdpredeemtx",
                           "\"WiZx6rrsBn9sHjwpvdwtMNNX2o31s3DEHH\", "
                           "\"b850d88bf1bed66d43552dd724c18f10355e9b6657baeae262b3c86a983bee71\", "
                           "20000000000, 40000000000, \"WICC:1000000:sawi\"\n"));
    }

    EnsureWalletIsUnlocked();

    const CUserID& cdpUid   = RPC_PARAM::GetUserId(params[0].get_str(), true);
    uint256 cdpTxId         = uint256S(params[1].get_str());
    uint64_t repayAmount    = AmountToRawValue(params[2]);
    uint64_t redeemAmount   = AmountToRawValue(params[3]);
    const ComboMoney& cmFee = RPC_PARAM::GetFee(params, 4, CDP_STAKE_TX);
    int32_t validHeight     = chainActive.Height();

    CAccount account = RPC_PARAM::GetUserAccount(*pCdMan->pAccountCache, cdpUid);

    CCDPRedeemTx tx(cdpUid, cmFee, validHeight, cdpTxId, repayAmount, redeemAmount);
    return SubmitTx(account.keyid, tx);
}

Value submitcdpliquidatetx(const Array& params, bool fHelp) {
    if (fHelp || params.size() < 3 || params.size() > 4) {
        throw runtime_error(
            "submitcdpliquidatetx \"addr\" \"cdp_id\" liquidate_amount [symbol:fee:unit]\n"
            "\nsubmit a CDP Liquidation Tx\n"
            "\nArguments:\n"
            "1. \"addr\" :              (string, required) CDP liquidator's address\n"
            "2. \"cdp_id\":             (string, required) ID of existing CDP (tx hash of the first CDP Stake Tx)\n"
            "3. \"liquidate_amount\":   (numeric, required) WUSD coins to repay to CDP, boosted by 10^8 (penalty fees "
            "deducted separately from sender account)\n"
            "4. \"symbol:fee:unit\":    (string:numeric:string, optional) fee paid to miner, default is "
            "WICC:100000:sawi\n"
            "\nResult:\n"
            "\"txid\" (string) The transaction id.\n"
            "\nExamples:\n" +
            HelpExampleCli(
                "submitcdpliquidatetx",
                "\"WiZx6rrsBn9sHjwpvdwtMNNX2o31s3DEHH\"  "
                "\"b850d88bf1bed66d43552dd724c18f10355e9b6657baeae262b3c86a983bee71\" 20000000000 \"WICC:1000000:sawi\"\n") +
            "\nAs json rpc call\n" +
            HelpExampleRpc("submitcdpliquidatetx",
                           "\"WiZx6rrsBn9sHjwpvdwtMNNX2o31s3DEHH\", "
                           "\"b850d88bf1bed66d43552dd724c18f10355e9b6657baeae262b3c86a983bee71\", 2000000000, "
                           "\"WICC:1000000:sawi\"\n"));
    }

    EnsureWalletIsUnlocked();

    const CUserID& cdpUid    = RPC_PARAM::GetUserId(params[0], true);
    const uint256& cdpTxId   = RPC_PARAM::GetTxid(params[1], "cdp_id");
    uint64_t liquidateAmount = AmountToRawValue(params[2]);
    const ComboMoney& cmFee  = RPC_PARAM::GetFee(params, 3, CDP_STAKE_TX);
    int32_t validHeight      = chainActive.Height();

    CAccount account = RPC_PARAM::GetUserAccount(*pCdMan->pAccountCache, cdpUid);

    CCDPLiquidateTx tx(cdpUid, cmFee, validHeight, cdpTxId, liquidateAmount);
    return SubmitTx(account.keyid, tx);
}

Value getscoininfo(const Array& params, bool fHelp){
    if (fHelp || params.size() != 0) {
        throw runtime_error(
            "getscoininfo\n"
            "\nget stable coin info.\n"
            "\nArguments:\n"
            "\nResult:\n"
            "\nExamples:\n" +
            HelpExampleCli("getscoininfo", "") + "\nAs json rpc call\n" + HelpExampleRpc("getscoininfo", ""));
    }

    int32_t height = chainActive.Height();

    uint64_t slideWindow = 0;
    if (!pCdMan->pSysParamCache->GetParam(SysParamType::MEDIAN_PRICE_SLIDE_WINDOW_BLOCKCOUNT, slideWindow)) {
        throw JSONRPCError(RPC_INTERNAL_ERROR, "Acquire median price slide window blockcount error");
    }

    uint64_t globalCollateralCeiling = 0;
    if (!pCdMan->pSysParamCache->GetParam(SysParamType::GLOBAL_COLLATERAL_CEILING_AMOUNT, globalCollateralCeiling)) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Acquire global collateral ceiling error");
    }

    uint64_t globalCollateralRatioFloor = 0;
    if (!pCdMan->pSysParamCache->GetParam(SysParamType::GLOBAL_COLLATERAL_RATIO_MIN, globalCollateralRatioFloor)) {
        throw JSONRPCError(RPC_INTERNAL_ERROR, "Acquire global collateral ratio floor error");
    }

    map<CoinPricePair, uint64_t> medianPricePoints;
    if (!pCdMan->pPpCache->GetBlockMedianPricePoints(height, slideWindow, medianPricePoints)) {
        throw JSONRPCError(RPC_INTERNAL_ERROR, "Acquire median price error");
    }

    // TODO: multi stable coin
    uint64_t bcoinMedianPrice =
        pCdMan->pPpCache->GetMedianPrice(height, slideWindow, CoinPricePair(SYMB::WICC, SYMB::USD));
    uint64_t globalCollateralRatio = pCdMan->pCdpCache->GetGlobalCollateralRatio(bcoinMedianPrice);
    bool globalCollateralRatioFloorReached =
        pCdMan->pCdpCache->CheckGlobalCollateralRatioFloorReached(bcoinMedianPrice, globalCollateralRatioFloor);

    uint64_t globalStakedBcoins = 0;
    uint64_t globalOwedScoins   = 0;
    pCdMan->pCdpCache->GetGlobalItem(globalStakedBcoins, globalOwedScoins);

    bool global_collateral_ceiling_reached = globalStakedBcoins >= globalCollateralCeiling * COIN;

    RatioCDPIdCache::Map forceLiquidateCdps;
    uint64_t forceLiquidateRatio = 0;
    if (!pCdMan->pSysParamCache->GetParam(SysParamType::CDP_FORCE_LIQUIDATE_RATIO, forceLiquidateRatio)) {
        throw JSONRPCError(RPC_INTERNAL_ERROR, "Acquire cdp force liquidate ratio error");
    }

    pCdMan->pCdpCache->GetCdpListByCollateralRatio(forceLiquidateRatio, bcoinMedianPrice,
                                                               forceLiquidateCdps);

    Object obj;
    Array prices;
    for (auto& item : medianPricePoints) {
        if (item.second == 0) {
            continue;
        }

        Object price;
        price.push_back(Pair("coin_symbol",                     item.first.first));
        price.push_back(Pair("price_symbol",                    item.first.second));
        price.push_back(Pair("price",                           (double)item.second / PRICE_BOOST));
        prices.push_back(price);
    }

    obj.push_back(Pair("tipblock_height",                       height));
    obj.push_back(Pair("median_price",                          prices));
    obj.push_back(Pair("slide_window_block_count",              slideWindow));

    obj.push_back(Pair("global_staked_bcoins",                  globalStakedBcoins));
    obj.push_back(Pair("global_owed_scoins",                    globalOwedScoins));
    obj.push_back(Pair("global_collateral_ceiling",             globalCollateralCeiling * COIN));
    obj.push_back(Pair("global_collateral_ceiling_reached",     global_collateral_ceiling_reached));

    string gcr = globalOwedScoins == 0 ? "INF" : strprintf("%.2f%%", (double)globalCollateralRatio / RATIO_BOOST * 100);
    obj.push_back(Pair("global_collateral_ratio",               gcr));
    obj.push_back(Pair("global_collateral_ratio_floor",         strprintf("%.2f%%", (double)globalCollateralRatioFloor / RATIO_BOOST * 100)));
    obj.push_back(Pair("global_collateral_ratio_floor_reached", globalCollateralRatioFloorReached));

    obj.push_back(Pair("force_liquidate_ratio",                 strprintf("%.2f%%", (double)forceLiquidateRatio / RATIO_BOOST * 100)));
    obj.push_back(Pair("force_liquidate_cdp_amount",            forceLiquidateCdps.size()));

    return obj;
}

Value listcdps(const Array& params, bool fHelp);
Value listcdpstoliquidate(const Array& params, bool fHelp);

Value getusercdp(const Array& params, bool fHelp){
    if (fHelp || params.size() < 1 || params.size() > 2) {
        throw runtime_error(
            "getusercdp \"addr\"\n"
            "\nget account's cdp.\n"
            "\nArguments:\n"
            "1.\"addr\": (string, required) CDP owner's account addr\n"
            "\nResult:\n"
            "\nExamples:\n"
            + HelpExampleCli("getusercdp", "\"WiZx6rrsBn9sHjwpvdwtMNNX2o31s3DEHH\"\n")
            + "\nAs json rpc call\n"
            + HelpExampleRpc("getusercdp", "\"WiZx6rrsBn9sHjwpvdwtMNNX2o31s3DEHH\"\n")
        );
    }

    auto pUserId = CUserID::ParseUserId(params[0].get_str());
    if (!pUserId) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid addr");
    }

    CAccount account;
    if (!pCdMan->pAccountCache->GetAccount(*pUserId, account)) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, strprintf("The account not exists! userId=%s", pUserId->ToString()));
    }

    int32_t height = chainActive.Height();
    uint64_t slideWindow;
    pCdMan->pSysParamCache->GetParam(SysParamType::MEDIAN_PRICE_SLIDE_WINDOW_BLOCKCOUNT, slideWindow);
    // TODO: multi stable coin
    uint64_t bcoinMedianPrice = pCdMan->pPpCache->GetMedianPrice(height, slideWindow, CoinPricePair(SYMB::WICC, SYMB::USD));

    Object obj;
    Array cdps;
    vector<CUserCDP> userCdps;
    if (pCdMan->pCdpCache->GetCDPList(account.regid, userCdps)) {
        for (auto& cdp : userCdps) {
            cdps.push_back(cdp.ToJson(bcoinMedianPrice));
        }

        obj.push_back(Pair("user_cdps", cdps));
    }

    return obj;
}

Value getcdp(const Array& params, bool fHelp){
    if (fHelp || params.size() < 1 || params.size() > 2) {
        throw runtime_error(
            "getcdp \"cdp_id\"\n"
            "\nget CDP by its CDP_ID\n"
            "\nArguments:\n"
            "1.\"cdp_id\": (string, required) cdp_id\n"
            "\nResult:\n"
            "\nExamples:\n"
            + HelpExampleCli("getcdp", "\"c01f0aefeeb25fd6afa596f27ee3a1e861b657d2e1c341bfd1c412e87d9135c8\"\n")
            + "\nAs json rpc call\n"
            + HelpExampleRpc("getcdp", "\"c01f0aefeeb25fd6afa596f27ee3a1e861b657d2e1c341bfd1c412e87d9135c8\"\n")
        );
    }

    int32_t height = chainActive.Height();
    uint64_t slideWindow;
    pCdMan->pSysParamCache->GetParam(SysParamType::MEDIAN_PRICE_SLIDE_WINDOW_BLOCKCOUNT, slideWindow);
    // TODO: multi stable coin
    uint64_t bcoinMedianPrice = pCdMan->pPpCache->GetMedianPrice(height, slideWindow, CoinPricePair(SYMB::WICC, SYMB::USD));

    uint256 cdpTxId(uint256S(params[0].get_str()));
    CUserCDP cdp;
    if (!pCdMan->pCdpCache->GetCDP(cdpTxId, cdp)) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, strprintf("CDP (%s) does not exist!", cdpTxId.GetHex()));
    }

    Object obj;
    obj.push_back(Pair("cdp", cdp.ToJson(bcoinMedianPrice)));
    return obj;
}

Value getclosedcdp(const Array& params, bool fHelp) {
    if(fHelp || params.size() != 1){
        throw  runtime_error(
                "getclosedcdp \"[cdp_id | close_txid]\"\n"
                "\nget closed CDP by its CDP_ID or CDP_CLOSE_TXID, you must provide one of CDP_ID and CDP_CLOSE_TXID \n"
                "\nArguments:\n"
                "1.\"cdp_id or cdp_close_txid\": (string, required) the closed cdp's or the txid that close the cdp\n"
                "\nResult:\n"
                "\n1 cdp_id: the id of closed cdp\n"
                "\n2 cdp_close_txid: the txid that closed this cdp\n"
                "\n3 cdp_close_type: the reason of closing cdp\n"
                "\nExamples:\n"
                + HelpExampleCli("getclosedcdp", "\"c01f0aefeeb25fd6afa596f27ee3a1e861b657d2e1c341bfd1c412e87d9135c8\"\n")
                + "\nAs json rpc call\n"
                + HelpExampleRpc("getclosedcdp", "\"c01f0aefeeb25fd6afa596f27ee3a1e861b657d2e1c341bfd1c412e87d9135c8\"\n")
                ) ;
    }

    uint256 id = uint256S(params[0].get_str()) ;
    std::pair<uint256, uint8_t> cdp ;
    Object obj ;
    if( pCdMan->pClosedCdpCache->GetClosedCdpById(id,cdp)){
        obj.push_back(Pair("cdp_id", params[0].get_str())) ;
        obj.push_back(Pair("cdp_close_txid", std::get<0>(cdp).GetHex()));
        obj.push_back(Pair("cdp_close_type", GetCdpCloseTypeName((CDPCloseType)std::get<1>(cdp))));
        return obj ;
    }

    if( pCdMan->pClosedCdpCache->GetClosedCdpByTxId(id,cdp)){
        obj.push_back(Pair("cdp_id", std::get<0>(cdp).GetHex())) ;
        obj.push_back(Pair("cdp_close_txid", params[0].get_str())) ;
        obj.push_back(Pair("cdp_close_type", GetCdpCloseTypeName((CDPCloseType)std::get<1>(cdp)))) ;
        return obj ;
    }

    throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, strprintf("Closed CDP (%s) does not exist!", params[0].get_str()));


}

///////////////////////////////////////////////////////////////////////////////
// asset tx rpc

Value submitassetissuetx(const Array& params, bool fHelp) {
    if (fHelp || params.size() < 6 || params.size() > 7) {
        throw runtime_error(
            "submitassetissuetx \"addr\" \"asset_symbol\" \"asset_owner_addr\" \"asset_name\" total_supply mintable [symbol:fee:unit]\n"
            "\nsubmit an asset issue tx.\n"
            "\nthe tx creator must have enough WICC for issued fee(550 WICC).\n"
            "\nArguments:\n"
            "1.\"addr\":            (string, required) tx owner address\n"
            "2.\"asset_symbol\":    (string, required) asset symbol, must be composed of 6 or 7 capital letters [A-Z]\n"
            "3.\"asset_owner_addr\":(string, required) asset owner address, can be same as tx owner address\n"
            "4.\"asset_name\":      (string, required) asset long name, E.g WaykiChain coin\n"
            "5.\"total_supply\":    (numeric, required) asset total supply\n"
            "6.\"mintable\":        (boolean, required) whether this asset token can be minted in the future\n"
            "7.\"symbol:fee:unit\": (string:numeric:string, optional) fee paid for miner, default is WICC:10000:sawi\n"
            "\nResult:\n"
            "\"txid\"               (string) The new transaction id.\n"
            "\nExamples:\n"
            + HelpExampleCli("submitassetissuetx", "\"10-2\" \"CNY\" \"10-2\" \"RMB\" 1000000000000000 true")
            + "\nAs json rpc call\n"
            + HelpExampleRpc("submitassetissuetx", "\"10-2\", \"CNY\", \"10-2\", \"RMB\", 1000000000000000, true")
        );
    }

    EnsureWalletIsUnlocked();

    const CUserID& uid             = RPC_PARAM::GetUserId(params[0]);
    const TokenSymbol& assetSymbol = RPC_PARAM::GetAssetIssueSymbol(params[1]);
    const CUserID& assetOwnerUid   = RPC_PARAM::GetUserId(params[2]);
    const TokenName& assetName     = RPC_PARAM::GetAssetName(params[3]);
    int64_t totalSupply            = params[4].get_int64();
    if (totalSupply <= 0 || (uint64_t)totalSupply > MAX_ASSET_TOTAL_SUPPLY)
        throw JSONRPCError(RPC_INVALID_PARAMS,
                           strprintf("asset total_supply=%lld can not <= 0 or > %llu", totalSupply, MAX_ASSET_TOTAL_SUPPLY));
    bool mintable    = params[5].get_bool();
    ComboMoney cmFee = RPC_PARAM::GetFee(params, 6, TxType::ASSET_ISSUE_TX);

    // Get account for checking balance
    CAccount account = RPC_PARAM::GetUserAccount(*pCdMan->pAccountCache, uid);
    RPC_PARAM::CheckAccountBalance(account, cmFee.symbol, SUB_FREE, cmFee.GetSawiAmount());

    uint64_t assetIssueFee; //550 WICC
    if (!pCdMan->pSysParamCache->GetParam(ASSET_ISSUE_FEE, assetIssueFee))
        throw JSONRPCError(RPC_INTERNAL_ERROR, "read system param ASSET_ISSUE_FEE error");
    RPC_PARAM::CheckAccountBalance(account, SYMB::WICC, SUB_FREE, assetIssueFee);

    int32_t validHeight = chainActive.Height();
    CAccount ownerAccount;
    CRegID *pOwnerRegid;
    if (account.IsMyUid(assetOwnerUid)) {
        pOwnerRegid = &account.regid;
    } else {
        ownerAccount = RPC_PARAM::GetUserAccount(*pCdMan->pAccountCache, assetOwnerUid);
        pOwnerRegid = &ownerAccount.regid;
    }

    if (pOwnerRegid->IsEmpty() || !pOwnerRegid->IsMature(validHeight)) {
        throw JSONRPCError(RPC_INTERNAL_ERROR, strprintf("owner regid=%s is not registerd or not mature",
            pOwnerRegid->ToString()));
    }

    CBaseAsset asset(assetSymbol, CUserID(*pOwnerRegid), assetName, (uint64_t)totalSupply, mintable);
    CAssetIssueTx tx(uid, validHeight, cmFee.symbol, cmFee.GetSawiAmount(), asset);
    return SubmitTx(account.keyid, tx);
}

Value submitassetupdatetx(const Array& params, bool fHelp) {
    if (fHelp || params.size() < 4 || params.size() > 5) {
        throw runtime_error(
            "submitassetupdatetx \"addr\" \"asset_symbol\" \"update_type\" \"update_value\" [symbol:fee:unit]\n"
            "\nsubmit an asset update tx.\n"
            "\nthe tx creator must have enough WICC for asset update fee(200 WICC).\n"
            "\nArguments:\n"
            "1.\"addr\":            (string, required) tx owner address\n"
            "2.\"asset_symbol\":    (string, required) asset symbol, must be composed of 6 or 7 capital letters [A-Z]\n"
            "3.\"update_type\":     (string, required) asset update type, can be (owner_addr, name, mint_amount)\n"
            "4.\"update_value\":    (string, required) update the value specified by update_type, value format see the submitassetissuetx\n"
            "5.\"symbol:fee:unit\": (string:numeric:string, optional) fee paid for miner, default is WICC:10000:sawi\n"
            "\nResult:\n"
            "\"txid\"               (string) The new transaction id.\n"
            "\nExamples:\n"
            + HelpExampleCli("submitassetupdatetx", "\"10-2\" \"CNY\" \"mint_amount\" \"100000000\"")
            + "\nAs json rpc call\n"
            + HelpExampleRpc("submitassetupdatetx", "\"10-2\", \"CNY\", \"mint_amount\", \"100000000\"")
        );
    }

    EnsureWalletIsUnlocked();

    const CUserID& uid             = RPC_PARAM::GetUserId(params[0]);
    const TokenSymbol& assetSymbol = RPC_PARAM::GetAssetIssueSymbol(params[1]);
    const string &updateTypeStr = params[2].get_str();
    const Value &jsonUpdateValue = params[3].get_str();

    auto pUpdateType = CAssetUpdateData::ParseUpdateType(updateTypeStr);
    if (!pUpdateType)
        throw JSONRPCError(RPC_INVALID_PARAMS, strprintf("Invalid update_type=%s", updateTypeStr));

    CAssetUpdateData updateData;
    switch(*pUpdateType) {
        case CAssetUpdateData::OWNER_UID: {
            const string &valueStr = jsonUpdateValue.get_str();
            auto pNewOwnerUid = CUserID::ParseUserId(valueStr);
            if (!pNewOwnerUid) {
                throw JSONRPCError(RPC_INVALID_PARAMS, strprintf("Invalid UserID format of owner_uid=%s",
                    valueStr));
            }
            updateData.Set(*pNewOwnerUid);
            break;
        }
        case CAssetUpdateData::NAME: {
            const string &valueStr = jsonUpdateValue.get_str();
            if (valueStr.size() == 0 || valueStr.size() > MAX_ASSET_NAME_LEN) {
                throw JSONRPCError(RPC_INVALID_PARAMS, strprintf("invalid asset name! empty, or length=%d greater than %d",
                    valueStr.size(), MAX_ASSET_NAME_LEN));
            }
            updateData.Set(valueStr);
            break;
        }
        case CAssetUpdateData::MINT_AMOUNT: {
            uint64_t mintAmount;
            if (jsonUpdateValue.type() == json_spirit::Value_type::int_type ) {
                int64_t v = jsonUpdateValue.get_int64();
                if (v < 0)
                    throw JSONRPCError(RPC_INVALID_PARAMS, strprintf("invalid mint amount=%lld as uint64_t type",
                        v, MAX_ASSET_NAME_LEN));
                mintAmount = v;
            } else if (jsonUpdateValue.type() == json_spirit::Value_type::str_type) {
                const string &valueStr = jsonUpdateValue.get_str();
                if (!ParseUint64(valueStr, mintAmount))
                    throw JSONRPCError(RPC_INVALID_PARAMS, strprintf("invalid mint_amount=%s as uint64_t type",
                        valueStr, MAX_ASSET_NAME_LEN));
            } else
                throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Invalid json value type: %s",
                    JSON::GetValueTypeName(jsonUpdateValue.type())));

            if (mintAmount == 0 || mintAmount > MAX_ASSET_TOTAL_SUPPLY)
                throw JSONRPCError(RPC_INVALID_PARAMS, strprintf("Invalid asset mint_amount=%llu, cannot be 0, or greater than %llu",
                    mintAmount, MAX_ASSET_TOTAL_SUPPLY));

            updateData.Set(mintAmount);
            break;
        }
        default: {
            throw JSONRPCError(RPC_INVALID_PARAMS, strprintf("unsupported updated_key=%s", updateTypeStr));
        }
    }

    ComboMoney cmFee = RPC_PARAM::GetFee(params, 4, TxType::ASSET_UPDATE_TX);

    // Get account for checking balance
    CAccount account = RPC_PARAM::GetUserAccount(*pCdMan->pAccountCache, uid);
    RPC_PARAM::CheckAccountBalance(account, cmFee.symbol, SUB_FREE, cmFee.GetSawiAmount());

    uint64_t assetUpdateFee;
    if (!pCdMan->pSysParamCache->GetParam(ASSET_UPDATE_FEE, assetUpdateFee))
        throw JSONRPCError(RPC_INTERNAL_ERROR, "read system param ASSET_UPDATE_FEE error");
    RPC_PARAM::CheckAccountBalance(account, SYMB::WICC, SUB_FREE, assetUpdateFee);

    int32_t validHeight = chainActive.Height();

    if (*pUpdateType == CAssetUpdateData::OWNER_UID) {
        CUserID &ownerUid = updateData.get<CUserID>();
        if (account.IsMyUid(ownerUid))
            return JSONRPCError(RPC_INVALID_PARAMS, strprintf("the new owner uid=%s is belong to old owner account",
                    ownerUid.ToDebugString()));

        CAccount newAccount = RPC_PARAM::GetUserAccount(*pCdMan->pAccountCache, ownerUid);
        if (!newAccount.IsRegistered())
            return JSONRPCError(RPC_INVALID_PARAMS, strprintf("the new owner account is not registered! new uid=%s",
                    ownerUid.ToDebugString()));
        if (!newAccount.regid.IsMature(validHeight))
            return JSONRPCError(RPC_INVALID_PARAMS, strprintf("the new owner regid is not matured! new uid=%s",
                ownerUid.ToDebugString()));
        ownerUid = newAccount.regid;
    }

    CAssetUpdateTx tx(uid, validHeight, cmFee.symbol, cmFee.GetSawiAmount(), assetSymbol, updateData);
    return SubmitTx(account.keyid, tx);
}

extern Value getasset(const Array& params, bool fHelp) {
     if (fHelp || params.size() < 1 || params.size() > 1) {
        throw runtime_error(
            "getasset \"asset_symbol\"\n"
            "\nget asset by symbol.\n"
            "\nArguments:\n"
            "1.\"aset_symbol\":            (string, required) asset symbol\n"
            "\nExamples:\n"
            + HelpExampleCli("getasset", "MINEUSD")
            + "\nAs json rpc call\n"
            + HelpExampleRpc("getasset", "MINEUSD")
        );
    }
    const TokenSymbol& assetSymbol = RPC_PARAM::GetAssetIssueSymbol(params[0]);

    CAsset asset;
    if (!pCdMan->pAssetCache->GetAsset(assetSymbol, asset))
        throw JSONRPCError(RPC_INVALID_PARAMS, strprintf("asset not exist! asset_symbol=%s", assetSymbol));

    Object obj = AssetToJson(*pCdMan->pAccountCache, asset);
    return obj;
}

extern Value getassets(const Array& params, bool fHelp) {
     if (fHelp || params.size() > 0) {
        throw runtime_error(
            "getassets\n"
            "\nget all assets.\n"
            "\nArguments:\n"
            "\nResult: a list of assets\n"
            "\nExamples:\n"
            + HelpExampleCli("getassets", "")
            + "\nAs json rpc call\n"
            + HelpExampleRpc("getassets", "")
        );
    }

    auto pAssetsIt = pCdMan->pAssetCache->CreateUserAssetsIterator();
    if (!pAssetsIt) {
        throw JSONRPCError(RPC_INVALID_PARAMS, "get all user issued assets iterator error!");
    }

    Array assetArray;
    // TODO: need page??
    int64_t count = 0;
    for (pAssetsIt->First(); pAssetsIt->IsValid(); pAssetsIt->Next(), count++) {
        assetArray.push_back(AssetToJson(*pCdMan->pAccountCache, pAssetsIt->GetAsset()));
    }

    Object obj;
    obj.push_back(Pair("count",     count));
    obj.push_back(Pair("assets",    assetArray));
    return obj;
}