/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-12-13
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include "nosqlprotocol.hh"
#include <endian.h>
#include <atomic>
#include <deque>
#include <set>
#include <sstream>
#include <stdexcept>
#include <bsoncxx/array/view.hpp>
#include <bsoncxx/json.hpp>
#include <mongoc/mongoc.h>
#include <maxbase/stopwatch.hh>
#include <maxscale/protocol2.hh>
#include <maxscale/routingworker.hh>
#include <maxscale/session.hh>
#include <maxscale/target.hh>
#include "config.hh"
#include "nosqlbase.hh"
#include "nosqlcursor.hh"
#include "nosqlkeys.hh"
#include "nosqlscram.hh"
#include "nosqlusermanager.hh"
#include "../../filter/masking/mysql.hh"

class DCB;

class ClientConnection;
class Config;
class ComERR;

namespace mariadb
{

enum class Op
{
    EQ,
    GT,
    GTE,
    LT,
    LTE,
    NE,
};

const char* to_string(Op op);

/**
 * Get the MariaDB account
 *
 * @param db    The NoSQL database.
 * @param user  The user name.
 * @param host  The host.
 *
 * @return A properly quoted and escaped MariaDB account.
 *
 * The MariaDB account will be like 'db.user'@'host'
 */
std::string get_account(std::string db, std::string user, const std::string& host);

/**
 * Get the MariaDB user name
 *
 * @param db    The NoSQL database.
 * @param user  The user name.
 *
 * @return A properly escaped MariaDB user name.
 */
std::string get_user_name(std::string db, std::string user);

}

inline std::ostream& operator << (std::ostream& out, mariadb::Op op)
{
    out << mariadb::to_string(op);
    return out;
}

namespace nosql
{

namespace protocol
{

namespace type
{
const int32_t DOUBLE = 1;
const int32_t STRING = 2;
const int32_t OBJECT = 3;
const int32_t ARRAY = 4;
const int32_t BIN_DATA = 5;
const int32_t UNDEFINED = 6;
const int32_t OBJECT_ID = 7;
const int32_t BOOL = 8;
const int32_t DATE = 9;
const int32_t NULL_TYPE = 10;
const int32_t REGEX = 11;
const int32_t DB_POINTER = 12;
const int32_t JAVASCRIPT = 13;
const int32_t SYMBOL = 14;
const int32_t JAVASCRIPT_SCOPE = 15;
const int32_t INT32 = 16;
const int32_t TIMESTAMP = 17;
const int32_t INT64 = 18;
const int32_t DECIMAL128 = 19;
const int32_t MIN_KEY = -1;
const int32_t MAX_KEY = 127;

std::string to_alias(int32_t type);

};

namespace alias
{
extern const char* DOUBLE;
extern const char* STRING;
extern const char* OBJECT;
extern const char* ARRAY;
extern const char* BIN_DATA;
extern const char* UNDEFINED;
extern const char* OBJECT_ID;
extern const char* BOOL;
extern const char* DATE;
extern const char* NULL_ALIAS;
extern const char* REGEX;
extern const char* DB_POINTER;
extern const char* JAVASCRIPT;
extern const char* SYMBOL;
extern const char* JAVASCRIPT_SCOPE;
extern const char* INT32;
extern const char* TIMESTAMP;
extern const char* INT64;
extern const char* DECIMAL128;
extern const char* MIN_KEY;
extern const char* MAX_KEY;

int32_t to_type(const std::string& alias);

inline int32_t to_type(const char* zAlias)
{
    return to_type(std::string(zAlias));
}

inline int32_t to_type(const bsoncxx::stdx::string_view& alias)
{
    return to_type(std::string(alias.data(), alias.length()));
}

}

struct HEADER
{
    int32_t msg_len;
    int32_t request_id;
    int32_t response_to;
    int32_t opcode;
};

const int HEADER_LEN = sizeof(HEADER);

const int MAX_BSON_OBJECT_SIZE = 16 * 1024 * 1024;
const int MAX_MSG_SIZE         = 48 * 1000* 1000;
const int MAX_WRITE_BATCH_SIZE = 100000;

inline int32_t get_byte1(const uint8_t* pBuffer, uint8_t* pHost8)
{
    *pHost8 = *pBuffer;
    return 1;
}

inline int32_t get_byte4(const uint8_t* pBuffer, uint32_t* pHost32)
{
    uint32_t le32 = *(reinterpret_cast<const uint32_t*>(pBuffer));
    *pHost32 = le32toh(le32);
    return 4;
}

inline int32_t get_byte4(const uint8_t* pBuffer, int32_t* pHost32)
{
    uint32_t host32;
    auto rv = get_byte4(pBuffer, &host32);
    *pHost32 = host32;
    return rv;
}

inline uint32_t get_byte4(const uint8_t* pBuffer)
{
    uint32_t host32;
    get_byte4(pBuffer, &host32);
    return host32;
}

inline int32_t get_byte8(const uint8_t* pBuffer, uint64_t* pHost64)
{
    uint64_t le64 = *(reinterpret_cast<const uint64_t*>(pBuffer));
    *pHost64 = le64toh(le64);
    return 8;
}

inline int32_t get_byte8(const uint8_t* pBuffer, int64_t* pHost64)
{
    uint64_t host64;
    auto rv = get_byte8(pBuffer, &host64);
    *pHost64 = host64;
    return rv;
}

inline uint64_t get_byte8(const uint8_t* pBuffer)
{
    uint64_t host64;
    get_byte8(pBuffer, &host64);
    return host64;
}

inline int32_t get_zstring(const uint8_t* pBuffer, const char** pzString)
{
    const char* zString = reinterpret_cast<const char*>(pBuffer);
    *pzString = zString;
    return strlen(zString) + 1;
}

int32_t get_document(const uint8_t* pData, const uint8_t* pEnd, bsoncxx::document::view* pView);

inline int32_t set_byte1(uint8_t* pBuffer, uint8_t val)
{
    *pBuffer = val;
    return 1;
}

inline int32_t set_byte4(uint8_t* pBuffer, uint32_t val)
{
    uint32_t le32 = htole32(val);
    auto ple32 = reinterpret_cast<uint32_t*>(pBuffer);
    *ple32 = le32;
    return 4;
}

inline int32_t set_byte8(uint8_t* pBuffer, uint64_t val)
{
    uint64_t le64 = htole64(val);
    auto ple64 = reinterpret_cast<uint64_t*>(pBuffer);
    *ple64 = le64;
    return 8;
}

}

enum class State
{
    BUSY,
    READY
};

// The MongoDB version we claim to be.
const int NOSQL_VERSION_MAJOR = 4;
const int NOSQL_VERSION_MINOR = 4;
const int NOSQL_VERSION_PATCH = 1;

const char* const NOSQL_ZVERSION = "4.4.1";

// See MongoDB: src/mongo/db/wire_version.h, 6 is the version that uses OP_MSG messages.
// Minimum version reported as 0, even though the old protocol versions are not fully
// supported as the MongoDB Shell does not do the right thing if the minimum version is 6.
const int MIN_WIRE_VERSION = 0;
const int MAX_WIRE_VERSION = 6;

const int DEFAULT_CURSOR_RETURN = 101;  // Documented to be that.

bsoncxx::document::value& topology_version();

const char* opcode_to_string(int code);

void append(DocumentBuilder& doc, const string_view& key, const bsoncxx::document::element& element);
inline void append(DocumentBuilder& doc, const std::string& key, const bsoncxx::document::element& element)
{
    append(doc, string_view(key.data(), key.length()), element);
}
inline void append(DocumentBuilder& doc, const char* zKey, const bsoncxx::document::element& element)
{
    append(doc, string_view(zKey), element);
}

namespace value
{

const char COLLECTION[] = "collection";
const char IMMEDIATE[]  = "immediate";
const char MOZJS[]      = "mozjs";
const char MULTI[]      = "multi";
const char SINGLE[]     = "single";
const char UNDECIDED[]  = "undecided";

}

bool get_integer(const bsoncxx::document::element& element, int64_t* pInt);
template<class bsoncxx_document_or_array_element>
bool get_number_as_integer(const bsoncxx_document_or_array_element& element, int64_t* pInt)
{
    bool rv = true;

    switch (element.type())
    {
    case bsoncxx::type::k_int32:
        *pInt = element.get_int32();
        break;

    case bsoncxx::type::k_int64:
        *pInt = element.get_int64();
        break;

    case bsoncxx::type::k_double:
        // Integers are often passed as double.
        *pInt = element.get_double();
        break;

    default:
        rv = false;
    }

    return rv;
}
bool get_number_as_double(const bsoncxx::document::element& element, double* pDouble);

/**
 * Converts an element to a value that can be used in comparisons.
 *
 * @param element  The element to be converted.
 *
 * @return A value expressed as a string; a number will just be the number, but a
 *         string will be enclosed in quotes.
 *
 * @throws SoftError(BAD_VALUE) if the element cannot be converted to a value.
 */
std::string to_string(const bsoncxx::document::element& element);

std::vector<std::string> extractions_from_projection(const bsoncxx::document::view& projection);
std::string columns_from_extractions(const std::vector<std::string>& extractions);

std::string where_condition_from_query(const bsoncxx::document::view& filter);
std::string where_clause_from_query(const bsoncxx::document::view& filter);

std::string order_by_value_from_sort(const bsoncxx::document::view& sort);

std::string set_value_from_update_specification(const bsoncxx::document::view& update_command,
                                                const bsoncxx::document::element& update_specification);

std::string set_value_from_update_specification(const bsoncxx::document::view& update_specification);

namespace packet
{

class Packet
{
public:
    Packet(const Packet&) = default;
    Packet(Packet&& rhs) = default;
    Packet& operator = (const Packet&) = default;
    Packet& operator = (Packet&&) = default;

    Packet(const uint8_t* pData, const uint8_t* pEnd)
        : m_pEnd(pEnd)
        , m_pHeader(reinterpret_cast<const protocol::HEADER*>(pData))
    {
    }

    Packet(const uint8_t* pData, int32_t size)
        : Packet(pData, pData + size)
    {
    }

    Packet(const std::vector<uint8_t>& buffer)
        : Packet(buffer.data(), buffer.data() + buffer.size())
    {
    }

    Packet(const GWBUF* pBuffer)
        : Packet(gwbuf_link_data(pBuffer), gwbuf_link_data(pBuffer) + gwbuf_link_length(pBuffer))
    {
        mxb_assert(gwbuf_is_contiguous(pBuffer));
    }

    int32_t msg_len() const
    {
        return m_pHeader->msg_len;
    }

    int32_t request_id() const
    {
        return m_pHeader->request_id;
    }

    int32_t response_to() const
    {
        return m_pHeader->response_to;
    }

    int32_t opcode() const
    {
        return m_pHeader->opcode;
    }

    enum Details
    {
        LOW_LEVEL = 1,
        HIGH_LEVEL = 2,
        ALL = (LOW_LEVEL | HIGH_LEVEL)
    };

    std::string to_string(uint32_t details, const char* zSeparator) const
    {
        std::ostringstream ss;

        if (details & LOW_LEVEL)
        {
            ss << low_level_to_string(zSeparator);
        }

        if (details & HIGH_LEVEL)
        {
            if (details & LOW_LEVEL)
            {
                ss << zSeparator;
            }

            ss << high_level_to_string(zSeparator);
        }

        return ss.str();
    }

    std::string to_string(uint32_t details) const
    {
        return to_string(details, ", ");
    }

    std::string to_string(const char* zSeparator) const
    {
        return to_string(HIGH_LEVEL, zSeparator);
    }

    std::string to_string() const
    {
        return to_string(HIGH_LEVEL, ", ");
    }

    std::string low_level_to_string(const char* zSeparator) const
    {
        std::ostringstream ss;

        ss << "msg_len: " << msg_len() << zSeparator
           << "request_id: " << request_id() << zSeparator
           << "response_to: " << response_to() << zSeparator
           << "opcode: " << opcode_to_string(opcode());

        return ss.str();
    }

    virtual std::string high_level_to_string(const char* zSeparator) const
    {
        return std::string();
    }

protected:
    const uint8_t*          m_pEnd;
    const protocol::HEADER* m_pHeader;
};

class Insert final : public Packet
{
public:
    Insert(const Packet& packet);
    Insert(Insert&& rhs) = default;

    enum Flags
    {
        CONTINUE_ON_ERROR = 0x01
    };

    uint32_t flags() const
    {
        return m_flags;
    }

    bool is_continue_on_error() const
    {
        return m_flags & CONTINUE_ON_ERROR;
    }

    const char* zCollection() const
    {
        return m_zCollection;
    }

    std::string collection() const
    {
        return m_zCollection;
    }

    const std::vector<bsoncxx::document::view>& documents() const
    {
        return m_documents;
    }

    std::string high_level_to_string(const char* zSeparator) const override
    {
        std::ostringstream ss;

        ss << "collection: " << m_zCollection << zSeparator
           << "continue_on_error: " << (is_continue_on_error() ? "true" : "false") << zSeparator
           << "documents: ";

        auto it = m_documents.begin();

        while (it != m_documents.end())
        {
            ss << bsoncxx::to_json(*it);

            if (++it != m_documents.end())
            {
                ss << ", ";
            }
        }

        return ss.str();
    }

private:
    uint32_t                             m_flags;
    const char*                          m_zCollection;
    std::vector<bsoncxx::document::view> m_documents;
};

class Delete final : public Packet
{
public:
    Delete(const Packet& packet);
    Delete(Delete&& rhs) = default;

    enum Flags
    {
        SINGLE_REMOVE = 1
    };

    const char* zCollection() const
    {
        return m_zCollection;
    }

    std::string collection() const
    {
        return m_zCollection;
    }

    uint32_t flags() const
    {
        return m_flags;
    }

    bool is_single_remove() const
    {
        return m_flags & SINGLE_REMOVE;
    }

    const bsoncxx::document::view& selector() const
    {
        return m_selector;
    }

    std::string high_level_to_string(const char* zSeparator) const override
    {
        std::ostringstream ss;

        ss << "collection: " << m_zCollection << zSeparator
           << "single_remove: " << (is_single_remove() ? "true" : "false") << zSeparator
           << "selector: " << bsoncxx::to_json(m_selector);

        return ss.str();
    }

private:
    const char*             m_zCollection;
    uint32_t                m_flags;
    bsoncxx::document::view m_selector;
};

class Update final : public Packet
{
public:
    Update(const Packet& packet);
    Update(Update&& rhs) = default;

    enum Flags
    {
        UPSERT = 0x01,
        MULTI  = 0x02,
    };

    const char* zCollection() const
    {
        return m_zCollection;
    }

    std::string collection() const
    {
        return m_zCollection;
    }

    uint32_t flags() const
    {
        return m_flags;
    }

    bool is_upsert() const
    {
        return m_flags & UPSERT;
    }

    bool is_multi() const
    {
        return m_flags & MULTI;
    }

    const bsoncxx::document::view& selector() const
    {
        return m_selector;
    }

    const bsoncxx::document::view update() const
    {
        return m_update;
    }

    std::string high_level_to_string(const char* zSeparator) const override
    {
        std::ostringstream ss;

        ss << "collection: " << m_zCollection << zSeparator
           << "upsert: " << (is_upsert() ? "true" : "false") << zSeparator
           << "multi: " << (is_multi() ? "true" : "false") << zSeparator
           << "selector: " << bsoncxx::to_json(m_selector) << zSeparator
           << "update: " << bsoncxx::to_json(m_update);

        return ss.str();
    }

private:
    const char*             m_zCollection;
    uint32_t                m_flags;
    bsoncxx::document::view m_selector;
    bsoncxx::document::view m_update;
};

class Query final : public Packet
{
public:
    Query(const Packet& packet);
    Query(Query&& rhs) = default;

    enum Flags
    {
        TAILABLE_CURSOR   = (1 << 1),
        SLAVE_OK          = (1 << 2),
        OPLOG_REPLAY      = (1 << 3),
        NO_CURSOR_TIMEOUT = (1 << 4),
        AWAIT_DATA        = (1 << 5),
        EXHAUST           = (1 << 6),
        PARTIAL           = (1 << 7)
    };

    uint32_t flags() const
    {
        return m_flags;
    }

    bool is_tailable_cursor() const
    {
        return m_flags & TAILABLE_CURSOR;
    }

    bool is_slave_ok() const
    {
        return m_flags & SLAVE_OK;
    }

    bool is_oplog_replay() const
    {
        return m_flags & OPLOG_REPLAY;
    }

    bool is_no_cursor_timeout() const
    {
        return m_flags & NO_CURSOR_TIMEOUT;
    }

    bool is_await_data() const
    {
        return m_flags & AWAIT_DATA;
    }

    bool is_exhaust() const
    {
        return m_flags & EXHAUST;
    }

    bool is_partial() const
    {
        return m_flags & PARTIAL;
    }

    const char* zCollection() const
    {
        return m_zCollection;
    }

    std::string collection() const
    {
        return m_zCollection;
    }

    uint32_t nSkip() const
    {
        return m_nSkip;
    }

    int32_t nReturn() const
    {
        return m_nReturn;
    }

    const bsoncxx::document::view& query() const
    {
        return m_query;
    }

    const bsoncxx::document::view& fields() const
    {
        return m_fields;
    }

    std::string high_level_to_string(const char* zSeparator) const override
    {
        std::ostringstream ss;

        ss << "collection: " << m_zCollection << zSeparator
           << "flags: " << m_flags << zSeparator // TODO: Perhaps should be decoded,
           << "nSkip: " << m_nSkip << zSeparator
           << "nReturn: " << m_nReturn << zSeparator
           << "query: " << bsoncxx::to_json(m_query) << zSeparator
           << "fields: " << bsoncxx::to_json(m_fields);

        return ss.str();
    }

protected:
    uint32_t                m_flags;
    const char*             m_zCollection;
    uint32_t                m_nSkip;
    uint32_t                m_nReturn;
    bsoncxx::document::view m_query;
    bsoncxx::document::view m_fields;
};

class Reply final : public Packet
{
public:
    Reply(const Packet& packet)
        : Packet(packet)
    {
        mxb_assert(opcode() == MONGOC_OPCODE_REPLY);

        const uint8_t* pData = reinterpret_cast<const uint8_t*>(m_pHeader) + sizeof(protocol::HEADER);

        pData += protocol::get_byte4(pData, &m_flags);
        pData += protocol::get_byte8(pData, &m_cursor_id);
        pData += protocol::get_byte4(pData, &m_start_from);
        pData += protocol::get_byte4(pData, &m_nReturned);

        while (pData < m_pEnd)
        {
            uint32_t size;
            protocol::get_byte4(pData, &size);
            m_documents.push_back(bsoncxx::document::view { pData, size });
            pData += size;
        }

        mxb_assert(m_nReturned == (int)m_documents.size());
        mxb_assert(pData == m_pEnd);
    }

    Reply(const Reply&) = default;
    Reply& operator = (const Reply&) = default;

    std::string high_level_to_string(const char* zSeparator) const override
    {
        std::ostringstream ss;

        ss << "flags: " << m_flags << zSeparator
           << "cursorId: " << m_cursor_id << zSeparator
           << "start_from: " << m_start_from << zSeparator
           << "nReturned: " << m_nReturned << zSeparator
           << "documents: ";

        auto it = m_documents.begin();

        while (it != m_documents.end())
        {
            ss << bsoncxx::to_json(*it);

            if (++it != m_documents.end())
            {
                ss << ", ";
            }
        }

        return ss.str();
    }

protected:
    int32_t                              m_flags;
    int64_t                              m_cursor_id;
    int32_t                              m_start_from;
    int32_t                              m_nReturned;
    std::vector<bsoncxx::document::view> m_documents;
};

class GetMore final : public Packet
{
public:
    GetMore(const Packet& packet);
    GetMore(const GetMore& that) = default;
    GetMore(GetMore&& that) = default;

    const char* zCollection() const
    {
        return m_zCollection;
    }

    std::string collection() const
    {
        return m_zCollection;
    }

    int32_t nReturn() const
    {
        return m_nReturn;
    }

    int64_t cursor_id() const
    {
        return m_cursor_id;
    }

    std::string high_level_to_string(const char* zSeparator) const override
    {
        std::ostringstream ss;

        ss << "collection: " << m_zCollection << zSeparator
           << "nReturn: " << m_nReturn << zSeparator
           << "cursor_id: " << m_cursor_id;

        return ss.str();
    }

private:
    const char* m_zCollection;
    int32_t     m_nReturn;
    int64_t     m_cursor_id;
};

class KillCursors final : public Packet
{
public:
    KillCursors(const Packet& packet);
    KillCursors(const KillCursors& that) = default;
    KillCursors(KillCursors&& that) = default;

    const std::vector<int64_t> cursor_ids() const
    {
        return m_cursor_ids;
    };

    std::string high_level_to_string(const char* zSeparator) const override
    {
        std::ostringstream ss;

        auto it = m_cursor_ids.begin();

        while (it != m_cursor_ids.end())
        {
            ss << *it;

            if (++it != m_cursor_ids.end())
            {
                ss << ", ";
            }
        }

        return ss.str();
    }

private:
    std::vector<int64_t> m_cursor_ids;
};

class Msg final : public Packet
{
public:
    enum
    {
        NONE             = 0,
        CHECKSUM_PRESENT = 1 << 0,
        MORE_TO_COME     = 1 << 1,
        EXHAUST_ALLOWED  = 1 << 16
    };

    using DocumentVector = std::vector<bsoncxx::document::view>;
    using DocumentArguments = std::unordered_map<std::string, DocumentVector>;

    Msg(const Packet& packet);
    Msg(const Msg& rhs) = default;
    Msg(Msg&& rhs) = default;

    bool checksum_present() const
    {
        return (m_flags & CHECKSUM_PRESENT) ? true : false;
    }

    bool exhaust_allowed() const
    {
        return (m_flags & EXHAUST_ALLOWED) ? true : false;
    }

    bool more_to_come() const
    {
        return (m_flags & MORE_TO_COME) ? true : false;
    }

    const bsoncxx::document::view& document() const
    {
        return m_document;
    }

    const DocumentArguments& arguments() const
    {
        return m_arguments;
    }

    std::string high_level_to_string(const char* zSeparator) const override
    {
        std::ostringstream ss;

        ss << "flags: " << m_flags << zSeparator
           << "document: " << bsoncxx::to_json(m_document) << zSeparator
           << "arguments: ";

        auto it = m_arguments.begin();

        while (it != m_arguments.end())
        {
            ss << "(" << it->first << ": ";

            auto jt = it->second.begin();

            while (jt != it->second.end())
            {
                ss << bsoncxx::to_json(*jt);

                if (++jt != it->second.end())
                {
                    ss << ", ";
                }
            }

            ss << ")";

            if (++it != m_arguments.end())
            {
                ss << ", ";
            }
        }

        return ss.str();
    }

private:
    uint32_t                m_flags { 0 };
    bsoncxx::document::view m_document;
    DocumentArguments       m_arguments;
};

}

class Database;
class UserManager;

class NoSQL
{
public:
    class Sasl
    {
    public:
        const UserManager::UserInfo& user_info() const
        {
            return m_user_info;
        }

        int32_t conversation_id() const
        {
            return m_conversation_id;
        }

        int32_t bump_conversation_id()
        {
            return ++m_conversation_id;
        }

        const std::string& client_nonce_b64() const
        {
            return m_client_nonce_b64;
        }

        const std::string& gs2_header() const
        {
            return m_gs2_header;
        }

        const std::string& server_nonce_b64() const
        {
            return m_server_nonce_b64;
        }

        std::string nonce_b64() const
        {
            return m_client_nonce_b64 + m_server_nonce_b64;
        }

        const std::string& initial_message() const
        {
            return m_initial_message;
        }

        const std::string& server_first_message() const
        {
            return m_server_first_message;
        }

        scram::Scram* scram() const
        {
            mxb_assert(m_sScram.get());
            return m_sScram.get();
        }

        void set_client_nonce_b64(const std::string s)
        {
            m_client_nonce_b64 = std::move(s);
        }

        void set_client_nonce_b64(const string_view& s)
        {
            set_client_nonce_b64(to_string(s));
        }

        void set_gs2_header(const std::string s)
        {
            m_gs2_header = std::move(s);
        }

        void set_gs2_header(const string_view& s)
        {
            set_gs2_header(to_string(s));
        }

        void set_server_nonce_b64(std::string s)
        {
            m_server_nonce_b64 = std::move(s);
        }

        void set_server_nonce_b64(const std::vector<uint8_t>& v)
        {
            set_server_nonce_b64(std::string(reinterpret_cast<const char*>(v.data()), v.size()));
        }

        void set_initial_message(std::string s)
        {
            m_initial_message = std::move(s);
        }

        void set_initial_message(const string_view& s)
        {
            set_initial_message(to_string(s));
        }

        void set_server_first_message(std::string s)
        {
            m_server_first_message = std::move(s);
        }

        void set_user_info(UserManager::UserInfo&& user_info)
        {
            m_user_info = std::move(user_info);
        }

        void set_scram(std::unique_ptr<scram::Scram> sScram)
        {
            m_sScram = std::move(sScram);
        }

    private:
        UserManager::UserInfo         m_user_info;
        std::string                   m_client_nonce_b64;
        std::string                   m_gs2_header;
        std::string                   m_server_nonce_b64;
        int32_t                       m_conversation_id { 0 };
        std::string                   m_initial_message;
        std::string                   m_server_first_message;
        std::unique_ptr<scram::Scram> m_sScram;
    };

    class Context
    {
    public:
        Context(const Context&) = delete;
        Context& operator = (const Context&) = delete;

        Context(UserManager* pUm,
                MXS_SESSION* pSession,
                ClientConnection* pClient_connection,
                mxs::Component* pDownstream);

        UserManager& um() const
        {
            return m_um;
        }

        ClientConnection& client_connection()
        {
            return m_client_connection;
        }

        MXS_SESSION& session()
        {
            return m_session;
        }

        mxs::Component& downstream()
        {
            return m_downstream;
        }

        int64_t connection_id() const
        {
            return m_connection_id;
        }

        int32_t current_request_id() const
        {
            return m_request_id;
        }

        int32_t next_request_id()
        {
            return ++m_request_id;
        }

        void set_last_error(std::unique_ptr<LastError>&& sLast_error)
        {
            m_sLast_error = std::move(sLast_error);
        }

        void get_last_error(DocumentBuilder& doc);
        void reset_error(int32_t n = 0);

        mxs::RoutingWorker& worker() const
        {
            mxb_assert(m_session.worker());
            return *m_session.worker();
        }

        void set_metadata_sent(bool metadata_sent)
        {
            m_metadata_sent = metadata_sent;
        }

        bool metadata_sent() const
        {
            return m_metadata_sent;
        }

        Sasl& sasl()
        {
            return m_sasl;
        }

        void set_roles(std::unordered_map<std::string, uint32_t>&& roles)
        {
            m_roles = roles;
        }

        uint32_t role_mask_of(const std::string& name) const
        {
            auto it = m_roles.find(name);

            return it == m_roles.end() ? 0 : it->second;
        }

        bool authenticated() const
        {
            return m_authenticated;
        }

        void set_authenticated(bool authenticated)
        {
            m_authenticated = authenticated;
        }

    private:
        using Roles = std::unordered_map<std::string, uint32_t>;

        UserManager&               m_um;
        MXS_SESSION&               m_session;
        ClientConnection&          m_client_connection;
        mxs::Component&            m_downstream;
        int32_t                    m_request_id { 1 };
        int64_t                    m_connection_id;
        std::unique_ptr<LastError> m_sLast_error;
        bool                       m_metadata_sent { false };
        Sasl                       m_sasl;
        Roles                      m_roles;
        bool                       m_authenticated { false };

        static std::atomic<int64_t> s_connection_id;
    };

    NoSQL(MXS_SESSION*      pSession,
          ClientConnection* pClient_connection,
          mxs::Component*   pDownstream,
          Config*           pConfig,
          UserManager*      pUm);
    ~NoSQL();

    NoSQL(const NoSQL&) = delete;
    NoSQL& operator = (const NoSQL&) = delete;

    State state() const
    {
        return m_sDatabase ?  State::BUSY : State::READY;
    }

    bool is_busy() const
    {
        return state() == State::BUSY;
    }

    Context& context()
    {
        return m_context;
    }

    const Config& config() const
    {
        return m_config;
    }

    State handle_request(GWBUF* pRequest, GWBUF** ppResponse);

    GWBUF* handle_request(GWBUF* pRequest)
    {
        GWBUF* pResponse = nullptr;
        handle_request(pRequest, &pResponse);

        return pResponse;
    }

    int32_t clientReply(GWBUF* sMariaDB_response, DCB* pDcb);

private:
    template<class T>
    void log_in(const char* zContext, const T& req)
    {
        if (m_config.should_log_in())
        {
            MXS_NOTICE("%s: %s", zContext, req.to_string().c_str());
        }
    }

    void kill_client();

    using SDatabase = std::unique_ptr<Database>;

    State handle_delete(GWBUF* pRequest, packet::Delete&& req, GWBUF** ppResponse);
    State handle_insert(GWBUF* pRequest, packet::Insert&& req, GWBUF** ppResponse);
    State handle_update(GWBUF* pRequest, packet::Update&& req, GWBUF** ppResponse);
    State handle_query(GWBUF* pRequest, packet::Query&& req, GWBUF** ppResponse);
    State handle_get_more(GWBUF* pRequest, packet::GetMore&& req, GWBUF** ppResponse);
    State handle_kill_cursors(GWBUF* pRequest, packet::KillCursors&& req, GWBUF** ppResponse);
    State handle_msg(GWBUF* pRequest, packet::Msg&& req, GWBUF** ppResponse);

    State              m_state { State::READY };
    Context            m_context;
    Config&            m_config;
    std::deque<GWBUF*> m_requests;
    SDatabase          m_sDatabase;
};

/**
 * A Path represents all incarnations of a particular JSON path.
 */
class Path
{
public:
    /**
     * An Incarnation represents a single JSON path.
     */
    class Incarnation
    {
    public:
        Incarnation(std::string&& path,
                    std::string&& parent_path,
                    std::string&& array_path)
            : m_path(std::move(path))
            , m_parent_path(std::move(parent_path))
            , m_array_path(std::move(array_path))
        {
        }

        /**
         * @return A complete JSON path.
         */
        const std::string& path() const
        {
            return m_path;
        }

        /**
         * @return The JSON path of the parent element or an empty string if there is no parent.
         *
         * @note The path does *not* contain any suffixes like "[*]" and is intended to be used
         *       e.g. for ensuring that the parent is an OBJECT.
         */
        const std::string& parent_path() const
        {
            return m_parent_path;
        }

        /**
         * @return The JSON path of the nearest ancestor element that is expected to be an array,
         *         or an empty string if no such ancestor exists.
         *
         * @note The path does *not* contain any suffixes like "[*]" and is intended to be used
         *       e.g. for ensuring that the ancestor is an ARRAY.
         */
        const std::string& array_path() const
        {
            return m_array_path;
        }

        bool has_parent() const
        {
            return !m_parent_path.empty();
        }

        bool has_array_demand() const
        {
            return !m_array_path.empty();
        }

        std::string get_comparison_condition(const bsoncxx::document::element& element) const;
        std::string get_comparison_condition(const bsoncxx::document::view& doc) const;

        enum class ArrayOp
        {
            AND,
            OR
        };

    private:
        std::string array_op_to_condition(const bsoncxx::document::element& element, ArrayOp op) const;
        std::string elemMatch_to_condition(const bsoncxx::document::element& element) const;
        std::string exists_to_condition(const bsoncxx::document::element& element) const;
        std::string mod_to_condition(const bsoncxx::document::element& element) const;
        std::string nin_to_condition(const bsoncxx::document::element& element) const;
        std::string not_to_condition(const bsoncxx::document::element& element) const;
        std::string type_to_condition(const bsoncxx::document::element& element) const;

    private:
        std::string m_path;
        std::string m_parent_path;
        std::string m_array_path;
    };

    class Part
    {
    public:
        enum Kind
        {
            ELEMENT,
            ARRAY,
            INDEXED_ELEMENT
        };

        Part(Kind kind, const std::string& name, Part* pParent = 0)
            : m_kind(kind)
            , m_name(name)
            , m_pParent(pParent)
        {
            if (m_pParent)
            {
                m_pParent->add_child(this);
            }
        }

        Kind kind() const
        {
            return m_kind;
        }

        bool is_element() const
        {
            return m_kind == ELEMENT;
        }

        bool is_array() const
        {
            return m_kind == ARRAY;
        }

        bool is_indexed_element() const
        {
            return m_kind == INDEXED_ELEMENT;
        }

        Part* parent() const
        {
            return m_pParent;
        }

        std::string name() const;

        std::string path() const;

        static std::vector<Part*> get_leafs(const std::string& path,
                                            std::vector<std::unique_ptr<Part>>& parts);

    private:
        void add_child(Part* pChild)
        {
            m_children.push_back(pChild);
        }

        static void add_leaf(const std::string& part,
                             bool last,
                             bool is_number,
                             Part* pParent,
                             std::vector<Part*>& leafs,
                             std::vector<std::unique_ptr<Part>>& parts);

        static void add_part(const std::string& part,
                             bool last,
                             std::vector<Part*>& leafs,
                             std::vector<std::unique_ptr<Part>>& parts);

        Kind               m_kind;
        std::string        m_name;
        Part*              m_pParent { nullptr };
        std::vector<Part*> m_children;
    };

    Path(const bsoncxx::document::element& element);

    std::string get_comparison_condition() const;

    static std::vector<Incarnation> get_incarnations(const std::string& key);

private:
    std::string get_element_condition(const bsoncxx::document::element& element) const;
    std::string get_document_condition(const bsoncxx::document::view& doc) const;

    std::string not_to_condition(const bsoncxx::document::element& element) const;

    static void add_part(std::vector<Incarnation>& rv, const std::string& part);

    bsoncxx::document::element m_element;
    std::vector<Incarnation>   m_paths;
};

/**
 * Get SQL statement for creating a document table.
 *
 * @param table_name     The name of the table. Will be used verbatim,
 *                       so all necessary quotes should be provided.
 * @param id_length      The VARCHAR length of the id column.
 * @param if_not_exists  If true, the statement will contain "IF NOT EXISTS".
 *
 * @return An SQL statement for creating the table.
 */
std::string table_create_statement(const std::string& table_name,
                                   int64_t id_length,
                                   bool if_not_exists = true);


/**
 * Escape the characters \ and '.
 *
 * @param from  The string to escape.
 *
 * @return The same string with \ and ' escaped.
 */
std::string escape_essential_chars(std::string&& from);

/**
 * Converts a JSON array into the equivalent BSON array.
 *
 * @param pArray  The JSON array.
 *
 * @return The corresponding BSON array.
 */
bsoncxx::array::value bson_from_json_array(json_t* pArray);

/**
 * Converts a JSON object into the equivalent BSON object.
 *
 * @param pObject  The JSON object.
 *
 * @return The corresponding BSON object.
 */
bsoncxx::document::value bson_from_json(json_t* pObject);

/**
 * Converts a JSON string into the equivalent BSON object.
 *
 * @param json  Valid JSON.
 *
 * @return The corresponding BSON object.
 */
bsoncxx::document::value bson_from_json(const std::string& json);

/**
 * Given a resultset row, converts it into the corresponding JSON.
 *
 * @param row          A result set row.
 * @param extractions  The extractions to perform, *MUST* match what the row contains.
 *
 * @return The row as JSON.
 */
std::string resultset_row_to_json(const CQRTextResultsetRow& row,
                                  const std::vector<std::string>& extractions);

std::string resultset_row_to_json(const CQRTextResultsetRow& row,
                                  CQRTextResultsetRow::iterator begin,
                                  const std::vector<std::string>& extractions);

}
