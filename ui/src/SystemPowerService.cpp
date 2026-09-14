#include "SystemPowerService.h"
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusMessage>
bool SystemPowerService::RequestPowerOff(QString& error)
{
    QDBusInterface login("org.freedesktop.login1","/org/freedesktop/login1","org.freedesktop.login1.Manager",QDBusConnection::systemBus());
    if(!login.isValid()){error="systemd-logind is unavailable";return false;}
    const QDBusMessage reply=login.call("PowerOff",true);
    if(reply.type()==QDBusMessage::ErrorMessage){error=reply.errorMessage();return false;}return true;
}
