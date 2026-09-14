#pragma once
#include <QString>
class SystemPowerService
{
public:
    virtual ~SystemPowerService()=default;
    virtual bool RequestPowerOff(QString& error);
};
