#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

struct NetworkAdapterState
{
    QString device,type,state,connection,ipv4,gateway,dns;
    bool wifi=false;
};
struct WifiAccessPoint { QString ssid;int signal=0;bool secured=false; };
struct NetworkResult { bool ok=false;QString message; };

class NetworkBackend
{
public:
    virtual ~NetworkBackend()=default;
    virtual NetworkResult Run(const QStringList& arguments,QString& output,int timeoutMs)=0;
};

class NmcliNetworkBackend final : public NetworkBackend
{
public:
    NetworkResult Run(const QStringList& arguments,QString& output,int timeoutMs) override;
};

class NetworkService
{
public:
    explicit NetworkService(NetworkBackend& backend):backend_(backend){}
    void SetEthercatAdapter(const QString& name){ethercatAdapter_=name.trimmed();}
    QVector<NetworkAdapterState> EnumerateAdapters(NetworkResult& result);
    QVector<WifiAccessPoint> ScanWifi(const QString& device,NetworkResult& result);
    NetworkResult SetWiredDhcp(const QString& device);
    NetworkResult SetWiredStatic(const QString& device,const QString& address,int prefix,const QString& gateway,const QString& dns);
    NetworkResult SetWifiEnabled(bool enabled);
    NetworkResult ConnectWifi(const QString& device,const QString& ssid,const QString& password);
    NetworkResult DisconnectWifi(const QString& device);
    static bool IsUserNetworkAdapter(const QString& device,const QString& type,const QString& ethercatAdapter);
    static bool IsValidIpv4(const QString& value,bool allowEmpty=false);
    static bool IsValidPrefix(int prefix){return prefix>=0&&prefix<=32;}
    static QStringList ParseTerseLine(const QString& line,QChar separator=':');
private:
    NetworkResult Run(const QStringList& args,QString* output=nullptr,int timeoutMs=10000);
    NetworkBackend& backend_;QString ethercatAdapter_;
};
