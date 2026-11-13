#pragma once
#include <QObject>
#include <functional>

// Fixed synthetic application's Save action. Observes its document only; it
// deliberately does not commit/cancel preedit or change editor behavior.
class LifecycleActions final : public QObject {
    Q_OBJECT
public:
    explicit LifecycleActions(std::function<void()> save, QObject *parent)
        : QObject(parent), save_(std::move(save)) {}
    Q_INVOKABLE void save() { save_(); }
private:
    std::function<void()> save_;
};
