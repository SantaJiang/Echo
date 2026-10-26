#include "quicktest_harness.h"

#include <QEventLoop>
#include <QTimer>

namespace echo::quicktest {

QStringList& issues()
{
    static QStringList list;
    return list;
}

void pump(int milliseconds)
{
    QEventLoop loop;
    QTimer::singleShot(milliseconds, &loop, &QEventLoop::quit);
    loop.exec();
}

} // namespace echo::quicktest
