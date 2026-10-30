#pragma once
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QFile>
#include <QFuture>
#include <QImage>
#include <QString>
#include <QThreadPool>
#include <QtConcurrent/QtConcurrentRun>
#include <algorithm>
#include <deque>
#include <vector>

// QA-only: retain each captured frame while a single worker writes lossless
// PNG evidence. The event thread still waits for every write and SHA before
// declaring success, and at most two full-resolution frames can be queued.
class PressureCaptureQueue {
public:
    struct Record {
        QString label,path,sha256;
        int width=0,height=0;
        double dpr=1;
        qint64 saveNs=0;
        bool saved=false;
    };
    static constexpr int Quality=85;
    static constexpr std::size_t MaxPending=2;
    PressureCaptureQueue() { pool_.setMaxThreadCount(1); }
    PressureCaptureQueue(const PressureCaptureQueue&)=delete;
    PressureCaptureQueue& operator=(const PressureCaptureQueue&)=delete;
    ~PressureCaptureQueue() { finish(); }

    bool enqueue(const QString &label,const QString &path,const QImage &frame,double dpr,
                 bool allowAfterFailure=false) {
        if(label.isEmpty()||path.isEmpty()||frame.isNull()||!(dpr>0))return false;
        harvest(false);
        if(failed_&&!allowAfterFailure)return false;
        if(pending_.size()>=MaxPending)harvestOne(true);
        if(failed_&&!allowAfterFailure)return false;
        Record meta;meta.label=label;meta.path=path;meta.width=frame.width();meta.height=frame.height();meta.dpr=dpr;
        auto future=QtConcurrent::run(&pool_,[frame,path]{
            Outcome result;QElapsedTimer timer;timer.start();
            result.saved=frame.save(path,"PNG",Quality);
            result.saveNs=timer.nsecsElapsed();
            if(result.saved){QFile file(path);QCryptographicHash hash(QCryptographicHash::Sha256);
                result.saved=file.open(QIODevice::ReadOnly)&&hash.addData(&file);
                if(result.saved)result.sha256=QString::fromLatin1(hash.result().toHex());}
            return result;
        });
        pending_.push_back({std::move(meta),std::move(future)});
        peakPending_=std::max(peakPending_,pending_.size());return true;
    }
    bool hasFailure() { harvest(false);return failed_; }
    bool finish() { harvest(true);return !failed_; }
    const std::vector<Record> &records() const { return records_; }
    std::size_t peakPending() const { return peakPending_; }
private:
    struct Outcome { bool saved=false; qint64 saveNs=0; QString sha256; };
    struct Pending { Record meta; QFuture<Outcome> future; };
    void harvest(bool wait) {
        while(!pending_.empty()&&(wait||pending_.front().future.isFinished()))harvestOne(wait);
    }
    void harvestOne(bool wait) {
        if(pending_.empty())return;
        auto &item=pending_.front();
        Outcome result;
        try { if(wait)item.future.waitForFinished();result=item.future.result(); } catch (...) { result.saved=false; }
        item.meta.saved=result.saved;item.meta.saveNs=result.saveNs;item.meta.sha256=result.sha256;
        failed_=failed_||!result.saved;records_.push_back(std::move(item.meta));pending_.pop_front();
    }
    QThreadPool pool_;
    std::deque<Pending> pending_;
    std::vector<Record> records_;
    std::size_t peakPending_=0;
    bool failed_=false;
};
