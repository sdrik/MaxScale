/*
 * Copyright (c) 2018 MariaDB Corporation Ab
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
#pragma once

#include <maxbase/ccdefs.hh>

#include <array>
#include <cstring>
#include <functional>
#include <map>
#include <memory>
#include <thread>
#include <unordered_map>

#include <maxbase/assert.h>
#include <maxbase/atomic.h>
#include <maxbase/average.hh>
#include <maxbase/messagequeue.hh>
#include <maxbase/semaphore.hh>
#include <maxbase/worker.h>
#include <maxbase/workertask.hh>
#include <maxbase/random.hh>
#include <maxbase/stopwatch.hh>

using namespace std::chrono_literals;

namespace maxbase
{

struct WORKER_STATISTICS
{
    static const int     MAXNFDS = 10;
    static const int64_t N_QUEUE_TIMES = 30;

    int64_t n_read = 0;     /*< Number of read events   */
    int64_t n_write = 0;    /*< Number of write events  */
    int64_t n_error = 0;    /*< Number of error events  */
    int64_t n_hup = 0;      /*< Number of hangup events */
    int64_t n_accept = 0;   /*< Number of accept events */
    int64_t n_polls = 0;    /*< Number of poll cycles   */
    int64_t n_pollev = 0;   /*< Number of polls returning events */
    int64_t evq_avg = 0;    /*< Average event queue length */
    int64_t evq_max = 0;    /*< Maximum event queue length */
    int64_t maxqtime = 0;
    int64_t maxexectime = 0;

    std::array<int64_t, MAXNFDS>            n_fds {};   /*< Number of wakeups with particular n_fds value */
    std::array<uint32_t, N_QUEUE_TIMES + 1> qtimes {};
    std::array<uint32_t, N_QUEUE_TIMES + 1> exectimes {};
};

/**
 * WorkerLoad is a class that calculates the load percentage of a worker
 * thread, based upon the relative amount of time the worker spends in
 * epoll_wait().
 *
 * If during a time period of length T milliseconds, the worker thread
 * spends t milliseconds in epoll_wait(), then the load of the worker is
 * calculated as 100 * ((T - t) / T). That is, if the worker spends all
 * the time in epoll_wait(), then the load is 0 and if the worker spends
 * no time waiting in epoll_wait(), then the load is 100.
 */
class WorkerLoad
{
    WorkerLoad(const WorkerLoad&) = delete;
    WorkerLoad& operator=(const WorkerLoad&) = delete;

public:
    enum counter_t
    {
        ONE_SECOND = 1000,
        ONE_MINUTE = 60 * ONE_SECOND,
        ONE_HOUR   = 60 * ONE_MINUTE,
    };

    const mxb::Duration GRANULARITY = 1s;

    /**
     * Constructor
     */
    WorkerLoad();

    /**
     * Reset the load calculation. Should be called immediately before the
     * worker enters its eternal epoll_wait()-loop.
     */
    void reset(mxb::TimePoint now)
    {
        m_start_time = now;
        m_wait_start = now;
        m_wait_time = 0s;
    }

    /**
     * To be used for signaling that the worker is about to call epoll_wait().
     *
     * @param now  The current time.
     *
     * @return The timeout the client should pass to epoll_wait().
     */
    mxb::Duration about_to_wait(mxb::TimePoint now)
    {
        m_wait_start = now;

        auto duration = now - m_start_time;

        if (duration >= GRANULARITY)
        {
            about_to_work(now);
            duration = GRANULARITY;
        }
        else
        {
            duration = GRANULARITY - duration;
        }

        return duration;
    }

    /**
     * To be used for signaling that the worker has returned from epoll_wait().
     *
     * @param now  The current time.
     */
    void about_to_work(TimePoint now);

    /**
     * Returns the last calculated load,
     *
     * @return A value between 0 and 100.
     */
    uint8_t percentage(counter_t counter) const
    {
        switch (counter)
        {
        case ONE_SECOND:
            return m_load_1_second.value();

        case ONE_MINUTE:
            return m_load_1_minute.value();

        case ONE_HOUR:
            return m_load_1_hour.value();

        default:
            mxb_assert(!true);
            return 0;
        }
    }

    /**
     * When was the last 1 second period started.
     *
     * @return The start time.
     */
    mxb::TimePoint start_time() const
    {
        return m_start_time;
    }

    /**
     * @return Convert a timepoint to milliseconds (for C-style interfaces).
     */
    static uint64_t get_time_ms(TimePoint tp);

private:

    mxb::TimePoint m_start_time {}; /*< When was the current 1-second period started. */
    mxb::TimePoint m_wait_start {}; /*< The time when the worker entered epoll_wait(). */
    mxb::Duration  m_wait_time {};  /*< How much time the worker has spent in epoll_wait(). */
    AverageN       m_load_1_hour;   /*< The average load during the last hour. */
    AverageN       m_load_1_minute; /*< The average load during the last minute. */
    Average1       m_load_1_second; /*< The load during the last 1-second period. */
};

/**
 * WorkerTimer is a timer class built on top of timerfd_create(2),
 * which means that each WorkerTimer instance will consume one file
 * descriptor. The implication of that is that there should not be
 * too many WorkerTimer instances. In order to be used, a WorkerTimer
 * needs a Worker instance in whose context the timer is triggered.
 */
class WorkerTimer : private MXB_POLL_DATA
{
    WorkerTimer(const WorkerTimer&) = delete;
    WorkerTimer& operator=(const WorkerTimer&) = delete;

public:
    virtual ~WorkerTimer();

    /**
     * @brief Start the timer.
     *
     * @param interval The initial delay in milliseconds before the
     *                 timer is triggered, and the subsequent interval
     *                 between triggers.
     *
     * @attention A value of 0 means that the timer is cancelled.
     */
    void start(int32_t interval);

    /**
     * @brief Cancel the timer.
     */
    void cancel();

protected:
    /**
     * @brief Constructor
     *
     * @param pWorker  The worker in whose context the timer is to run.
     */
    WorkerTimer(Worker* pWorker);

    /**
     * @brief Called when the timer is triggered.
     */
    virtual void tick() = 0;

private:
    uint32_t handle(Worker* pWorker, uint32_t events);

    static uint32_t handler(MXB_POLL_DATA* pThis, MXB_WORKER* pWorker, uint32_t events);

private:
    int     m_fd;       /**< The timerfd descriptor. */
    Worker* m_pWorker;  /**< The worker in whose context the timer runs. */
};

/**
 * @class Worker
 *
 * A Worker is a class capable of asynchronously processing events
 * associated with file descriptors. Internally Worker has a thread
 * and an epoll-instance of its own.
 */
class Worker : public MXB_WORKER
             , private MessageQueue::Handler
{
    Worker(const Worker&) = delete;
    Worker& operator=(const Worker&) = delete;

public:
    using STATISTICS = WORKER_STATISTICS;
    using Task = WorkerTask;
    using DisposableTask = WorkerDisposableTask;
    using Load = WorkerLoad;
    using Timer = WorkerTimer;
    using RandomEngine = XorShiftRandom;
    using CallIdT = int64_t;

    static constexpr CallIdT NO_CALL = -1;

    /**
     * A delegating timer that delegates the timer tick handling
     * to another object.
     */
    template<class T>
    class DelegatingTimer : public Timer
    {
        DelegatingTimer(const DelegatingTimer&) = delete;
        DelegatingTimer& operator=(const DelegatingTimer&) = delete;

    public:
        typedef void (T::* PMethod)();

        /**
         * @brief Constructor
         *
         * @param pWorker     The worker in whose context the timer runs.
         * @param pDelegatee  The object to whom the timer tick is delivered.
         * @param pMethod     The method to call on @c pDelegatee when the
         *                    timer is triggered.
         */
        DelegatingTimer(Worker* pWorker, T* pDelegatee, PMethod pMethod)
            : Timer(pWorker)
            , m_pDelegatee(pDelegatee)
            , m_pMethod(pMethod)
        {
        }

    private:
        void tick() override final
        {
            (m_pDelegatee->*m_pMethod)();
        }

    private:
        T*      m_pDelegatee;
        PMethod m_pMethod;
    };

    enum state_t
    {
        STOPPED,
        POLLING,
        PROCESSING,
        FINISHED
    };

    enum execute_mode_t
    {
        EXECUTE_DIRECT, /**< Always execute directly using the calling thread/worker. */
        EXECUTE_QUEUED, /**< Always execute via the event loop using this thread/worker. */
        EXECUTE_AUTO,   /**< If calling thread/worker is this worker, call directly otherwise queued. */
    };

    struct Call
    {
        enum action_t
        {
            EXECUTE,    /**< Execute the call */
            CANCEL      /**< Cancel the call */
        };
    };

    enum
    {
        MAX_EVENTS = 1000
    };

    /**
     * Constructs a worker.
     *
     * @param max_events  The maximum number of events that can be returned by
     *                    one call to epoll_wait.
     */
    Worker(int max_events = MAX_EVENTS);

    virtual ~Worker();

    /**
     * Returns the id of the worker
     *
     * @return The address of the worker cast to an int
     */
    virtual int id() const
    {
        return (intptr_t)this;
    }

    int load(Load::counter_t counter)
    {
        return m_load.percentage(counter);
    }

    /**
     * Returns the state of the worker.
     *
     * @return The current state.
     *
     * @attentions The state might have changed the moment after the function returns.
     */
    state_t state() const
    {
        return m_state;
    }

    /**
     * Returns statistics for this worker.
     *
     * @return The worker specific statistics.
     *
     * @attentions The statistics may change at any time.
     */
    const STATISTICS& statistics() const
    {
        return m_statistics;
    }

    /**
     * Return the count of descriptors.
     *
     * @param pnCurrent  On output the current number of descriptors.
     * @param pnTotal    On output the total number of descriptors.
     */
    void get_descriptor_counts(uint32_t* pnCurrent, uint64_t* pnTotal);

    /**
     * Return the random engine of this worker.
     */
    RandomEngine& random_engine();

    /**
     * Write random bytes to a buffer using the random generator of this worker. Should be only used
     * from within a worker thread.
     *
     * @param pOutput Output buffer
     * @param nBytes Bytes to write
     */
    static void gen_random_bytes(uint8_t* pOutput, size_t nBytes);

    /**
     * Returns the TimePoint when epoll_tick() was called. Use this in worker threads
     * instead of maxbase::Clock::now() for timeouts, time tracking etc. where absolute
     * precision is not needed (i.e. almost always).
     */
    TimePoint epoll_tick_now()
    {
        return m_epoll_tick_now;
    }

    /**
     * Add a file descriptor to the epoll instance of the worker.
     *
     * @param fd      The file descriptor to be added.
     * @param events  Mask of epoll event types.
     * @param pData   The poll data associated with the descriptor:
     *
     *                 data->handler  : Handler that knows how to deal with events
     *                                  for this particular type of 'struct mxs_poll_data'.
     *                 data->thread.id: Will be updated by the worker.
     *
     * @attention The provided file descriptor must be non-blocking.
     * @attention @c pData must remain valid until the file descriptor is
     *            removed from the worker.
     *
     * @return True, if the descriptor could be added, false otherwise.
     */
    bool add_fd(int fd, uint32_t events, MXB_POLL_DATA* pData);

    /**
     * Remove a file descriptor from the worker's epoll instance.
     *
     * @param fd  The file descriptor to be removed.
     *
     * @return True on success, false on failure.
     */
    bool remove_fd(int fd);

    /**
     * Main function of worker.
     *
     * The worker will run the poll loop, until it is told to shut down.
     *
     * @attention  This function will run in the calling thread.
     */
    void run()
    {
        run(nullptr);
    }

    /**
     * Run worker in separate thread.
     *
     * This function will start a new thread, in which the `run`
     * function will be executed.
     *
     * @return True if the thread could be started, false otherwise.
     */
    virtual bool start();

    /**
     * Waits for the worker to finish.
     */
    void join();

    /**
     * Initate shutdown of worker.
     *
     * @attention A call to this function will only initiate the shutdowm,
     *            the worker will not have shut down when the function returns.
     *
     * @attention This function is signal safe.
     */
    void shutdown();

    /**
     * Query whether worker should shutdown.
     *
     * @return True, if the worker should shut down, false otherwise.
     */
    bool should_shutdown() const
    {
        return m_should_shutdown;
    }

    /**
     * Executes a task on the worker thread.
     *
     * @param pTask  The task to be executed.
     * @param pSem   If non-NULL, will be posted once the task's `execute` return.
     * @param mode   Execution mode
     *
     * @return True if the task could be posted to the worker (i.e. not executed yet),
     *          false otherwise.
     *
     * @attention  The instance must remain valid for as long as it takes for the
     *             task to be transferred to the worker and its `execute` function
     *             to be called.
     *
     * The semaphore can be used for waiting for the task to be finished.
     *
     * @code
     *     mxb::Semaphore sem;
     *     MyTask task;
     *
     *     pWorker->execute(&task, &sem);
     *     sem.wait();
     *
     *     MyResult& result = task.result();
     * @endcode
     */
    bool execute(Task* pTask, mxb::Semaphore* pSem, enum execute_mode_t mode);

    bool execute(Task* pTask, enum execute_mode_t mode)
    {
        return execute(pTask, NULL, mode);
    }

    /**
     * Executes a task on the worker thread.
     *
     * @param pTask  The task to be executed.
     * @param mode   Execution mode
     *
     * @return True if the task could be posted (i.e. not executed yet), false otherwise.
     *
     * @attention  Once the task has been executed, it will be deleted.
     */
    bool execute(std::unique_ptr<DisposableTask> sTask, enum execute_mode_t mode);

    /**
     * Execute a function on the worker thread.
     *
     * @param func The function to call
     * @param pSem If non-NULL, will be posted once the task's `execute` return.
     * @param mode Execution mode
     *
     * @return True, if task was posted to the worker
     */
    bool execute(const std::function<void ()>& func, mxb::Semaphore* pSem, enum execute_mode_t mode);

    bool execute(const std::function<void ()>& func, enum execute_mode_t mode)
    {
        return execute(func, NULL, mode);
    }

    /**
     * Executes a task on the worker thread and returns only when the task
     * has finished.
     *
     * @param task   The task to be executed.
     * @param mode   Execution mode
     *
     * @return True if the task was executed on the worker.
     */
    bool call(Task& task, enum execute_mode_t mode);

    /**
     * Executes function on worker thread and returns only when the function
     * has finished.
     *
     * @param func Function to execute
     * @param mode Execution mode
     *
     * @return True if function was executed on the worker.
     */
    bool call(const std::function<void ()>& func, enum execute_mode_t mode);

    /**
     * Post a message to a worker.
     *
     * @param msg_id  The message id.
     * @param arg1    Message specific first argument.
     * @param arg2    Message specific second argument.
     *
     * @return True if the message could be sent, false otherwise. If the message
     *         posting fails, errno is set appropriately.
     *
     * @attention The return value tells *only* whether the message could be sent,
     *            *not* that it has reached the worker.
     *
     * @attention This function is signal safe.
     */
    bool post_message(uint32_t msg_id, intptr_t arg1, intptr_t arg2);

    /**
     * Return the worker associated with the current thread.
     *
     * @return The worker instance, or NULL if the current thread does not have a worker.
     */
    static Worker* get_current();

    /**
     * Push a function for delayed execution.
     *
     * @param delay      The delay in milliseconds.
     * @param pFunction  The function to call.
     *
     * @return A unique identifier for the delayed call. Using that identifier
     *         the call can be cancelled.
     *
     * @attention When invoked, if @c action is @c Worker::Call::EXECUTE, the
     *            function should perform the delayed call and return @true, if
     *            the function should be called again. If the function returns
     *            @c false, it will not be called again.
     *
     *            If @c action is @c Worker::Call::CANCEL, then the function
     *            should perform whatever canceling actions are needed. In that
     *            case the return value is ignored and the function will not
     *            be called again.
     */
    CallIdT delayed_call(int32_t delay, bool (* pFunction)(Worker::Call::action_t action))
    {
        return add_delayed_call(new DelayedCallFunctionVoid(delay, next_delayed_call_id(), pFunction));
    }

    CallIdT delayed_call(const std::chrono::milliseconds& delay,
                         bool (* pFunction)(Worker::Call::action_t action))
    {
        int32_t ms = delay.count();
        return delayed_call(ms, pFunction);
    }

    /**
     * Push a function for delayed execution.
     *
     * @param delay      The delay in milliseconds.
     * @param pFunction  The function to call.
     * @param data       The data to be provided to the function when invoked.
     *
     * @return A unique identifier for the delayed call. Using that identifier
     *         the call can be cancelled.
     *
     * @attention When invoked, if @c action is @c Worker::Call::EXECUTE, the
     *            function should perform the delayed call and return @true, if
     *            the function should be called again. If the function returns
     *            @c false, it will not be called again.
     *
     *            If @c action is @c Worker::Call::CANCEL, then the function
     *            should perform whatever canceling actions are needed. In that
     *            case the return value is ignored and the function will not
     *            be called again.
     */
    template<class D>
    CallIdT delayed_call(int32_t delay,
                         bool (* pFunction)(Worker::Call::action_t action, D data),
                         D data)
    {
        return add_delayed_call(new DelayedCallFunction<D>(delay, next_delayed_call_id(), pFunction, data));
    }

    template<class D>
    CallIdT delayed_call(const std::chrono::milliseconds& delay,
                         bool (* pFunction)(Worker::Call::action_t action, D data),
                         D data)
    {
        int32_t ms = delay.count();
        return delayed_call(ms, pFunction, data);
    }

    /**
     * Push a member function for delayed execution.
     *
     * @param delay    The delay in milliseconds.
     * @param pMethod  The member function to call.
     *
     * @return A unique identifier for the delayed call. Using that identifier
     *         the call can be cancelled.
     *
     * @attention When invoked, if @c action is @c Worker::Call::EXECUTE, the
     *            function should perform the delayed call and return @true, if
     *            the function should be called again. If the function returns
     *            @c false, it will not be called again.
     *
     *            If @c action is @c Worker::Call::CANCEL, then the function
     *            should perform whatever canceling actions are needed. In that
     *            case the return value is ignored and the function will not
     *            be called again.
     */
    template<class T>
    CallIdT delayed_call(int32_t delay,
                         bool (T::* pMethod)(Worker::Call::action_t action),
                         T* pT)
    {
        return add_delayed_call(new DelayedCallMethodVoid<T>(delay, next_delayed_call_id(), pMethod, pT));
    }

    template<class T>
    CallIdT delayed_call(const std::chrono::milliseconds& delay,
                         bool (T::* pMethod)(Worker::Call::action_t action),
                         T* pT)
    {
        int32_t ms = delay.count();
        return delayed_call(ms, pMethod, pT);
    }

    /**
     * Push a member function for delayed execution.
     *
     * @param delay    The delay in milliseconds.
     * @param pMethod  The member function to call.
     * @param data     The data to be provided to the function when invoked.
     *
     * @return A unique identifier for the delayed call. Using that identifier
     *         the call can be cancelled.
     *
     * @attention When invoked, if @c action is @c Worker::Call::EXECUTE, the
     *            function should perform the delayed call and return @true, if
     *            the function should be called again. If the function returns
     *            @c false, it will not be called again.
     *
     *            If @c action is @c Worker::Call::CANCEL, then the function
     *            should perform whatever canceling actions are needed. In that
     *            case the return value is ignored and the function will not
     *            be called again.
     */
    template<class T, class D>
    CallIdT delayed_call(int32_t delay,
                         bool (T::* pMethod)(Worker::Call::action_t action, D data),
                         T* pT,
                         D data)
    {
        return add_delayed_call(new DelayedCallMethod<T, D>(delay,
                                                            next_delayed_call_id(),
                                                            pMethod,
                                                            pT,
                                                            data));
    }

    template<class T, class D>
    CallIdT delayed_call(const std::chrono::milliseconds& delay,
                         bool (T::* pMethod)(Worker::Call::action_t action, D data),
                         T* pT,
                         D data)
    {
        int32_t ms = delay.count();
        return delayed_call(ms, pMethod, pT, data);
    }

    /**
     * Push a general-purpose function wrapper for delayed execution.
     *
     * @param delay    The delay in milliseconds.
     * @param f        The function wrapper.
     *
     * @return A unique identifier for the delayed call. Using that identifier
     *         the call can be cancelled.
     *
     * @attention When invoked, if @c action is @c Worker::Call::EXECUTE, the
     *            function should perform the delayed call and return @true, if
     *            the function should be called again. If the function returns
     *            @c false, it will not be called again.
     *
     *            If @c action is @c Worker::Call::CANCEL, then the function
     *            should perform whatever canceling actions are needed. In that
     *            case the return value is ignored and the function will not
     *            be called again.
     */
    CallIdT delayed_call(int32_t delay,
                         std::function<bool(Worker::Call::action_t action)>&& f)
    {
        return add_delayed_call(new DelayedCallFunctor(delay, next_delayed_call_id(), std::move(f)));
    }

    CallIdT delayed_call(const std::chrono::milliseconds& delay,
                         std::function<bool(Worker::Call::action_t action)>&& f)
    {
        return add_delayed_call(new DelayedCallFunctor(delay.count(), next_delayed_call_id(), std::move(f)));
    }

    /**
     * Cancel delayed call.
     *
     * When this function is called, the delayed call in question will be called
     * *synchronously* with the @c action argument being @c Worker::Call::CANCEL.
     * That is, when this function returns, the function has been canceled.
     *
     * @param id  The id that was returned when the delayed call was scheduled.
     *
     * @return True, if the id represented an existing delayed call.
     */
    bool cancel_delayed_call(CallIdT id);

protected:
    const int m_epoll_fd;               /*< The epoll file descriptor. */
    state_t   m_state;                  /*< The state of the worker */

    static void inc_ref(WorkerDisposableTask* pTask)
    {
        pTask->inc_ref();
    }

    static void dec_ref(WorkerDisposableTask* pTask)
    {
        pTask->dec_ref();
    }

    bool post_disposable(DisposableTask* pTask, enum execute_mode_t mode);

    /**
     * Called by Worker::run() before starting the epoll loop.
     *
     * Default implementation returns True.
     *
     * @return True, if the epoll loop should be started, false otherwise.
     */
    virtual bool pre_run();

    /**
     * Called by Worker::run() after the epoll loop has finished.
     *
     * Default implementation does nothing.
     */
    virtual void post_run();

    /**
     * Called by Worker::run() once per epoll loop.
     *
     * Default implementation calls @c epoll_tick().
     */
    virtual void call_epoll_tick();

    /**
     * Called by Worker::run() once per epoll loop.
     *
     * Default implementation does nothing.
     */
    virtual void epoll_tick();

    /**
     * Helper for resolving epoll-errors. In case of fatal ones, SIGABRT
     * will be raised.
     *
     * @param fd      The epoll file descriptor.
     * @param errnum  The errno of the operation.
     * @param op      Either EPOLL_CTL_ADD or EPOLL_CTL_DEL.
     */
    static void resolve_poll_error(int fd, int err, int op);

private:
    friend class Initer;
    static bool init();
    static void finish();

private:
    class DelayedCall;
    friend class DelayedCall;

    CallIdT next_delayed_call_id()
    {
        // Called in single-thread context.
        return m_next_delayed_call_id++;
    }

    class DelayedCall
    {
        DelayedCall(const DelayedCall&) = delete;
        DelayedCall& operator=(const DelayedCall&) = delete;

    public:
        virtual ~DelayedCall()
        {
        }

        int32_t delay() const
        {
            return m_delay;
        }

        CallIdT id() const
        {
            return m_id;
        }

        int64_t at() const
        {
            return m_at;
        }

        bool call(Worker::Call::action_t action)
        {
            bool rv = do_call(action);
            // We try to invoke the function as often as it was specified. If the
            // delay is very short and the execution time for the function very long,
            // then we will not succeed with that and the function will simply be
            // invoked as frequently as possible.
            int64_t now = WorkerLoad::get_time_ms(mxb::Clock::now());
            int64_t then = m_at + m_delay;

            if (now > then)
            {
                m_at = now;
            }
            else
            {
                m_at = then;
            }
            return rv;
        }

    protected:
        DelayedCall(int32_t delay, CallIdT id)
            : m_id(id)
            , m_delay(delay >= 0 ? delay : 0)
            , m_at(get_at(delay, mxb::Clock::now()))
        {
            mxb_assert(delay >= 0);
        }

        virtual bool do_call(Worker::Call::action_t action) = 0;

    private:
        static int64_t get_at(int32_t delay, mxb::TimePoint tp)
        {
            mxb_assert(delay >= 0);

            int64_t now = WorkerLoad::get_time_ms(tp);

            return now + delay;
        }

    private:
        CallIdT m_id;       // The id of the delayed call.
        int32_t m_delay;    // The delay in milliseconds.
        int64_t m_at;       // The next time the function should be invoked.
    };

    template<class D>
    class DelayedCallFunction : public DelayedCall
    {
        DelayedCallFunction(const DelayedCallFunction&) = delete;
        DelayedCallFunction& operator=(const DelayedCallFunction&) = delete;

    public:
        DelayedCallFunction(int32_t delay,
                            CallIdT id,
                            bool (*pFunction)(Worker::Call::action_t action, D data),
                            D data)
            : DelayedCall(delay, id)
            , m_pFunction(pFunction)
            , m_data(data)
        {
        }

    private:
        bool do_call(Worker::Call::action_t action) override final
        {
            return m_pFunction(action, m_data);
        }

    private:
        bool (* m_pFunction)(Worker::Call::action_t, D);
        D m_data;
    };

    // Explicit specialization requires namespace scope
    class DelayedCallFunctionVoid : public DelayedCall
    {
        DelayedCallFunctionVoid(const DelayedCallFunctionVoid&) = delete;
        DelayedCallFunctionVoid& operator=(const DelayedCallFunctionVoid&) = delete;

    public:
        DelayedCallFunctionVoid(int32_t delay,
                                CallIdT id,
                                bool (*pFunction)(Worker::Call::action_t action))
            : DelayedCall(delay, id)
            , m_pFunction(pFunction)
        {
        }

    private:
        bool do_call(Worker::Call::action_t action) override
        {
            return m_pFunction(action);
        }

    private:
        bool (* m_pFunction)(Worker::Call::action_t action);
    };

    template<class T, class D>
    class DelayedCallMethod : public DelayedCall
    {
        DelayedCallMethod(const DelayedCallMethod&) = delete;
        DelayedCallMethod& operator=(const DelayedCallMethod&) = delete;

    public:
        DelayedCallMethod(int32_t delay,
                          CallIdT id,
                          bool (T::* pMethod)(Worker::Call::action_t action, D data),
                          T* pT,
                          D data)
            : DelayedCall(delay, id)
            , m_pMethod(pMethod)
            , m_pT(pT)
            , m_data(data)
        {
        }

    private:
        bool do_call(Worker::Call::action_t action) override final
        {
            return (m_pT->*m_pMethod)(action, m_data);
        }

    private:
        bool (T::* m_pMethod)(Worker::Call::action_t, D);
        T* m_pT;
        D  m_data;
    };

    template<class T>
    class DelayedCallMethodVoid : public DelayedCall
    {
        DelayedCallMethodVoid(const DelayedCallMethodVoid&) = delete;
        DelayedCallMethodVoid& operator=(const DelayedCallMethodVoid&) = delete;

    public:
        DelayedCallMethodVoid(int32_t delay,
                              CallIdT id,
                              bool (T::* pMethod)(Worker::Call::action_t),
                              T* pT)
            : DelayedCall(delay, id)
            , m_pMethod(pMethod)
            , m_pT(pT)
        {
        }

    private:
        bool do_call(Worker::Call::action_t action) override final
        {
            return (m_pT->*m_pMethod)(action);
        }

    private:
        bool (T::* m_pMethod)(Worker::Call::action_t);
        T* m_pT;
    };

    class DelayedCallFunctor : public DelayedCall
    {
        DelayedCallFunctor(const DelayedCallFunctor&) = delete;
        DelayedCallFunctor& operator=(const DelayedCallFunctor&) = delete;

    public:
        DelayedCallFunctor(int32_t delay,
                           CallIdT id,
                           std::function<bool(Worker::Call::action_t)>&& f)
            : DelayedCall(delay, id)
            , m_f(std::move(f))
        {
        }

    private:
        bool do_call(Worker::Call::action_t action) override
        {
            return m_f(action);
        }

    private:
        std::function<bool(Worker::Call::action_t)> m_f;
    };

    CallIdT add_delayed_call(DelayedCall* pDelayed_call);
    void    adjust_timer();

    void handle_message(MessageQueue& queue, const MessageQueue::Message& msg) override;

    static void thread_main(Worker* pThis, mxb::Semaphore* pSem);

    void poll_waitevents();

    void tick();
private:
    class LaterAt : public std::binary_function<const DelayedCall*, const DelayedCall*, bool>
    {
    public:
        bool operator()(const DelayedCall* pLhs, const DelayedCall* pRhs)
        {
            return pLhs->at() > pRhs->at();
        }
    };

    void run(mxb::Semaphore* pSem);

    typedef DelegatingTimer<Worker>                   PrivateTimer;
    typedef std::multimap<int64_t, DelayedCall*>      DelayedCallsByTime;
    typedef std::unordered_map<CallIdT, DelayedCall*> DelayedCallsById;

    uint32_t           m_max_events;            /*< Maximum numer of events in each epoll_wait call. */
    STATISTICS         m_statistics;            /*< Worker statistics. */
    MessageQueue*      m_pQueue;                /*< The message queue of the worker. */
    std::thread        m_thread;                /*< The thread object of the worker. */
    bool               m_started;               /*< Whether the thread has been started or not. */
    bool               m_should_shutdown;       /*< Whether shutdown should be performed. */
    bool               m_shutdown_initiated;    /*< Whether shutdown has been initated. */
    uint32_t           m_nCurrent_descriptors;  /*< Current number of descriptors. */
    uint64_t           m_nTotal_descriptors;    /*< Total number of descriptors. */
    Load               m_load;                  /*< The worker load. */
    PrivateTimer*      m_pTimer;                /*< The worker's own timer. */
    DelayedCallsByTime m_sorted_calls;          /*< Current delayed calls sorted by time. */
    DelayedCallsById   m_calls;                 /*< Current delayed calls indexed by id. */
    RandomEngine       m_random_engine;         /*< Random engine for this worker (this thread). */
    TimePoint          m_epoll_tick_now;        /*< TimePoint when epoll_tick() was called */

    CallIdT m_next_delayed_call_id {1};     /*< The next delayed call id. */
};
}
