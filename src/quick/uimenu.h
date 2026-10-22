#pragma once
#include <QString>
#include <functional>
#include <memory>
#include <vector>
#include <type_traits>
namespace echo {
// Data and callbacks only; menus are rendered by Qt Quick or the native OS menu bar.
struct UiMenu {
    struct Entry {
        QString text;
        std::function<void()> invoke;
        std::unique_ptr<UiMenu> submenu;
        bool separator = false;
    };
    std::vector<Entry> entries;
    template<class Object, class Callable> void addAction(const QString& text, Object* object, Callable fn) {
        if constexpr (std::is_member_function_pointer_v<Callable>)
            entries.push_back({text, [object, fn] { std::invoke(fn, object); }, {}, false});
        else
            entries.push_back({text, std::move(fn), {}, false});
    }
    UiMenu* addMenu(const QString& text) {
        auto child = std::make_unique<UiMenu>(); auto* result = child.get();
        entries.push_back({text, {}, std::move(child), false}); return result;
    }
    void addSeparator() { entries.push_back({{}, {}, {}, true}); }
};
}
