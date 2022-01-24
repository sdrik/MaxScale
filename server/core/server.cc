/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-01-04
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "internal/server.hh"

#include <stdio.h>
#include <string.h>

#include <mutex>
#include <string>
#include <vector>

#include <maxbase/alloc.h>
#include <maxbase/atomic.hh>
#include <maxbase/stopwatch.hh>
#include <maxbase/log.hh>

#include <maxscale/config2.hh>
#include <maxscale/session.hh>
#include <maxscale/dcb.hh>
#include <maxscale/ssl.hh>
#include <maxscale/json_api.hh>
#include <maxscale/http.hh>
#include <maxscale/routingworker.hh>
#include <maxscale/modutil.hh>

#include "internal/config.hh"
#include "internal/session.hh"

using maxbase::Worker;
using maxscale::RoutingWorker;

using std::string;
using Guard = std::lock_guard<std::mutex>;
using namespace std::literals::chrono_literals;
using namespace std::literals::string_literals;

namespace cfg = mxs::config;
using Operation = mxb::MeasureTime::Operation;

namespace
{

constexpr const char CN_EXTRA_PORT[] = "extra_port";
constexpr const char CN_MONITORPW[] = "monitorpw";
constexpr const char CN_MONITORUSER[] = "monitoruser";
constexpr const char CN_PERSISTMAXTIME[] = "persistmaxtime";
constexpr const char CN_PERSISTPOOLMAX[] = "persistpoolmax";
constexpr const char CN_PRIORITY[] = "priority";
constexpr const char CN_PROXY_PROTOCOL[] = "proxy_protocol";

const char ERR_TOO_LONG_CONFIG_VALUE[] = "The new value for %s is too long. Maximum length is %i characters.";

/**
 * Write to char array by first zeroing any extra space. This reduces effects of concurrent reading.
 * Concurrent writing should be prevented by the caller.
 *
 * @param dest Destination buffer. The buffer is assumed to contains at least a \0 at the end.
 * @param max_len Size of destination buffer - 1. The last element (max_len) is never written to.
 * @param source Source string. A maximum of @c max_len characters are copied.
 */
void careful_strcpy(char* dest, size_t max_len, const std::string& source)
{
    // The string may be accessed while we are updating it.
    // Take some precautions to ensure that the string cannot be completely garbled at any point.
    // Strictly speaking, this is not fool-proof as writes may not appear in order to the reader.
    size_t new_len = source.length();
    if (new_len > max_len)
    {
        new_len = max_len;
    }

    size_t old_len = strlen(dest);
    if (new_len < old_len)
    {
        // If the new string is shorter, zero out the excess data.
        memset(dest + new_len, 0, old_len - new_len);
    }

    // No null-byte needs to be set. The array starts out as all zeros and the above memset adds
    // the necessary null, should the new string be shorter than the old.
    strncpy(dest, source.c_str(), new_len);
}

class ServerSpec : public cfg::Specification
{
public:
    using cfg::Specification::Specification;

protected:

    template<class Params>
    bool do_post_validate(Params params) const;

    bool post_validate(const mxs::ConfigParameters& params) const override
    {
        return do_post_validate(params);
    }

    bool post_validate(json_t* json) const override
    {
        return do_post_validate(json);
    }
};

static const auto NO_QUOTES = cfg::ParamString::IGNORED;
static const auto AT_RUNTIME = cfg::Param::AT_RUNTIME;

static ServerSpec s_spec(CN_SERVERS, cfg::Specification::SERVER);

static cfg::ParamString s_type(&s_spec, CN_TYPE, "Object type", "server", NO_QUOTES);
static cfg::ParamString s_protocol(&s_spec, CN_PROTOCOL, "Server protocol (deprecated)", "", NO_QUOTES);
static cfg::ParamString s_authenticator(
    &s_spec, CN_AUTHENTICATOR, "Server authenticator (deprecated)", "", NO_QUOTES);

static cfg::ParamString s_address(&s_spec, CN_ADDRESS, "Server address", "", NO_QUOTES, AT_RUNTIME);
static cfg::ParamString s_socket(&s_spec, CN_SOCKET, "Server UNIX socket", "", NO_QUOTES, AT_RUNTIME);
static cfg::ParamCount s_port(&s_spec, CN_PORT, "Server port", 3306, AT_RUNTIME);
static cfg::ParamCount s_extra_port(&s_spec, CN_EXTRA_PORT, "Server extra port", 0, AT_RUNTIME);
static cfg::ParamCount s_priority(&s_spec, CN_PRIORITY, "Server priority", 0, AT_RUNTIME);
static cfg::ParamString s_monitoruser(&s_spec, CN_MONITORUSER, "Monitor user", "", NO_QUOTES, AT_RUNTIME);
static cfg::ParamString s_monitorpw(&s_spec, CN_MONITORPW, "Monitor password", "", NO_QUOTES, AT_RUNTIME);

static cfg::ParamCount s_persistpoolmax(
    &s_spec, CN_PERSISTPOOLMAX, "Maximum size of the persistent connection pool", 0, AT_RUNTIME);

static cfg::ParamSeconds s_persistmaxtime(
    &s_spec, CN_PERSISTMAXTIME, "Maximum time that a connection can be in the pool",
    cfg::INTERPRET_AS_SECONDS, 0s, AT_RUNTIME);

static cfg::ParamBool s_proxy_protocol(
    &s_spec, CN_PROXY_PROTOCOL, "Enable proxy protocol", false, AT_RUNTIME);

static Server::ParamDiskSpaceLimits s_disk_space_threshold(
    &s_spec, CN_DISK_SPACE_THRESHOLD, "Server disk space threshold");

static cfg::ParamEnum<int64_t> s_rank(
    &s_spec, CN_RANK, "Server rank",
    {
        {RANK_PRIMARY, "primary"},
        {RANK_SECONDARY, "secondary"}
    }, RANK_PRIMARY, AT_RUNTIME);

static cfg::ParamCount s_max_connections(
    &s_spec, "max_connections", "Maximum connections", 0, AT_RUNTIME);

//
// TLS parameters
//

static cfg::ParamBool s_ssl(&s_spec, CN_SSL, "Enable TLS for server", false, AT_RUNTIME);

static cfg::ParamPath s_ssl_cert(
    &s_spec, CN_SSL_CERT, "TLS public certificate", cfg::ParamPath::R, "", AT_RUNTIME);
static cfg::ParamPath s_ssl_key(
    &s_spec, CN_SSL_KEY, "TLS private key", cfg::ParamPath::R, "", AT_RUNTIME);
static cfg::ParamPath s_ssl_ca(
    &s_spec, CN_SSL_CA_CERT, "TLS certificate authority", cfg::ParamPath::R, "", AT_RUNTIME);

static cfg::ParamEnum<mxb::ssl_version::Version> s_ssl_version(
    &s_spec, CN_SSL_VERSION, "Minimum TLS protocol version",
    {
        {mxb::ssl_version::SSL_TLS_MAX, "MAX"},
        {mxb::ssl_version::TLS10, "TLSv10"},
        {mxb::ssl_version::TLS11, "TLSv11"},
        {mxb::ssl_version::TLS12, "TLSv12"},
        {mxb::ssl_version::TLS13, "TLSv13"}
    }, mxb::ssl_version::SSL_TLS_MAX, AT_RUNTIME);

static cfg::ParamString s_ssl_cipher(&s_spec, CN_SSL_CIPHER, "TLS cipher list", "", NO_QUOTES, AT_RUNTIME);

static cfg::ParamCount s_ssl_cert_verify_depth(
    &s_spec, CN_SSL_CERT_VERIFY_DEPTH, "TLS certificate verification depth", 9, AT_RUNTIME);

static cfg::ParamBool s_ssl_verify_peer_certificate(
    &s_spec, CN_SSL_VERIFY_PEER_CERTIFICATE, "Verify TLS peer certificate", false, AT_RUNTIME);

static cfg::ParamBool s_ssl_verify_peer_host(
    &s_spec, CN_SSL_VERIFY_PEER_HOST, "Verify TLS peer host", false, AT_RUNTIME);

template<class Params>
bool ServerSpec::do_post_validate(Params params) const
{
    bool rval = true;
    auto monuser = s_monitoruser.get(params);
    auto monpw = s_monitorpw.get(params);

    if (monuser.empty() != monpw.empty())
    {
        MXS_ERROR("If '%s is defined, '%s' must also be defined.",
                  !monuser.empty() ? CN_MONITORUSER : CN_MONITORPW,
                  !monuser.empty() ? CN_MONITORPW : CN_MONITORUSER);
        rval = false;
    }

    if (monuser.length() > Server::MAX_MONUSER_LEN)
    {
        MXS_ERROR(ERR_TOO_LONG_CONFIG_VALUE, CN_MONITORUSER, Server::MAX_MONUSER_LEN);
        rval = false;
    }

    if (monpw.length() > Server::MAX_MONPW_LEN)
    {
        MXS_ERROR(ERR_TOO_LONG_CONFIG_VALUE, CN_MONITORPW, Server::MAX_MONPW_LEN);
        rval = false;
    }

    auto address = s_address.get(params);
    auto socket = s_socket.get(params);
    bool have_address = !address.empty();
    bool have_socket = !socket.empty();
    auto addr = have_address ? address : socket;

    if (have_socket && have_address)
    {
        MXS_ERROR("Both '%s=%s' and '%s=%s' defined: only one of the parameters can be defined",
                  CN_ADDRESS, address.c_str(), CN_SOCKET, socket.c_str());
        rval = false;
    }
    else if (!have_address && !have_socket)
    {
        MXS_ERROR("Missing a required parameter: either '%s' or '%s' must be defined",
                  CN_ADDRESS, CN_SOCKET);
        rval = false;
    }
    else if (have_address && addr[0] == '/')
    {
        MXS_ERROR("The '%s' parameter is not a valid IP or hostname", CN_ADDRESS);
        rval = false;
    }
    else if (addr.length() > Server::MAX_ADDRESS_LEN)
    {
        MXS_ERROR(ERR_TOO_LONG_CONFIG_VALUE, have_address ? CN_ADDRESS : CN_SOCKET, Server::MAX_ADDRESS_LEN);
        rval = false;
    }

    if (s_ssl.get(params) && s_ssl_cert.get(params).empty() != s_ssl_key.get(params).empty())
    {
        MXS_ERROR("Both '%s' and '%s' must be defined", s_ssl_cert.name().c_str(), s_ssl_key.name().c_str());
        rval = false;
    }

    return rval;
}

std::pair<bool, std::unique_ptr<mxs::SSLContext>> create_ssl(const char* name, const mxb::SSLConfig& config)
{
    bool ok = true;
    auto ssl = mxs::SSLContext::create(config);

    if (!ssl)
    {
        MXS_ERROR("Unable to initialize SSL for server '%s'", name);
        ok = false;
    }
    else if (!ssl->valid())
    {
        // An empty ssl config should result in an empty pointer. This can be removed if Server stores
        // SSLContext as value.
        ssl.reset();
    }

    return {ok, std::move(ssl)};
}

void persistpoolmax_modified(const std::string& srvname, int64_t pool_size)
{
    auto func = [=]() {
            RoutingWorker::pool_set_size(srvname, pool_size);
        };
    mxs::RoutingWorker::broadcast(func, nullptr, mxb::Worker::EXECUTE_AUTO);
}
}

Server::ParamDiskSpaceLimits::ParamDiskSpaceLimits(cfg::Specification* pSpecification,
                                                   const char* zName, const char* zDescription)
    : cfg::ConcreteParam<ParamDiskSpaceLimits, DiskSpaceLimits>(
        pSpecification, zName, zDescription, AT_RUNTIME, OPTIONAL, MXS_MODULE_PARAM_STRING, value_type())
{
}

std::string Server::ParamDiskSpaceLimits::type() const
{
    return "disk_space_limits";
}

std::string Server::ParamDiskSpaceLimits::to_string(Server::ParamDiskSpaceLimits::value_type value) const
{
    std::vector<std::string> tmp;
    std::transform(value.begin(), value.end(), std::back_inserter(tmp),
                   [](const auto& a) {
                       return a.first + ':' + std::to_string(a.second);
                   });
    return mxb::join(tmp, ",");
}

bool Server::ParamDiskSpaceLimits::from_string(const std::string& value, value_type* pValue,
                                               std::string* pMessage) const
{
    return config_parse_disk_space_threshold(pValue, value.c_str());
}

json_t* Server::ParamDiskSpaceLimits::to_json(value_type value) const
{
    json_t* obj = value.empty() ? json_null() : json_object();

    for (const auto& a : value)
    {
        json_object_set_new(obj, a.first.c_str(), json_integer(a.second));
    }

    return obj;
}

bool Server::ParamDiskSpaceLimits::from_json(const json_t* pJson, value_type* pValue,
                                             std::string* pMessage) const
{
    bool ok = false;

    if (json_is_object(pJson))
    {
        ok = true;
        const char* key;
        json_t* value;
        value_type newval;

        json_object_foreach(const_cast<json_t*>(pJson), key, value)
        {
            if (json_is_integer(value))
            {
                newval[key] = json_integer_value(value);
            }
            else
            {
                ok = false;
                *pMessage = "'"s + key + "' is not a JSON number.";
                break;
            }
        }
    }
    else if (json_is_string(pJson))
    {
        // Allow conversion from the INI format string to make it easier to configure this via maxctrl:
        // defining JSON objects with it is not very convenient.
        ok = from_string(json_string_value(pJson), pValue, pMessage);
    }
    else if (json_is_null(pJson))
    {
        ok = true;
    }
    else
    {
        *pMessage = "Not a JSON object or JSON null.";
    }

    return ok;
}

bool Server::configure(const mxs::ConfigParameters& params)
{
    return m_settings.configure(params) && post_configure();
}

bool Server::configure(json_t* params)
{
    return m_settings.configure(params) && post_configure();
}

Server::Settings::Settings(const std::string& name)
    : mxs::config::Configuration(name, &s_spec)
    , m_type(this, &s_type)
    , m_protocol(this, &s_protocol)
    , m_authenticator(this, &s_authenticator)
    , m_address(this, &s_address)
    , m_socket(this, &s_socket)
    , m_port(this, &s_port)
    , m_extra_port(this, &s_extra_port)
    , m_priority(this, &s_priority)
    , m_monitoruser(this, &s_monitoruser)
    , m_monitorpw(this, &s_monitorpw)
    , m_persistmaxtime(this, &s_persistmaxtime)
    , m_proxy_protocol(this, &s_proxy_protocol)
    , m_disk_space_threshold(this, &s_disk_space_threshold)
    , m_rank(this, &s_rank)
    , m_max_connections(this, &s_max_connections)
    , m_ssl(this, &s_ssl)
    , m_ssl_cert(this, &s_ssl_cert)
    , m_ssl_key(this, &s_ssl_key)
    , m_ssl_ca(this, &s_ssl_ca)
    , m_ssl_version(this, &s_ssl_version)
    , m_ssl_cert_verify_depth(this, &s_ssl_cert_verify_depth)
    , m_ssl_verify_peer_certificate(this, &s_ssl_verify_peer_certificate)
    , m_ssl_verify_peer_host(this, &s_ssl_verify_peer_host)
    , m_ssl_cipher(this, &s_ssl_cipher)
    , m_persistpoolmax(this, &s_persistpoolmax, [name](int64_t val) {
                           persistpoolmax_modified(name, val);
                       })
{
}

bool Server::Settings::post_configure(const std::map<string, mxs::ConfigParameters>& nested)
{
    mxb_assert(nested.empty());

    auto addr = !m_address.get().empty() ? m_address.get() : m_socket.get();

    careful_strcpy(address, MAX_ADDRESS_LEN, addr);
    careful_strcpy(monuser, MAX_MONUSER_LEN, m_monitoruser.get());
    careful_strcpy(monpw, MAX_MONPW_LEN, m_monitorpw.get());

    m_have_disk_space_limits.store(!m_disk_space_threshold.get().empty());

    return true;
}

// static
const cfg::Specification& Server::specification()
{
    return s_spec;
}

std::unique_ptr<Server> Server::create(const char* name, const mxs::ConfigParameters& params)
{
    std::unique_ptr<Server> rval;

    if (s_spec.validate(params))
    {
        if (auto server = std::make_unique<Server>(name))
        {
            if (server->configure(params))
            {
                rval = std::move(server);
            }
        }
    }

    return rval;
}

std::unique_ptr<Server> Server::create(const char* name, json_t* json)
{
    std::unique_ptr<Server> rval;

    if (s_spec.validate(json))
    {
        if (auto server = std::make_unique<Server>(name))
        {
            if (server->configure(json))
            {
                rval = std::move(server);
            }
        }
    }

    return rval;
}

Server* Server::create_test_server()
{
    static int next_id = 1;
    string name = "TestServer" + std::to_string(next_id++);
    return new Server(name);
}

void Server::set_status(uint64_t bit)
{
    m_status |= bit;
}

void Server::clear_status(uint64_t bit)
{
    m_status &= ~bit;
}

void Server::assign_status(uint64_t status)
{
    m_status = status;
}

bool Server::set_monitor_user(const string& username)
{
    bool rval = false;
    if (username.length() <= MAX_MONUSER_LEN)
    {
        careful_strcpy(m_settings.monuser, MAX_MONUSER_LEN, username);
        rval = true;
    }
    else
    {
        MXS_ERROR(ERR_TOO_LONG_CONFIG_VALUE, CN_MONITORUSER, MAX_MONUSER_LEN);
    }
    return rval;
}

bool Server::set_monitor_password(const string& password)
{
    bool rval = false;
    if (password.length() <= MAX_MONPW_LEN)
    {
        careful_strcpy(m_settings.monpw, MAX_MONPW_LEN, password);
        rval = true;
    }
    else
    {
        MXS_ERROR(ERR_TOO_LONG_CONFIG_VALUE, CN_MONITORPW, MAX_MONPW_LEN);
    }
    return rval;
}

string Server::monitor_user() const
{
    return m_settings.monuser;
}

string Server::monitor_password() const
{
    return m_settings.monpw;
}

bool Server::set_address(const string& new_address)
{
    bool rval = false;
    if (new_address.length() <= MAX_ADDRESS_LEN)
    {
        if (m_settings.m_address.set(new_address))
        {
            careful_strcpy(m_settings.address, MAX_ADDRESS_LEN, new_address);
            rval = true;
        }
        else
        {
            MXS_ERROR("The specifed server address '%s' is not valid.", new_address.c_str());
        }
    }
    else
    {
        MXS_ERROR(ERR_TOO_LONG_CONFIG_VALUE, CN_ADDRESS, MAX_ADDRESS_LEN);
    }
    return rval;
}

void Server::set_port(int new_port)
{
    m_settings.m_port.set(new_port);
}

void Server::set_extra_port(int new_port)
{
    m_settings.m_extra_port.set(new_port);
}

std::shared_ptr<mxs::SSLContext> Server::ssl() const
{
    return *m_ssl_ctx;
}

mxb::SSLConfig Server::ssl_config() const
{
    std::lock_guard<std::mutex> guard(m_ssl_lock);
    return m_ssl_config;
}

bool Server::proxy_protocol() const
{
    return m_settings.m_proxy_protocol.get();
}

void Server::set_proxy_protocol(bool proxy_protocol)
{
    m_settings.m_proxy_protocol.set(proxy_protocol);
}

uint8_t Server::charset() const
{
    return m_charset;
}

void Server::set_charset(uint8_t charset)
{
    m_charset = charset;
}

void Server::set_session_track_system_variables(std::string&& value)
{
    std::lock_guard<std::mutex> guard(m_var_lock);
    m_session_track_system_variables = std::move(value);
}

std::string Server::get_session_track_system_variables() const
{
    std::lock_guard<std::mutex> guard(m_var_lock);
    return m_session_track_system_variables;
}

uint64_t Server::status_from_string(const char* str)
{
    static std::vector<std::pair<const char*, uint64_t>> status_bits =
    {
        {"running",      SERVER_RUNNING },
        {"master",       SERVER_MASTER  },
        {"slave",        SERVER_SLAVE   },
        {"synced",       SERVER_JOINED  },
        {"maintenance",  SERVER_MAINT   },
        {"maint",        SERVER_MAINT   },
        {"drain",        SERVER_DRAINING},
        {"blr",          SERVER_BLR     },
        {"binlogrouter", SERVER_BLR     }
    };

    for (const auto& a : status_bits)
    {
        if (strcasecmp(str, a.first) == 0)
        {
            return a.second;
        }
    }

    return 0;
}

void Server::set_gtid_list(const std::vector<std::pair<uint32_t, uint64_t>>& domains)
{
    mxs::MainWorker::get()->execute(
        [this, domains]() {
            auto gtids = *m_gtids;

            for (const auto& p : domains)
            {
                gtids[p.first] = p.second;
            }

            m_gtids.assign(gtids);
        }, mxb::Worker::EXECUTE_AUTO);
}

void Server::clear_gtid_list()
{
    mxs::MainWorker::get()->execute(
        [this]() {
            m_gtids->clear();
            m_gtids.assign(*m_gtids);
        }, mxb::Worker::EXECUTE_AUTO);
}

uint64_t Server::gtid_pos(uint32_t domain) const
{
    const auto& gtids = *m_gtids;
    auto it = gtids.find(domain);
    return it != gtids.end() ? it->second : 0;
}

void Server::set_version(uint64_t version_num, const std::string& version_str)
{
    bool changed = m_info.set(version_num, version_str);
    if (changed)
    {
        auto type_string = m_info.type_string();
        auto vrs = m_info.version_num();
        MXS_NOTICE("'%s' sent version string '%s'. Detected type: '%s', version: %i.%i.%i.",
                   name(), version_str.c_str(), type_string.c_str(), vrs.major, vrs.minor, vrs.patch);
    }
}

json_t* Server::json_attributes() const
{
    /** Resource attributes */
    json_t* attr = json_object();

    /** Store server parameters in attributes */
    json_t* params = json_object();
    m_settings.fill(params);

    // Return either address/port or socket, not both
    auto socket = json_object_get(params, CN_SOCKET);

    if (socket && !json_is_null(socket))
    {
        mxb_assert(json_is_string(socket));
        json_object_set_new(params, CN_ADDRESS, json_null());
        json_object_set_new(params, CN_PORT, json_null());
    }
    else
    {
        json_object_set_new(params, CN_SOCKET, json_null());
    }

    // Remove unwanted parameters
    json_object_del(params, CN_TYPE);
    json_object_del(params, CN_AUTHENTICATOR);
    json_object_del(params, CN_PROTOCOL);

    json_object_set_new(attr, CN_PARAMETERS, params);

    /** Store general information about the server state */
    string stat = status_string();
    json_object_set_new(attr, CN_STATE, json_string(stat.c_str()));

    json_object_set_new(attr, CN_VERSION_STRING, json_string(m_info.version_string()));
    json_object_set_new(attr, "replication_lag", json_integer(replication_lag()));

    json_t* statistics = stats().to_json();
    auto pool_stats = mxs::RoutingWorker::pool_get_stats(this);
    json_object_set_new(statistics, "persistent_connections", json_integer(pool_stats.curr_size));
    json_object_set_new(statistics, "max_pool_size", json_integer(pool_stats.max_size));
    json_object_set_new(statistics, "reused_connections", json_integer(pool_stats.times_found));
    json_object_set_new(statistics, "connection_pool_empty", json_integer(pool_stats.times_empty));
    maxbase::Duration response_ave(mxb::from_secs(response_time_average()));
    json_object_set_new(statistics, "adaptive_avg_select_time",
                        json_string(mxb::to_string(response_ave).c_str()));


    if (is_resp_distribution_enabled())
    {
        const auto& distr_obj = json_object();
        json_object_set_new(distr_obj, "read", response_distribution_to_json(Operation::READ));
        json_object_set_new(distr_obj, "write", response_distribution_to_json(Operation::WRITE));
        json_object_set_new(statistics, "response_time_distribution", distr_obj);
    }

    json_object_set_new(attr, "statistics", statistics);
    return attr;
}

json_t* Server::response_distribution_to_json(Operation opr) const
{
    const auto& distr_obj = json_object();
    const auto& arr = json_array();
    auto my_distribution = get_complete_response_distribution(opr);

    for (const auto& element : my_distribution.get())
    {
        auto row_obj = json_object();

        json_object_set_new(row_obj, "time",
                            json_string(std::to_string(mxb::to_secs(element.limit)).c_str()));
        json_object_set_new(row_obj, "total", json_real(mxb::to_secs(element.total)));
        json_object_set_new(row_obj, "count", json_integer(element.count));

        json_array_append_new(arr, row_obj);
    }
    json_object_set_new(distr_obj, "distribution", arr);
    json_object_set_new(distr_obj, "range_base",
                        json_integer(my_distribution.range_base()));
    json_object_set_new(distr_obj, "operation", json_string(opr == Operation::READ ? "read" : "write"));

    return distr_obj;
}

json_t* Server::to_json_data(const char* host) const
{
    json_t* rval = json_object();

    /** Add resource identifiers */
    json_object_set_new(rval, CN_ID, json_string(name()));
    json_object_set_new(rval, CN_TYPE, json_string(CN_SERVERS));

    /** Attributes */
    json_object_set_new(rval, CN_ATTRIBUTES, json_attributes());
    json_object_set_new(rval, CN_LINKS, mxs_json_self_link(host, CN_SERVERS, name()));

    return rval;
}

bool Server::post_configure()
{
    json_t* js = m_settings.to_json();
    auto params = mxs::ConfigParameters::from_json(js);
    json_decref(js);

    bool ok;
    std::shared_ptr<mxs::SSLContext> ctx;
    std::tie(ok, ctx) = create_ssl(m_name.c_str(), create_ssl_config());

    if (ok)
    {
        m_ssl_ctx.assign(ctx);
        std::lock_guard<std::mutex> guard(m_ssl_lock);
        m_ssl_config = ctx ? ctx->config() : mxb::SSLConfig();
    }

    return ok;
}

mxb::SSLConfig Server::create_ssl_config()
{
    mxb::SSLConfig cfg;

    cfg.enabled = m_settings.m_ssl.get();
    cfg.key = m_settings.m_ssl_key.get();
    cfg.cert = m_settings.m_ssl_cert.get();
    cfg.ca = m_settings.m_ssl_ca.get();
    cfg.version = m_settings.m_ssl_version.get();
    cfg.verify_peer = m_settings.m_ssl_verify_peer_certificate.get();
    cfg.verify_host = m_settings.m_ssl_verify_peer_host.get();
    cfg.verify_depth = m_settings.m_ssl_cert_verify_depth.get();
    cfg.cipher = m_settings.m_ssl_cipher.get();

    return cfg;
}

bool Server::VersionInfo::set(uint64_t version, const std::string& version_str)
{
    uint32_t major = version / 10000;
    uint32_t minor = (version - major * 10000) / 100;
    uint32_t patch = version - major * 10000 - minor * 100;

    Type new_type = Type::UNKNOWN;
    auto version_strz = version_str.c_str();
    if (strcasestr(version_strz, "xpand") || strcasestr(version_strz, "clustrix"))
    {
        new_type = Type::XPAND;
    }
    else if (strcasestr(version_strz, "binlogrouter"))
    {
        new_type = Type::BLR;
    }
    else if (strcasestr(version_strz, "mariadb"))
    {
        // Needs to be after Xpand and BLR as their version strings may include "mariadb".
        new_type = Type::MARIADB;
    }
    else if (!version_str.empty())
    {
        new_type = Type::MYSQL;     // Used for any unrecognized server types.
    }

    bool changed = false;
    /* This only protects against concurrent writing which could result in garbled values. Reads are not
     * synchronized. Since writing is rare, this is an unlikely issue. Readers should be prepared to
     * sometimes get inconsistent values. */
    Guard lock(m_lock);

    if (new_type != m_type || version != m_version_num.total || version_str != m_version_str)
    {
        m_type = new_type;
        m_version_num.total = version;
        m_version_num.major = major;
        m_version_num.minor = minor;
        m_version_num.patch = patch;
        careful_strcpy(m_version_str, MAX_VERSION_LEN, version_str);
        changed = true;
    }
    return changed;
}

const Server::VersionInfo::Version& Server::VersionInfo::version_num() const
{
    return m_version_num;
}

Server::VersionInfo::Type Server::VersionInfo::type() const
{
    return m_type;
}

const char* Server::VersionInfo::version_string() const
{
    return m_version_str;
}

bool SERVER::VersionInfo::is_database() const
{
    auto t = m_type;
    return t == Type::MARIADB || t == Type::XPAND || t == Type::MYSQL;
}

std::string SERVER::VersionInfo::type_string() const
{
    string type_str;
    switch (m_type)
    {
    case Type::UNKNOWN:
        type_str = "Unknown";
        break;

    case Type::MYSQL:
        type_str = "MySQL";
        break;

    case Type::MARIADB:
        type_str = "MariaDB";
        break;

    case Type::XPAND:
        type_str = "Xpand";
        break;

    case Type::BLR:
        type_str = "MaxScale Binlog Router";
        break;
    }
    return type_str;
}

const SERVER::VersionInfo& Server::info() const
{
    return m_info;
}

maxscale::ResponseDistribution& Server::response_distribution(Operation opr)
{
    mxb_assert(opr != Operation::NOP);

    if (opr == Operation::READ)
    {
        return *m_read_distributions;
    }
    else
    {
        return *m_write_distributions;
    }
}

const maxscale::ResponseDistribution& Server::response_distribution(Operation opr) const
{
    return const_cast<Server*>(this)->response_distribution(opr);
}

// The threads modify a reference to a ResponseDistribution, which is
// in a WorkerGlobal. So when the code below reads a copy (the +=)
// there can be a small inconsistency: the count might have been updated,
// but the total not, or even the other way around as there are no atomics
// in ResponseDistribution.
// Fine, it is still thread safe. All in the name of performance.
maxscale::ResponseDistribution Server::get_complete_response_distribution(Operation opr) const
{
    mxb_assert(opr != Operation::NOP);

    maxscale::ResponseDistribution ret = m_read_distributions->with_stats_reset();

    const auto& distr = (opr == Operation::READ) ? m_read_distributions : m_write_distributions;

    for (auto rhs : distr.values())
    {
        ret += rhs;
    }

    return ret;
}

ServerEndpoint::ServerEndpoint(mxs::Component* up, MXS_SESSION* session, Server* server)
    : m_up(up)
    , m_session(session)
    , m_server(server)
    , m_query_time(RoutingWorker::get_current())
    , m_read_distribution(server->response_distribution(Operation::READ))
    , m_write_distribution(server->response_distribution(Operation::WRITE))
{
}

ServerEndpoint::~ServerEndpoint()
{
    if (is_open())
    {
        close();
    }
}

bool ServerEndpoint::connect()
{
    mxb_assert(m_connstatus == ConnStatus::NO_CONN || m_connstatus == ConnStatus::IDLE_POOLED);
    mxb::LogScope scope(m_server->name());
    auto worker = m_session->worker();
    auto res = worker->get_backend_connection(m_server, m_session, this);
    bool rval = false;
    if (res.conn)
    {
        m_conn = res.conn;
        m_connstatus = ConnStatus::CONNECTED;
        rval = true;
    }
    else if (res.wait_for_conn)
    {
        // 'get_backend_connection' succeeded without a connection. This means that a backend connection
        // limit with idle pooling is in effect. A connection slot may become available soon.
        m_connstatus = ConnStatus::WAITING_FOR_CONN;
        worker->add_conn_wait_entry(this, static_cast<Session*>(m_session));
        rval = true;
    }
    else
    {
        // Connection failure.
        m_connstatus = ConnStatus::NO_CONN;
    }
    return rval;
}

void ServerEndpoint::close()
{
    mxb::LogScope scope(m_server->name());

    bool normal_close = (m_connstatus == ConnStatus::CONNECTED);
    if (normal_close || m_connstatus == ConnStatus::CONNECTED_FAILED)
    {
        auto* dcb = m_conn->dcb();
        bool moved_to_pool = false;
        if (normal_close)
        {
            // Try to move the connection into the pool. If it fails, close normally.
            moved_to_pool = dcb->session()->normal_quit() && dcb->manager()->move_to_conn_pool(dcb);
        }

        if (moved_to_pool)
        {
            mxb_assert(dcb->is_open());
        }
        else
        {
            BackendDCB::close(dcb);
            m_server->stats().remove_connection();
        }
        m_conn = nullptr;
        m_session->worker()->notify_connection_available(m_server);
    }
    else if (m_connstatus == ConnStatus::WAITING_FOR_CONN)
    {
        // Erase the entry in the wait list.
        m_session->worker()->erase_conn_wait_entry(this, static_cast<Session*>(m_session));
    }

    // This function seems to be called twice when closing an Endpoint. Take this into account by always
    // setting connstatus. Should be fixed properly at some point.
    m_connstatus = ConnStatus::NO_CONN;
}

void ServerEndpoint::handle_failed_continue()
{
    mxs::Reply dummy;
    // Need to give some kind of error packet or handleError will crash. The Endpoint will be closed
    // after the call.
    auto errorbuf = mysql_create_custom_error(
        1, 0, 1927, "Lost connection to server when reusing connection.");
    m_up->handleError(mxs::ErrorType::PERMANENT, errorbuf, this, dummy);
}

bool ServerEndpoint::is_open() const
{
    return m_connstatus != ConnStatus::NO_CONN;
}

bool ServerEndpoint::routeQuery(GWBUF* buffer)
{
    mxb::LogScope scope(m_server->name());
    mxb_assert(is_open());
    int32_t rval = 0;

    const uint32_t read_only_types = QUERY_TYPE_READ | QUERY_TYPE_LOCAL_READ
        | QUERY_TYPE_USERVAR_READ | QUERY_TYPE_SYSVAR_READ | QUERY_TYPE_GSYSVAR_READ;

    uint32_t type_mask = 0;

    if (modutil_is_SQL(buffer) || modutil_is_SQL_prepare(buffer))
    {
        if (!gwbuf_is_contiguous(buffer))
        {
            buffer = gwbuf_make_contiguous(buffer);
        }

        type_mask = qc_get_type_mask(buffer);
    }

    auto is_read_only = !(type_mask & ~read_only_types);
    auto is_read_only_trx = m_session->protocol_data()->is_trx_read_only();
    auto not_master = !(m_server->status() & SERVER_MASTER);
    auto opr = (not_master || is_read_only || is_read_only_trx) ? Operation::READ : Operation::WRITE;

    switch (m_connstatus)
    {
    case ConnStatus::NO_CONN:
    case ConnStatus::CONNECTED_FAILED:
        mxb_assert(!true);      // Means that an earlier failure was not properly handled.
        break;

    case ConnStatus::CONNECTED:
        rval = m_conn->write(buffer);
        m_server->stats().add_packet();
        break;

    case ConnStatus::IDLE_POOLED:
        // Connection was pre-emptively pooled. Try to get another one.
        if (connect())
        {
            if (m_connstatus == ConnStatus::CONNECTED)
            {
                MXB_INFO("Session %lu connection to %s restored from pool.",
                         m_session->id(), m_server->name());
                rval = m_conn->write(buffer);
                m_server->stats().add_packet();
            }
            else
            {
                // Waiting for another one.
                m_delayed_packets.emplace_back(buffer);
                rval = 1;
            }
        }
        else
        {
            // Connection failed, return error.
            gwbuf_free(buffer);
        }
        break;

    case ConnStatus::WAITING_FOR_CONN:
        // Already waiting for a connection. Save incoming buffer so it can be sent once a connection
        // is available.
        m_delayed_packets.emplace_back(buffer);
        rval = 1;
        break;
    }
    m_query_time.start(opr);    // always measure
    return rval;
}

bool ServerEndpoint::clientReply(GWBUF* buffer, mxs::ReplyRoute& down, const mxs::Reply& reply)
{
    mxb::LogScope scope(m_server->name());
    mxb_assert(is_open());
    down.push_back(this);

    m_query_time.stop();    // always measure

    if (m_query_time.opr() == Operation::READ)
    {
        m_read_distribution.add(m_query_time.duration());
    }
    else
    {
        m_write_distribution.add(m_query_time.duration());
    }

    return m_up->clientReply(buffer, down, reply);
}

bool ServerEndpoint::handleError(mxs::ErrorType type, GWBUF* error,
                                 mxs::Endpoint* down, const mxs::Reply& reply)
{
    mxb::LogScope scope(m_server->name());
    mxb_assert(is_open());
    return m_up->handleError(type, error, this, reply);
}

bool ServerEndpoint::try_to_pool()
{
    bool rval = false;
    if (m_connstatus == ConnStatus::CONNECTED)
    {
        auto* dcb = m_conn->dcb();
        if (dcb->manager()->move_to_conn_pool(dcb))
        {
            rval = true;
            m_connstatus = ConnStatus::IDLE_POOLED;
            m_conn = nullptr;
            MXB_INFO("Session %lu connection to %s pooled.", m_session->id(), m_server->name());
            m_session->worker()->notify_connection_available(m_server);
        }
    }
    return rval;
}

ServerEndpoint::ContinueRes ServerEndpoint::continue_connecting()
{
    mxb_assert(m_connstatus == ConnStatus::WAITING_FOR_CONN);
    auto res = m_session->worker()->get_backend_connection(m_server, m_session, this);
    auto rval = ContinueRes::FAIL;
    if (res.conn)
    {
        m_conn = res.conn;
        m_connstatus = ConnStatus::CONNECTED;

        // Send all pending packets one by one to the connection. The physical connection may not be ready
        // yet, but the protocol should keep track of the state.
        bool success = true;
        for (auto& packet : m_delayed_packets)
        {
            if (m_conn->write(packet.release()) == 0)
            {
                success = false;
                break;
            }
        }
        m_delayed_packets.clear();

        if (success)
        {
            rval = ContinueRes::SUCCESS;
        }
        else
        {
            // This special state ensures the connection is not pooled.
            m_connstatus = ConnStatus::CONNECTED_FAILED;
        }
    }
    else if (res.wait_for_conn)
    {
        // Still no connection.
        rval = ContinueRes::WAIT;
    }
    else
    {
        m_connstatus = ConnStatus::NO_CONN;
    }
    return rval;
}

SERVER* ServerEndpoint::server() const
{
    return m_server;
}

MXS_SESSION* ServerEndpoint::session() const
{
    return m_session;
}

std::unique_ptr<mxs::Endpoint> Server::get_connection(mxs::Component* up, MXS_SESSION* session)
{
    return std::unique_ptr<mxs::Endpoint>(new ServerEndpoint(up, session, this));
}
