#include "quick/selectionmodel.h"

#include <Qt>
#include <algorithm>

namespace echo {

void SelectionModel::remember(const QString& route)
{
    m_stored.insert(route, m_selection);
}

void SelectionModel::restore(const QString& route)
{
    m_selection = m_stored.value(route);
    m_anchor = -1;
    bump();
}

bool SelectionModel::selectRow(int row, int count, int modifiers, const std::function<QString(int)>& keyAt)
{
    if (row < 0 || row >= count)
        return false;
    if (modifiers & Qt::ShiftModifier) {
        if (m_anchor < 0)
            m_anchor = row;
        if (!(modifiers & (Qt::ControlModifier | Qt::MetaModifier)))
            m_selection.clear();
        for (int i = std::min(row, m_anchor); i <= std::max(row, m_anchor); ++i)
            m_selection.insert(keyAt(i));
    } else if (modifiers & (Qt::ControlModifier | Qt::MetaModifier)) {
        const auto key = keyAt(row);
        if (m_selection.contains(key))
            m_selection.remove(key);
        else
            m_selection.insert(key);
        m_anchor = row;
    } else {
        m_selection = { keyAt(row) };
        m_anchor = row;
    }
    bump();
    return true;
}

void SelectionModel::selectAll(int count, const std::function<QString(int)>& keyAt)
{
    for (int i = 0; i < count; ++i)
        m_selection.insert(keyAt(i));
    bump();
}

void SelectionModel::clear()
{
    m_selection.clear();
    m_anchor = -1;
    bump();
}

void SelectionModel::retain(int count, const std::function<QString(int)>& keyAt)
{
    QSet<QString> kept;
    for (int i = 0; !m_selection.isEmpty() && i < count; ++i)
        if (m_selection.contains(keyAt(i)))
            kept.insert(keyAt(i));
    m_selection = kept;
    bump();
}

} // namespace echo
