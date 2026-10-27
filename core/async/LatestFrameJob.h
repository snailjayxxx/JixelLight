#pragma once
#include "core/async/LatestJob.h"

// GUI-thread live-frame queue: complete the current frame while replacing ONE
// pending snapshot. Unlike LatestJob, new input does not cancel useful current
// work. The owner must cancel on gesture/context changes and reject old epochs.
template<class Request,class Result>
class LatestFrameJob final {
public:
    using Work=std::function<Result(const Request &,const CancelToken &)>;
    using Deliver=std::function<void(const Request &,Result)>;
    LatestFrameJob(Work work,Deliver deliver):m_work(std::move(work)),m_deliver(std::move(deliver)) {
        m_pool.setMaxThreadCount(1);
        QObject::connect(&m_watcher,&QFutureWatcher<Result>::finished,&m_context,[this] {
            if(!m_active)return;
            const bool valid=m_active->epoch==m_epoch&&!cancelled(m_cancel);
            auto active=std::move(m_active);m_active.reset();m_running=false;
            // Move the result out of QFuture's store even when cancelled: an
            // idle watcher must not retain an invisible brush output lease.
            auto result=m_watcher.future().takeResult();
            if(valid)m_deliver(active->request,std::move(result));
            if(!m_running&&m_pending)start();
        });
    }
    ~LatestFrameJob(){QObject::disconnect(&m_watcher,nullptr,&m_context,nullptr);cancel();m_watcher.waitForFinished();m_pool.waitForDone();}
    LatestFrameJob(const LatestFrameJob &)=delete;
    LatestFrameJob &operator=(const LatestFrameJob &)=delete;
    void submit(Request request){m_pending=Pending{m_epoch,std::move(request)};if(!m_running)start();}
    void cancel(){++m_epoch;m_pending.reset();if(m_cancel)m_cancel->store(true,std::memory_order_relaxed);}
    bool busy()const{return m_running||m_pending.has_value();}
private:
    struct Pending{quint64 epoch;Request request;};
    void start(){
        m_active=std::move(m_pending);m_pending.reset();m_cancel=std::make_shared<std::atomic_bool>(false);m_running=true;
        const auto request=m_active->request;const auto token=m_cancel;const auto work=m_work;
        m_watcher.setFuture(QtConcurrent::run(&m_pool,[request,token,work]{try{return work(request,token);}catch(...){return Result{};}}));
    }
    QObject m_context;QThreadPool m_pool;QFutureWatcher<Result> m_watcher;
    Work m_work;Deliver m_deliver;std::optional<Pending> m_active,m_pending;
    CancelToken m_cancel;quint64 m_epoch=0;bool m_running=false;
};
