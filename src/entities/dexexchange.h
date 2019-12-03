// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2017-2019 The WaykiChain Developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.


#ifndef ENTITIES_DEX_EXCHANGE_H
#define ENTITIES_DEX_EXCHANGE_H

#include "commons/serialize.h"
#include "id.h"
#include "commons/json/json_spirit.h"

#include <boost/variant.hpp>

// #include <string>

// #include "id.h"
// #include "asset.h"
// #include "commons/types.h"

namespace dex {

    /**
     * match fee ratio map
     * key: uint8_t     ratio type, range: [0, 100]
     * value: uint64_t  ratio value, boost 10000, range: [0, 100 * 10000]
     */
    typedef std::map<uint8_t, CVarIntValue<uint64_t>> MatchFeeRatioMap;

    class CBaseExchange {
    public:
        CUserID owner_uid;                          // owner uid of exchange
        string domain_name;                         // domain name
        CUserID match_uid;                       // matching uid
        MatchFeeRatioMap match_fee_ratio_map;    // match fee ratio map

        CBaseExchange() {}

        CBaseExchange(const CUserID &ownerUidIn, const string &domainNameIn,
                        const CUserID &matchUidIn, const MatchFeeRatioMap &orderFeeRatioMapIn)
            : owner_uid(ownerUidIn),
            domain_name(domainNameIn),
            match_uid(matchUidIn),
            match_fee_ratio_map(orderFeeRatioMapIn) {}

        IMPLEMENT_SERIALIZE(
            READWRITE(owner_uid);
            READWRITE(domain_name);
            READWRITE(match_uid);
            READWRITE(match_fee_ratio_map);
        )
    };

    enum class ExchangeKey : uint8_t {
        UPDATE_NONE         = 0,
        OWNER_UID           = 1,
        DOMAIN_NAME         = 2,
        MATCH_UID           = 3,
        MATCH_FEE_RATIO_MAP = 4
    };
    static const ExchangeKey EXCHANGE_FIELD_MAX = ExchangeKey::MATCH_FEE_RATIO_MAP;

    class CExchangeUpdateData {
    public:
        typedef boost::variant<
            CNullObject,            // none
            CUserID,                // owner_uid or match_uid
            string,                 // domain_name
            MatchFeeRatioMap     // match_fee_ratio_map
        > UpdateValue;
    private:
        ExchangeKey key; // update key
        UpdateValue value; // update value

    public:
        //static std::shared_ptr<UpdateType> ParseUpdateType(const string& str);

        //static const string& GetUpdateTypeName(UpdateType type);
    public:
        CExchangeUpdateData(): key(ExchangeKey::UPDATE_NONE), value(CNullObject()) {}

        template <typename ValueType>
        void Set(const ExchangeKey &keyIn, const ValueType &valueIn) {
            key = keyIn;
            value = valueIn;
        }

        const ExchangeKey& GetField() const { return key; }

         template <typename ValueType>
        ValueType &GetValue() {
            return boost::get<ValueType>(value);
        }

        template <typename ValueType>
        const ValueType &GetValue() const {
            return boost::get<ValueType>(value);
        }

    public:
        struct SerializeOptions {
            int32_t type;
            int32_t version;

            SerializeOptions() {}
            SerializeOptions(int32_t typeIn, int32_t versionIn): type(typeIn), version(versionIn) {}
        };

        class CGetSerializeSizeVisitor: public boost::static_visitor<uint32_t> {
        public:
            SerializeOptions options;
            CGetSerializeSizeVisitor(int32_t typeIn, int32_t versionIn): options(typeIn, versionIn) {}

            template<typename T>
            uint32_t operator()(const T& t) const {
                return ::GetSerializeSize(t, options.type, options.version);
            }
        };

        template<typename Stream>
        class CSerializeVisitor: public boost::static_visitor<void> {
        public:
            Stream &stream;
            SerializeOptions options;
            CSerializeVisitor(Stream &streamIn, int32_t typeIn, int32_t versionIn)
                : stream(streamIn), options(typeIn, versionIn) {}

            template<typename T>
            void operator()(const T& t) const {
                ::Serialize(stream, t, options.type, options.version);
            }
        };

        template<typename Stream>
        class CUnserializeVisitor: public boost::static_visitor<void> {
        public:
            Stream &stream;
            SerializeOptions options;
            CUnserializeVisitor(Stream &streamIn, int32_t typeIn, int32_t versionIn)
                : stream(streamIn), options(typeIn, versionIn) {}

            template<typename T>
            void operator()(const T& t) const {
                ::Serialize(stream, t, options.type, options.version);
            }
        };

        void CheckSerializeKey() const {
            if (key <= ExchangeKey::UPDATE_NONE && key > EXCHANGE_FIELD_MAX) {
                LogPrint(BCLog::ERROR, "%s(), Invalid exchange update type=%d\n", __func__, (int32_t)key);
                throw ios_base::failure("Invalid exchange update key");
            }
        }

        inline uint32_t GetSerializeSize(int32_t type, int32_t version) const {
            CheckSerializeKey();
            return sizeof(uint8_t) + boost::apply_visitor(CGetSerializeSizeVisitor(type, version), value);
        }

        template <typename Stream>
        void Serialize(Stream &s, int type, int version) const {
            CheckSerializeKey();
            s << (uint8_t)type;
            boost::apply_visitor(CSerializeVisitor(s, type, version), value);
        }

        template <typename Stream>
        void Unserialize(Stream &s, int type, int version) {
            s >> ((uint8_t&)key);
            CheckSerializeKey();
            switch (key) { // create instance of value content
                case ExchangeKey::OWNER_UID:             value = CUserID(); break;
                case ExchangeKey::DOMAIN_NAME:           value = string(); break;
                case ExchangeKey::OWNER_UID:             value = CUserID(); break;
                case ExchangeKey::MATCH_FEE_RATIO_MAP:   value = MatchFeeRatioMap(); break;
                default: assert(false && "has been checked in CheckSerializeKey()"); break;
            }
            boost::apply_visitor(CUnserializeVisitor(s, type, version), value);
        }

        string ValueToString() const;

        string ToString(const CAccountDBCache &accountCache) const;

        json_spirit::Object ToJson(const CAccountDBCache &accountCache) const;
    };

    /**
     * serialize map values only
     * the value must support value.GetKey() function
     */
    template <typename MapType>
    class CSerializeMapValues {
    public:
        MapType &data_map;
        CSerializeMapValues(MapType &dataMapIn): data_map(dataMapIn) {}

        inline uint32_t GetSerializeSize(int32_t type, int32_t version) const {
            uint32_t sz = GetSizeOfCompactSize(data_map.size());
            for (const auto& item : data_map) {
                sz += ::GetSerializeSize(item.second, type, version);
            }
            return sz;
        }

        template <typename Stream>
        void Serialize(Stream &s, int type, int version) const {
            WriteCompactSize(s, data_map.size());
            for (const auto& item : data_map) {
                ::Serialize(s, item.second, type, version);
            }
        }

        template <typename Stream>
        void Unserialize(Stream &s, int type, int version) {
            data_map.clear();
            uint32_t sz = ReadCompactSize(s);
            for (uint32_t i = 0; i < sz; i++)
            {
                typename MapType::mapped_type v;
                ::Unserialize(s, v, type, version);
                auto ret = data_map.emplace(v.GetKey(), v);
                if (!ret.second) {
                    LogPrint(BCLog::ERROR, "%s(), add new item to map failed\n", __func__);
                    throw ios_base::failure("add new item to map failed");
                }
            }
        }
    };

    class CExchangeUpdateMap {
    public:
        typedef std::map<ExchangeKey, CExchangeUpdateData>  UpdateMap;

        uint32_t exchange_id;    // exchange id
        UpdateMap update_map;   // update data(type, value)

        IMPLEMENT_SERIALIZE(
            READWRITE(exchange_id);
            READWRITE(CSerializeMapValues(update_map));
        )

    };

}

#endif //ENTITIES_DEX_EXCHANGE_H
