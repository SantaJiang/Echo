#pragma once

#include <QHash>
#include <QSet>
#include <QString>
#include <functional>

namespace echo {

class SelectionModel {
public:
    [[nodiscard]] int revision() const { return m_revision; }
    [[nodiscard]] int count() const { return m_selection.size(); }
    [[nodiscard]] bool empty() const { return m_selection.isEmpty(); }
    [[nodiscard]] bool contains(const QString& key) const { return m_selection.contains(key); }
    void remember(const QString& route);
    void restore(const QString& route);
    bool selectRow(int row, int count, int modifiers, const std::function<QString(int)>& keyAt);
    void selectAll(int count, const std::function<QString(int)>& keyAt);
    void clear();
    void retain(int count, const std::function<QString(int)>& keyAt);

private:
    void bump() { ++m_revision; }
    QHash<QString, QSet<QString>> m_stored;
    QSet<QString> m_selection;
    int m_revision = 0;
    int m_anchor = -1;
};

} // namespace echo
