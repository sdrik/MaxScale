/*
 * Copyright (c) 2019 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-09-20
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#define MXS_MODULE_NAME "qlafilter"

#include <maxscale/ccdefs.hh>

#include <string>
#include <maxbase/stopwatch.hh>
#include <maxscale/config.hh>
#include <maxscale/filter.hh>
#include <maxscale/pcre2.hh>
#include <maxscale/workerlocal.hh>
#include <maxsimd/canonical.hh>

class QlaFilterSession;
struct LogEventElems;

using File = std::shared_ptr<FILE>;

/**
 * A instance structure, the assumption is that the option passed
 * to the filter is simply a base for the filename to which the queries
 * are logged.
 *
 * To this base a session number is attached such that each session will
 * have a unique name.
 */
class QlaInstance : public mxs::Filter
{
public:
    QlaInstance(const QlaInstance&) = delete;
    QlaInstance& operator=(const QlaInstance&) = delete;

    QlaInstance(const std::string& name);
    ~QlaInstance();

    /* Log file save mode flags. */
    static const int64_t LOG_FILE_SESSION = (1 << 0);   /**< Default value, session specific files */
    static const int64_t LOG_FILE_UNIFIED = (1 << 1);   /**< One file shared by all sessions */
    static const int64_t LOG_FILE_STDOUT = (1 << 2);    /**< Same as unified, but to stdout */

    /* Flags for controlling extra log entry contents */
    static const int64_t LOG_DATA_SERVICE = (1 << 0);
    static const int64_t LOG_DATA_SESSION = (1 << 1);
    static const int64_t LOG_DATA_DATE = (1 << 2);
    static const int64_t LOG_DATA_USER = (1 << 3);
    static const int64_t LOG_DATA_QUERY = (1 << 4);
    static const int64_t LOG_DATA_REPLY_TIME = (1 << 5);
    static const int64_t LOG_DATA_DEFAULT_DB = (1 << 6);

    /**
     * Associate a new session with this instance of the filter. Creates a session-specific logfile.
     *
     * @param session   The generic session
     * @return          Router session on null on error
     */
    mxs::FilterSession* newSession(MXS_SESSION* session, SERVICE* service) override;

    /**
     * Create an instance of the filter for a particular service within MaxScale.
     *
     * @param name      The name of the instance (as defined in the config file)
     * @param params    The array of name/value pair parameters for the filter
     * @return          The new filter instance, or NULL on error
     */
    static QlaInstance* create(const char* name);

    /**
     * Read contents of unified log file and save to json object.
     *
     * @param start First line to output
     * @param end Last line to output
     * @param output Where to save read lines
     * @return True if file was opened
     */
    bool read_to_json(int start, int end, json_t** output) const;

    json_t* diagnostics() const override;

    mxs::config::Configuration& getConfiguration() override
    {
        return m_settings;
    }

    uint64_t getCapabilities() const override;

    bool post_configure();

    class Settings : public mxs::config::Configuration
    {
    public:
        Settings(const std::string& name, QlaInstance* instance);

        struct Values
        {
            bool        use_canonical_form {false};
            bool        write_unified_log {false};
            bool        write_session_log {false};
            bool        write_stdout_log {false};
            uint32_t    log_file_data_flags {0};/* What data is saved to the files */
            uint32_t    log_file_types {0};
            uint64_t    session_data_flags {0}; /* What data is printed to session files */
            std::string filebase;               /* The filename base */
            bool        flush_writes {false};   /* Flush log file after every write? */
            bool        append {true};          /* Open files in append-mode? */
            std::string query_newline;          /* Character(s) used to replace a newline within a query */
            std::string separator;              /*  Character(s) used to separate elements */
            std::string user_name;              /* The user name to filter on */
            std::string source;                 /* The source of the client connection to filter on */

            mxs::config::RegexValue match;  /* Optional text to match against */
            mxs::config::RegexValue exclude;/* Optional text to match against for exclusion */
            uint32_t                options;/* Regular expression options */
        };

        const Values& values() const
        {
            return m_v;
        }

    protected:
        bool post_configure(const std::map<std::string, mxs::ConfigParameters>& nested_params) override final;

    private:
        QlaInstance* m_instance;
        Values       m_v;
    };

    class LogManager
    {
    public:
        static std::unique_ptr<LogManager> create(const Settings::Values& settings);
        ~LogManager();

        bool        open_unified_logfile();
        File        open_session_log_file(const std::string& filename) const;
        std::string generate_log_header(uint64_t data_flags) const;
        void        check_reopen_session_file(const std::string& filename, File* ppFile) const;
        void        write_unified_log_entry(const std::string& contents);
        bool        write_to_logfile(File fp, const std::string& contents) const;
        void        write_stdout_log_entry(const std::string& contents) const;
        bool        match_exclude(const char* sql, int len);
        bool        read_to_json(int start, int end, json_t** output);

        const Settings::Values& settings() const
        {
            return m_settings;
        }

    private:
        LogManager(const Settings::Values& settings);
        bool prepare();
        File open_log_file(uint64_t data_flags, const std::string& filename) const;
        void check_reopen_file(const std::string& filename, uint64_t data_flags, File* ppFile) const;

        Settings::Values m_settings;
        std::mutex       m_file_lock;                   /* Protects access to the unified log file */
        std::string      m_unified_filename;            /* Filename of the unified log file */
        File             m_unified_fp {nullptr};        /* Unified log file. */
        int              m_rotation_count {0};          /* Log rotation counter */
        bool             m_write_error_logged {false};  /* Avoid repeatedly printing some errors/warnings. */
    };

    std::shared_ptr<LogManager> log() const
    {
        return *m_log;
    }

private:
    Settings          m_settings;
    const std::string m_name;   /* Filter definition name */

    mxs::WorkerGlobal<std::shared_ptr<LogManager>> m_log;
};

/* The session structure for this QLA filter. */
class QlaFilterSession : public mxs::FilterSession
{
public:
    QlaFilterSession(const QlaFilterSession&) = delete;
    QlaFilterSession& operator=(const QlaFilterSession&) = delete;
    QlaFilterSession(QlaInstance& instance, MXS_SESSION* session, SERVICE* service);
    ~QlaFilterSession();

    /**
     * Prepares a session for routing. Checks if username and/or host match and opens the log file.
     *
     * @return True on success. If false is returned, the session should be closed and deleted.
     */
    bool prepare();

    bool routeQuery(GWBUF* query) override;

    bool clientReply(GWBUF* buffer, const mxs::ReplyRoute& down, const mxs::Reply& reply) override;

    json_t* diagnostics() const;

private:
    std::shared_ptr<QlaInstance::LogManager> m_log;

    std::string       m_filename;   /* The session-specific log file name */
    const std::string m_user;       /* Client username */
    const std::string m_remote;     /* Client address */
    const std::string m_service;    /* The service name this filter is attached to. */
    const uint64_t    m_ses_id {0}; /* The session this filter session serves. */

    bool m_active {false};      /* Is session active? */

    File m_logfile {nullptr};           /* The session-specific log file */
    int  m_rotation_count {0};          /* Log rotation counter */
    bool m_write_error_logged {false};  /* Has write error been logged */

    std::string          m_sql;             // Sql, in canonical form if asked for
    mxb::TimePoint       m_begin_time;      // Timer value at the moment of receiving query.
    std::string          m_wall_time_str;   // Wall time as a string
    std::chrono::seconds m_last_wall_second;

    maxsimd::Markers m_markers;     /* maxsimd::get_canonical needs these, kept outside for re-use */

    void        write_log_entries(const LogEventElems& elems);
    void        write_session_log_entry(const std::string& entry);
    std::string generate_log_entry(uint64_t data_flags, const LogEventElems& elems) const;
};

/**
 * Helper struct for passing some log entry info around. Other entry elements are fields of the
 * filter session. Fields are pointers to avoid unnecessary copies.
 */
struct LogEventElems
{
    mxb::TimePoint     begin_time;
    const std::string& sql;
    mxb::TimePoint     end_time;

    LogEventElems(mxb::TimePoint begin_time, const std::string& sql, mxb::TimePoint end_time)
        : begin_time(begin_time)
        , sql(sql)
        , end_time(end_time)
    {
    }
};
