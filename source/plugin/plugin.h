#pragma once

#include <memory>
#include <string>

/// 这是插件原生插件，与 gui::Plugin 不同，没有 GUI 和 Qt 依赖。

namespace bakuon::plugin {

class Plugin
{
public:
    virtual ~Plugin();

    template<class Interface>
    Interface *queryInterface();

    template<class Interface>
    const Interface *queryInterface() const;

    template<class Interface>
    std::shared_ptr<Interface> queryInterfaceSharedPtr();

    template<class Interface>
    std::shared_ptr<const Interface> queryInterfaceSharedPtr() const;

    template<class Interface>
    [[nodiscard]] bool hasInterface() const;

    [[nodiscard]] bool hasInterface(const std::string &interfaceName, bool demangle = true) const;

protected:
    Plugin();

private:
    [[nodiscard]] void *resoleInterface(const std::string &interfaceName) const;

private:
    class Implementation;
    const std::unique_ptr<Implementation> d;
};

} // namespace bakuon::plugin
