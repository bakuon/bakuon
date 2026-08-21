#pragma once

#include <QtCore/QString>

namespace bakuon::gui {

class ActionListener
{
public:
    virtual ~ActionListener() = default;

    virtual int handle(const QString& message) = 0;

    virtual int handleBatch(const std::vector<QString>& messages)
    {
        Q_UNUSED(messages)
        return -1;
    }
};

} // namespace bakuon::gui
