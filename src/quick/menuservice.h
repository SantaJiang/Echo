#pragma once

#include "quick/uimenu.h"

#include <QHash>
#include <QVariantList>
#include <functional>
#include <memory>

namespace echo {

class MenuService {
public:
    MenuService();
    UiMenu& bar() { return *m_bar; }
    QVariantList describe(const UiMenu& menu, bool application = false);
    void trigger(int token);
    void beginPopup() { m_popup.clear(); }

private:
    std::unique_ptr<UiMenu> m_bar;
    int m_token = 0;
    QHash<int, std::function<void()>> m_popup;
    QHash<int, std::function<void()>> m_application;
};

} // namespace echo
