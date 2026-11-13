#pragma once
#include <QObject>
#include <QPointer>
#include <QQuickItem>
#include <QTimer>
#include <QJsonArray>
#include <array>
#include <chrono>

// Synthetic benchmark only. Observe lengths after queued signals settle; never
// change the editor or acknowledge keys to the producer. No text is exported.
class TimingObserver final : public QObject {
    Q_OBJECT
public:
    explicit TimingObserver(QQuickItem *field, QObject *parent) : QObject(parent), field_(field) {
        connect(field, SIGNAL(textChanged()), this, SLOT(changed()));
        connect(field, SIGNAL(preeditTextChanged()), this, SLOT(changed()));
    }
    QJsonArray times() const { QJsonArray a; for (auto n : times_) a.append(n); return a; }
    bool complete() const { return !invalid_ && last_ == int(times_.size()); }
private Q_SLOTS:
    void changed() {
        if (queued_) return;
        queued_ = true;
        QTimer::singleShot(0, this, [this] {
            queued_ = false;
            if (!field_ || !field_->hasActiveFocus()) return;
            const int length = field_->property("text").toString().size()
                + field_->property("preeditText").toString().size();
            if (length == last_) return;
            if (length != last_ + 1 || length > int(times_.size())) { invalid_ = true; return; }
            times_[size_t(length - 1)] = std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count();
            last_ = length;
        });
    }
private:
    QPointer<QQuickItem> field_;
    std::array<qint64, 60> times_{};
    int last_ = 0;
    bool queued_ = false, invalid_ = false;
};
