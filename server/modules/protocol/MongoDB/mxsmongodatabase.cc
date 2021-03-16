/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2024-08-24
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "mxsmongocommands.hh"
#include "mxsmongodatabase.hh"
#include <maxscale/mysql_utils.hh>
#include <maxscale/protocol/mariadb/mysql.hh>
#include "config.hh"

using namespace std;
using mxb::Worker;

mxsmongo::Database::Database(const std::string& name,
                             Mongo::Context* pContext,
                             const Config* pConfig)
    : m_name(name)
    , m_context(*pContext)
    , m_config(*pConfig)
{
}

mxsmongo::Database::~Database()
{
    mxb_assert(m_state == READY);
}

//static
unique_ptr<mxsmongo::Database> mxsmongo::Database::create(const std::string& name,
                                                          Mongo::Context* pContext,
                                                          const Config* pConfig)
{
    return unique_ptr<Database>(new Database(name, pContext, pConfig));
}

GWBUF* mxsmongo::Database::handle_query(GWBUF* pRequest, const mxsmongo::Query& req)
{
    mxb_assert(is_ready());

    return execute(pRequest, req, req.query());
}

GWBUF* mxsmongo::Database::handle_command(GWBUF* pRequest,
                                          const mxsmongo::Msg& req,
                                          const bsoncxx::document::view& doc)
{
    mxb_assert(is_ready());

    return execute(pRequest, req, doc);
}

GWBUF* mxsmongo::Database::translate(GWBUF& mariadb_response)
{
    mxb_assert(is_pending());
    mxb_assert(m_sCommand.get());

    GWBUF* pResponse;
    Command::State state = m_sCommand->translate(mariadb_response, &pResponse);

    if (state == Command::READY)
    {
        mxb_assert(state == Command::READY);

        m_sCommand.reset();

        set_ready();
    }

    return pResponse;
}

GWBUF* mxsmongo::Database::execute(GWBUF* pRequest,
                                   const mxsmongo::Packet& req,
                                   const bsoncxx::document::view& doc)
{
    GWBUF* pResponse = nullptr;

    auto sCommand = mxsmongo::Command::get(this, pRequest, req, doc);

    try
    {
        pResponse = sCommand->execute();
    }
    catch (const std::exception& x)
    {
        MXS_ERROR("Exeception occurred when parsing MongoDB command: %s", x.what());
        mxb_assert(!true);

        pResponse = sCommand->create_error_response(x.what(), mxsmongo::error::FAILED_TO_PARSE);
    }

    if (!pResponse)
    {
        m_sCommand = std::move(sCommand);
        set_pending();
    }

    return pResponse;
}
