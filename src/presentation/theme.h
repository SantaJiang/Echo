#pragma once
#include <QString>
namespace echo {
void useDarkTheme();
[[nodiscard]] QString formatDuration(qint64 milliseconds);
}
