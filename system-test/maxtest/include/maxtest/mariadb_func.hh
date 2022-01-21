#pragma once

/**
 * @file mariadb_func.h - basic DB interaction routines
 *
 * @verbatim
 * Revision History
 *
 * Date     Who     Description
 * 17/11/14 Timofey Turenko Initial implementation
 *
 * @endverbatim
 */

#include <mysql.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <math.h>
#include <time.h>
#include <stdarg.h>
#include <errno.h>
#include <string>
#include <vector>

#include <maxbase/ccdefs.hh>
#include <maxbase/string.hh>

typedef std::vector<std::string> Row;
typedef std::vector<Row>         Result;

/**
 * Opens connection to DB: wropper over mysql_real_connect
 *
 * @param port  DB server port
 * @param ip    DB server IP address
 * @param db    name of DB to connect
 * @param user  user name
 * @param password  password
 * @param flag  Connections flags
 * @param ssl   true if ssl should be used
 *
 * @return MYSQL struct
 */
MYSQL* open_conn_db_flags(int port,
                          std::string ip,
                          std::string db,
                          std::string user,
                          std::string password,
                          unsigned long flag,
                          bool ssl);


/**
 * Opens connection to DB: wropper over mysql_real_connect
 *
 * @param port  DB server port
 * @param ip    DB server IP address
 * @param db    name of DB to connect
 * @param user  user name
 * @param password  password
 * @param timeout  timeout on seconds
 * @param ssl   true if ssl should be used
 *
 * @return MYSQL struct
 */
MYSQL* open_conn_db_timeout(int port,
                            std::string ip,
                            std::string db,
                            std::string user,
                            std::string password,
                            unsigned int timeout,
                            bool ssl);

/**
 * Opens connection to DB with default flags
 *
 * @param port  DB server port
 * @param ip    DB server IP address
 * @param db    name of DB to connect
 * @param user  user name
 * @param password  password
 * @param ssl   true if ssl should be used
 *
 * @return MYSQL struct
 */
static MYSQL* open_conn_db(int port,
                           std::string ip,
                           std::string db,
                           std::string user,
                           std::string password,
                           bool ssl = false)
{
    return open_conn_db_flags(port, ip, db, user, password, CLIENT_MULTI_STATEMENTS, ssl);
}

/**
 * Opens connection to 'test' with default flags
 *
 * @param port  DB server port
 * @param ip    DB server IP address
 * @param user  user name
 * @param password  password
 * @param ssl   true if ssl should be used
 *
 * @return MYSQL struct
 */
static MYSQL* open_conn(int port, std::string ip, std::string user, std::string password, bool ssl = false)
{
    return open_conn_db(port, ip.c_str(), "test", user.c_str(), password.c_str(), ssl);
}

/**
 * Opens connection to with default flags without defning DB name (just conecto server)
 *
 * @param port  DB server port
 * @param ip    DB server IP address
 * @param user  user name
 * @param password  password
 * @param ssl   true if ssl should be used
 *
 * @return MYSQL struct
 */
static MYSQL* open_conn_no_db(int port,
                              std::string ip,
                              std::string user,
                              std::string password,
                              bool ssl = false)
{
    return open_conn_db_flags(port, ip, "", user, password, CLIENT_MULTI_STATEMENTS, ssl);
}

/**
 * @brief Executes SQL query. Function also executes mysql_store_result() and mysql_free_result() to clean up
 * returns
 * @param conn      MYSQL connection
 * @param format    SQL string with printf style formatting
 * @param ...       Parameters for @c format
 * @return 0 in case of success
 */
int execute_query(MYSQL* conn, const char* format, ...) mxb_attribute((format(printf, 2, 3)));

/**
 * @brief execute_query_from_file Read a line from a file, trim leading and trailing whitespace and execute
 * it.
 * @param conn MYSQL handler
 * @param file file handler
 * @return 0 in case of success
 */
int execute_query_from_file(MYSQL* conn, FILE* file);

/**
 * @brief Executes SQL query. Function also executes mysql_store_result() and mysql_free_result() to clean up
 * returns
 * @param conn MYSQL connection struct
 * @param sql   SQL string
 * @return 0 in case of success
 */
int execute_query_silent(MYSQL* conn, const char* sql, bool silent = true);

/**
 * @brief Executes SQL query and store 'affected rows' number in affectet_rows parameter
 * @param conn MYSQL    connection struct
 * @param sql   SQL string
 * @param affected_rows pointer to variabe to store number of affected rows
 * @return 0 in case of success
 */
int execute_query_affected_rows(MYSQL* conn, const char* sql, my_ulonglong* affected_rows);

/**
 * @brief A more convenient form of execute_query_affected_rows()
 *
 * @param conn Connection to use for the query
 * @param sql  The SQL statement to execute
 * @return Number of rows or -1 on error
 */
int execute_query_count_rows(MYSQL* conn, const char* sql);

/**
 * @brief Executes SQL query and get number of rows in the result
 * This function does not check boudaries of 'num_of_rows' array. This
 * array have to be big enough to store all results
 * @param conn MYSQL    connection struct
 * @param sql   SQL string
 * @param num_of_rows pointer to array to store number of result rows
 * @param i pointer to variable to store number of result sets
 * @return 0 in case of success
 */
int execute_query_num_of_rows(MYSQL* conn,
                              const char* sql,
                              my_ulonglong* num_of_rows,
                              unsigned long long* i);

/**
 * @brief Executes perared statement and get number of rows in the result
 * This function does not check boudaries of 'num_of_rows' array. This
 * array have to be big enough to store all results
 * @param stmt MYSQL_STMT statetement struct (from mysql_stmt_init())
 * @param num_of_rows pointer to array to store number of result rows
 * @param i pointer to variable to store number of result sets
 * @return 0 in case of success
 */
int execute_stmt_num_of_rows(MYSQL_STMT* stmt, my_ulonglong* num_of_rows, unsigned long long* i);

/**
 * @brief execute_query_check_one Executes query and check if first field of first row is equal to 'expected'
 * @param conn MYSQL handler
 * @param sql query SQL query to execute
 * @param expected Expected result
 * @return 0 in case of success
 */
int execute_query_check_one(MYSQL* conn, const char* sql, const char* expected);

/**
 * @brief Executes 'show processlist' and calculates number of connections from defined host to defined DB
 * @param conn MYSQL    connection struct
 * @param ip    connections from this IP address are counted
 * @param db    name of DB to which connections are counted
 * @return number of connections
 */
int get_conn_num(MYSQL* conn, std::string ip, std::string hostname, std::string db);

/**
 * @brief Find given filed in the SQL query reply
 * Function checks only firs row from the table
 * @param conn MYSQL    connection struct
 * @param sql   SQL query to execute
 * @param filed_name    name of field to find
 * @param value pointer to variable to store value of found field
 * @return 0 in case of success
 */
int find_field(MYSQL* conn, const char* sql, const char* field_name, char* value);

/**
 * Execute a query and return the first row
 *
 * @param conn The connection to use
 * @param sql  The query to execute
 *
 * @return The first row as a list of strings
 */
Row get_row(MYSQL* conn, std::string sql);

/**
 * Execute a query and return the result
 *
 * @param conn The connection to use
 * @param sql  The query to execute
 *
 * @return The result as a list of rows
 */
Result get_result(MYSQL* conn, std::string sql);

int get_int_version(std::string version);

// Helper class for performing queries
class Connection
{
public:
    Connection(Connection&) = delete;
    Connection& operator=(Connection&) = delete;

    Connection(std::string host,
               int port,
               std::string user,
               std::string password,
               std::string db = "",
               bool ssl = false)
        : m_host(host)
        , m_port(port)
        , m_user(user)
        , m_pw(password)
        , m_db(db)
        , m_ssl(ssl)
    {
    }

    Connection(Connection&& rhs)
        : m_host(std::move(rhs.m_host))
        , m_port(std::move(rhs.m_port))
        , m_user(std::move(rhs.m_user))
        , m_pw(std::move(rhs.m_pw))
        , m_db(std::move(rhs.m_db))
        , m_ssl(std::move(rhs.m_ssl))
        , m_conn(std::move(rhs.m_conn))
    {
        rhs.m_conn = nullptr;
    }

    Connection& operator=(Connection&& rhs)
    {
        disconnect();

        m_host = std::move(rhs.m_host);
        m_port = std::move(rhs.m_port);
        m_user = std::move(rhs.m_user);
        m_pw = std::move(rhs.m_pw);
        m_db = std::move(rhs.m_db);
        m_ssl = std::move(rhs.m_ssl);
        m_conn = std::move(rhs.m_conn);
        rhs.m_conn = nullptr;
        return *this;
    }

    virtual ~Connection()
    {
        mysql_close(m_conn);
    }

    void ssl(bool value)
    {
        m_ssl = value;
    }

    bool connect();

    void disconnect()
    {
        mysql_close(m_conn);
        m_conn = nullptr;
    }

    bool query(std::string q)
    {
        return execute_query_silent(m_conn, q.c_str()) == 0;
    }

    bool check(std::string q, std::string res)
    {
        Row row = get_row(m_conn, q);
        return !row.empty() && row[0] == res;
    }

    Row row(std::string q)
    {
        return get_row(m_conn, q);
    }

    Result rows(const std::string& q) const
    {
        return get_result(m_conn, q);
    }

    std::string pretty_rows(const std::string& q) const
    {
        std::string rval;

        for (const auto& a : rows(q))
        {
            rval += mxb::join(a) + '\n';
        }

        return rval;
    }

    std::string field(std::string q, int idx = 0)
    {
        Row r = get_row(m_conn, q);
        return r.empty() ? std::string() : r[idx];
    }

    const char* error() const
    {
        return mysql_error(m_conn);
    }

    unsigned int errnum() const
    {
        return mysql_errno(m_conn);
    }

    bool change_user(std::string user, std::string pw, std::string db = "test")
    {
        return mysql_change_user(m_conn, user.c_str(), pw.c_str(), db.c_str()) == 0;
    }

    bool reset_connection()
    {
        return change_user(m_user, m_pw, m_db);
    }

    void set_credentials(const std::string& user, const std::string pw)
    {
        m_user = user;
        m_pw = pw;
    }

    void set_database(const std::string& db)
    {
        m_db = db;
    }

    void set_charset(const std::string& charset)
    {
        m_charset = charset;
    }

    void set_timeout(int timeout)
    {
        m_timeout = timeout;
    }

    uint32_t thread_id() const
    {
        return mysql_thread_id(m_conn);
    }

    std::string host() const
    {
        return m_host;
    }

    int port() const
    {
        return m_port;
    }

    MYSQL_STMT* stmt()
    {
        return mysql_stmt_init(m_conn);
    }

private:
    std::string m_host;
    int         m_port;
    std::string m_user;
    std::string m_pw;
    std::string m_db;
    std::string m_charset;
    bool        m_ssl;
    int         m_timeout = 0;
    MYSQL*      m_conn = nullptr;
};
