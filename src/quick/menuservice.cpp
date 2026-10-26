#include "quick/menuservice.h"

namespace echo {

MenuService::MenuService()
    : m_bar(std::make_unique<UiMenu>())
{
}

QVariantList MenuService::describe(const UiMenu& menu, bool application)
{
    QVariantList result;
    auto& callbacks = application ? m_application : m_popup;
    for (const auto& entry : menu.entries) {
        const int token = ++m_token;
        QVariantMap item { { QStringLiteral("text"), entry.text }, { QStringLiteral("token"), token },
            { QStringLiteral("separator"), entry.separator } };
        if (entry.invoke)
            callbacks.insert(token, entry.invoke);
        if (entry.submenu)
            item.insert(QStringLiteral("children"), describe(*entry.submenu, application));
        result << item;
    }
    return result;
}

void MenuService::trigger(int token)
{
    auto invoke = m_popup.value(token, m_application.value(token));
    if (invoke)
        invoke();
}

} // namespace echo
