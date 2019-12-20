// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2017-2019 The WaykiChain Developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef TX_SERIALIZER_H
#define TX_SERIALIZER_H

#include <stdexcept>
#include "commons/serialize.h"
#include "tx/tx.h"

#include "tx/accountregtx.h"
#include "tx/cointransfertx.h"
#include "tx/blockpricemediantx.h"
#include "tx/blockrewardtx.h"
#include "tx/cdptx.h"
#include "tx/coinrewardtx.h"
#include "tx/cointransfertx.h"
#include "tx/contracttx.h"
#include "tx/delegatetx.h"
#include "tx/dextx.h"
#include "tx/coinstaketx.h"
#include "tx/mulsigtx.h"
#include "tx/pricefeedtx.h"
#include "tx/tx.h"
#include "tx/txmempool.h"
#include "tx/assettx.h"
#include "tx/wasmcontracttx.h"
#include "tx/nickidregtx.h"

using namespace std;

class EInvalidTxType: public runtime_error {
public:
    using runtime_error::runtime_error;
};


template<typename Stream>
void CBaseTx::SerializePtr(Stream& os, const std::shared_ptr<CBaseTx> &pBaseTx, int serType, int version) {

    // if (!pBaseTx) {
    //     throw EInvalidTxType(strprintf("%s(), unsupport null tx type to serialize",
    //         __FUNCTION__));
    // }
    const CBaseTx &tx = *pBaseTx;
    uint8_t txType = tx.nTxType;
    ::Serialize(os, txType, serType, version);
    switch (txType) {
        case BLOCK_REWARD_TX:
            ::Serialize(os, (const CBlockRewardTx&)tx, serType, version); break;
        case ACCOUNT_REGISTER_TX:
            ::Serialize(os, (const CAccountRegisterTx&)tx, serType, version); break;
        case BCOIN_TRANSFER_TX:
            ::Serialize(os, (const CBaseCoinTransferTx&)tx, serType, version); break;
        case LCONTRACT_INVOKE_TX:
            ::Serialize(os, (const CLuaContractInvokeTx&)tx, serType, version); break;
        case LCONTRACT_DEPLOY_TX:
            ::Serialize(os, (const CLuaContractDeployTx&)tx, serType, version); break;
        case DELEGATE_VOTE_TX:
            ::Serialize(os, (const CDelegateVoteTx&)tx, serType, version); break;

        case UCOIN_TRANSFER_MTX:
            ::Serialize(os, (const CMulsigTx&)tx, serType, version); break;
        case UCOIN_STAKE_TX:
            ::Serialize(os, (const CCoinStakeTx&)tx, serType, version); break;
        case ASSET_ISSUE_TX:
            ::Serialize(os, (const CAssetIssueTx&)tx, serType, version); break;
        case ASSET_UPDATE_TX:
            ::Serialize(os, (const CAssetUpdateTx&)tx, serType, version); break;

        case UCOIN_TRANSFER_TX:
            ::Serialize(os, (const CCoinTransferTx&)tx, serType, version); break;
        case UCOIN_REWARD_TX:
            ::Serialize(os, (const CCoinRewardTx&)tx, serType, version); break;
        case UCOIN_BLOCK_REWARD_TX:
            ::Serialize(os, (const CUCoinBlockRewardTx&)tx, serType, version); break;
        case UCONTRACT_DEPLOY_TX:
            ::Serialize(os, (const CUniversalContractDeployTx&)tx, serType, version); break;
        case UCONTRACT_INVOKE_TX:
            ::Serialize(os, (const CUniversalContractInvokeTx&)tx, serType, version); break;
        case PRICE_FEED_TX:
            ::Serialize(os, (const CPriceFeedTx&)tx, serType, version); break;
        case PRICE_MEDIAN_TX:
            ::Serialize(os, (const CBlockPriceMedianTx&)tx, serType, version); break;

        case CDP_STAKE_TX:
            ::Serialize(os, (const CCDPStakeTx&)tx, serType, version); break;
        case CDP_REDEEM_TX:
            ::Serialize(os, (const CCDPRedeemTx&)tx, serType, version); break;
        case CDP_LIQUIDATE_TX:
            ::Serialize(os, (const CCDPLiquidateTx&)tx, serType, version); break;

        case NICKID_REGISTER_TX:
            ::Serialize(os, (const CNickIdRegisterTx&)tx, serType, version); break;

        case WASM_CONTRACT_TX:
            ::Serialize(os, (const CWasmContractTx&)tx, serType, version); break;

        case DEX_TRADE_SETTLE_TX:
            ::Serialize(os, (const CDEXSettleTx&)tx, serType, version); break;
        case DEX_CANCEL_ORDER_TX:
            ::Serialize(os, (const CDEXCancelOrderTx&)tx, serType, version); break;
        case DEX_LIMIT_BUY_ORDER_TX:
            ::Serialize(os, (const CDEXBuyLimitOrderTx&)tx, serType, version); break;
        case DEX_LIMIT_SELL_ORDER_TX:
            ::Serialize(os, (const CDEXSellLimitOrderTx&)tx, serType, version); break;
        case DEX_MARKET_BUY_ORDER_TX:
            ::Serialize(os, (const CDEXBuyMarketOrderTx&)tx, serType, version); break;
        case DEX_MARKET_SELL_ORDER_TX:
            ::Serialize(os, (const CDEXSellMarketOrderTx&)tx, serType, version); break;
        case DEX_LIMIT_BUY_ORDER_EX_TX:
            ::Serialize(os, (const CDEXBuyLimitOrderExTx&)tx, serType, version); break;
        case DEX_LIMIT_SELL_ORDER_EX_TX:
            ::Serialize(os, (const CDEXSellLimitOrderExTx&)tx, serType, version); break;
        case DEX_MARKET_BUY_ORDER_EX_TX:
            ::Serialize(os, (const CDEXBuyMarketOrderExTx&)tx, serType, version); break;
        case DEX_MARKET_SELL_ORDER_EX_TX:
            ::Serialize(os, (const CDEXSellMarketOrderExTx&)tx, serType, version); break;

        default:
            throw EInvalidTxType(strprintf("%s(), unsupport nTxType(%d:%s) to serialize",
                __FUNCTION__, tx.nTxType, GetTxType(tx.nTxType)));
            break;
    }
}

template<typename Stream>
void CBaseTx::UnserializePtr(Stream& is, std::shared_ptr<CBaseTx> &pBaseTx, int serType, int version) {
    uint8_t nTxType;
    is.read((char *)&(nTxType), sizeof(nTxType));
    switch((TxType)nTxType) {
        case BLOCK_REWARD_TX: {
            pBaseTx = std::make_shared<CBlockRewardTx>();
            ::Unserialize(is, *((CBlockRewardTx *)(pBaseTx.get())), serType, version);
            break;
        }
        case ACCOUNT_REGISTER_TX: {
            pBaseTx = std::make_shared<CAccountRegisterTx>();
            ::Unserialize(is, *((CAccountRegisterTx *)(pBaseTx.get())), serType, version);
            break;
        }
        case BCOIN_TRANSFER_TX: {
            pBaseTx = std::make_shared<CBaseCoinTransferTx>();
            ::Unserialize(is, *((CBaseCoinTransferTx *)(pBaseTx.get())), serType, version);
            break;
        }
        case LCONTRACT_INVOKE_TX: {
            pBaseTx = std::make_shared<CLuaContractInvokeTx>();
            ::Unserialize(is, *((CLuaContractInvokeTx *)(pBaseTx.get())), serType, version);
            break;
        }
        case LCONTRACT_DEPLOY_TX: {
            pBaseTx = std::make_shared<CLuaContractDeployTx>();
            ::Unserialize(is, *((CLuaContractDeployTx *)(pBaseTx.get())), serType, version);
            break;
        }
        case DELEGATE_VOTE_TX: {
            pBaseTx = std::make_shared<CDelegateVoteTx>();
            ::Unserialize(is, *((CDelegateVoteTx *)(pBaseTx.get())), serType, version);
            break;
        }

        case UCOIN_TRANSFER_MTX: {
            pBaseTx = std::make_shared<CMulsigTx>();
            ::Unserialize(is, *((CMulsigTx *)(pBaseTx.get())), serType, version);
            break;
        }

        case UCOIN_STAKE_TX: {
            pBaseTx = std::make_shared<CCoinStakeTx>();
            ::Unserialize(is, *((CCoinStakeTx *)(pBaseTx.get())), serType, version);
            break;
        }

        case ASSET_ISSUE_TX: {
            pBaseTx = std::make_shared<CAssetIssueTx>();
            ::Unserialize(is, *((CAssetIssueTx *)(pBaseTx.get())), serType, version);
            break;
        }

        case ASSET_UPDATE_TX: {
            pBaseTx = std::make_shared<CAssetUpdateTx>();
            ::Unserialize(is, *((CAssetUpdateTx *)(pBaseTx.get())), serType, version);
            break;
        }

        case UCOIN_TRANSFER_TX: {
            pBaseTx = std::make_shared<CCoinTransferTx>();
            ::Unserialize(is, *((CCoinTransferTx *)(pBaseTx.get())), serType, version);
            break;
        }
        case UCOIN_REWARD_TX: {
            pBaseTx = std::make_shared<CCoinRewardTx>();
            ::Unserialize(is, *((CCoinRewardTx *)(pBaseTx.get())), serType, version);
            break;
        }
        case UCOIN_BLOCK_REWARD_TX: {
            pBaseTx = std::make_shared<CUCoinBlockRewardTx>();
            ::Unserialize(is, *((CUCoinBlockRewardTx *)(pBaseTx.get())), serType, version);
            break;
        }
        case UCONTRACT_DEPLOY_TX: {
            pBaseTx = std::make_shared<CUniversalContractDeployTx>();
            ::Unserialize(is, *((CUniversalContractDeployTx *)(pBaseTx.get())), serType, version);
            break;
        }
        case UCONTRACT_INVOKE_TX: {
            pBaseTx = std::make_shared<CUniversalContractInvokeTx>();
            ::Unserialize(is, *((CUniversalContractInvokeTx *)(pBaseTx.get())), serType, version);
            break;
        }
        case PRICE_FEED_TX: {
            pBaseTx = std::make_shared<CPriceFeedTx>();
            ::Unserialize(is, *((CPriceFeedTx *)(pBaseTx.get())), serType, version);
            break;
        }
        case PRICE_MEDIAN_TX: {
            pBaseTx = std::make_shared<CBlockPriceMedianTx>();
            ::Unserialize(is, *((CBlockPriceMedianTx *)(pBaseTx.get())), serType, version);
            break;
        }

        case CDP_STAKE_TX: {
            pBaseTx = std::make_shared<CCDPStakeTx>();
            ::Unserialize(is, *((CCDPStakeTx *)(pBaseTx.get())), serType, version);
            break;
        }
        case CDP_REDEEM_TX: {
            pBaseTx = std::make_shared<CCDPRedeemTx>();
            ::Unserialize(is, *((CCDPRedeemTx *)(pBaseTx.get())), serType, version);
            break;
        }
        case CDP_LIQUIDATE_TX: {
            pBaseTx = std::make_shared<CCDPLiquidateTx>();
            ::Unserialize(is, *((CCDPLiquidateTx *)(pBaseTx.get())), serType, version);
            break;
        }

        case NICKID_REGISTER_TX: {
            pBaseTx = std::make_shared<CNickIdRegisterTx>();
            ::Unserialize(is, *((CNickIdRegisterTx *)(pBaseTx.get())), serType, version);
            break;
        }

        case WASM_CONTRACT_TX: {
            pBaseTx = std::make_shared<CWasmContractTx>();
            ::Unserialize(is, *((CWasmContractTx *)(pBaseTx.get())), serType, version);
            break;
        }

        case DEX_TRADE_SETTLE_TX: {
            pBaseTx = std::make_shared<CDEXSettleTx>();
            ::Unserialize(is, *((CDEXSettleTx *)(pBaseTx.get())), serType, version);
            break;
        }
        case DEX_CANCEL_ORDER_TX: {
            pBaseTx = std::make_shared<CDEXCancelOrderTx>();
            ::Unserialize(is, *((CDEXCancelOrderTx *)(pBaseTx.get())), serType, version);
            break;
        }
        case DEX_LIMIT_BUY_ORDER_TX: {
            pBaseTx = std::make_shared<CDEXBuyLimitOrderTx>();
            ::Unserialize(is, *((CDEXBuyLimitOrderTx *)(pBaseTx.get())), serType, version);
            break;
        }
        case DEX_LIMIT_SELL_ORDER_TX: {
            pBaseTx = std::make_shared<CDEXSellLimitOrderTx>();
            ::Unserialize(is, *((CDEXSellLimitOrderTx *)(pBaseTx.get())), serType, version);
            break;
        }
        case DEX_MARKET_BUY_ORDER_TX: {
            pBaseTx = std::make_shared<CDEXBuyMarketOrderTx>();
            ::Unserialize(is, *((CDEXBuyMarketOrderTx *)(pBaseTx.get())), serType, version);
            break;
        }
        case DEX_MARKET_SELL_ORDER_TX: {
            pBaseTx = std::make_shared<CDEXSellMarketOrderTx>();
            ::Unserialize(is, *((CDEXSellMarketOrderTx *)(pBaseTx.get())), serType, version);
            break;
        }
        case DEX_LIMIT_BUY_ORDER_EX_TX: {
            pBaseTx = std::make_shared<CDEXBuyLimitOrderExTx>();
            ::Unserialize(is, *((CDEXBuyLimitOrderExTx *)(pBaseTx.get())), serType, version);
            break;
        }
        case DEX_LIMIT_SELL_ORDER_EX_TX: {
            pBaseTx = std::make_shared<CDEXSellLimitOrderExTx>();
            ::Unserialize(is, *((CDEXSellLimitOrderExTx *)(pBaseTx.get())), serType, version);
            break;
        }
        case DEX_MARKET_BUY_ORDER_EX_TX: {
            pBaseTx = std::make_shared<CDEXBuyMarketOrderExTx>();
            ::Unserialize(is, *((CDEXBuyMarketOrderExTx *)(pBaseTx.get())), serType, version);
            break;
        }
        case DEX_MARKET_SELL_ORDER_EX_TX: {
            pBaseTx = std::make_shared<CDEXSellMarketOrderExTx>();
            ::Unserialize(is, *((CDEXSellMarketOrderExTx *)(pBaseTx.get())), serType, version);
            break;
        }

        default:
            throw EInvalidTxType(strprintf("%s(), unsupport nTxType(%d:%s) to unserialize",
                __FUNCTION__, pBaseTx->nTxType, GetTxType(pBaseTx->nTxType)));
    }
    pBaseTx->nTxType = TxType(nTxType);
}

#endif //TX_SERIALIZER_H
