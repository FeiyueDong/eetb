#pragma once

#include <QFutureWatcher>
#include <QString>
#include <QtConcurrentRun>

#include <exception>
#include <memory>
#include <type_traits>
#include <utility>

namespace gmtools::gui {

template<class T>
struct TaskResult {
    // QFuture copies its result. Keep large sample arrays behind shared ownership.
    std::shared_ptr<T> value;
    QString error;
};

// Work owns its inputs and must not access widgets. Completion runs on the
// context's thread and is disconnected automatically when that context dies.
template<class Work, class Done>
void run_background(QObject* context, Work work, Done done) {
    using Result = TaskResult<std::invoke_result_t<Work>>;
    auto* watcher = new QFutureWatcher<Result>(context);
    QObject::connect(watcher, &QFutureWatcher<Result>::finished, context,
        [watcher, done = std::move(done)]() mutable {
            const auto result = watcher->result();
            watcher->deleteLater();
            done(result);
        });
    watcher->setFuture(QtConcurrent::run([work = std::move(work)]() mutable -> Result {
        try {
            return {std::make_shared<std::invoke_result_t<Work>>(work()), {}};
        } catch (const std::exception& problem) {
            return {{}, QString::fromUtf8(problem.what())};
        } catch (...) {
            return {{}, QStringLiteral("The operation failed unexpectedly.")};
        }
    }));
}

} // namespace gmtools::gui
