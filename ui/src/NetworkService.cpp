#include "NetworkService.h"

#include <QProcess>

NetworkResult NmcliNetworkBackend::Run(const QStringList& args,QString& output,int timeoutMs)
{
    QProcess p;p.setProgram("/usr/bin/nmcli");p.setArguments(args);p.start();
    if(!p.waitForStarted(2000))return {false,"NetworkManager tool could not be started"};
    if(!p.waitForFinished(timeoutMs)){p.kill();p.waitForFinished();return {false,"NetworkManager request timed out"};}
    output=QString::fromUtf8(p.readAllStandardOutput());const QString error=QString::fromUtf8(p.readAllStandardError()).trimmed();
    return {p.exitStatus()==QProcess::NormalExit&&p.exitCode()==0,error.isEmpty()?output.trimmed():error};
}
NetworkResult NetworkService::Run(const QStringList& args,QString* output,int timeoutMs)
{QString text;auto r=backend_.Run(args,text,timeoutMs);if(output)*output=text;return r;}
bool NetworkService::IsUserNetworkAdapter(const QString& d,const QString& t,const QString& ethercat)
{
    if(d.isEmpty()||d=="lo"||d==ethercat)return false;
    if(t!="ethernet"&&t!="wifi")return false;
    const QString n=d.toLower();return !(n.startsWith("docker")||n.startsWith("br-")||n.startsWith("virbr")||n.startsWith("veth")||n.startsWith("tun")||n.startsWith("tap")||n.startsWith("wg"));
}
bool NetworkService::IsValidIpv4(const QString& value,bool allowEmpty)
{const QString v=value.trimmed();if(allowEmpty&&v.isEmpty())return true;const auto p=v.split('.');if(p.size()!=4)return false;for(const auto& x:p){bool ok=false;const int n=x.toInt(&ok);if(!ok||n<0||n>255||QString::number(n)!=x)return false;}return true;}
QStringList NetworkService::ParseTerseLine(const QString& line,QChar separator)
{
    QStringList fields;QString field;bool escaped=false;
    for(const QChar ch:line){
        if(escaped){field+=ch;escaped=false;}
        else if(ch=='\\')escaped=true;
        else if(ch==separator){fields+=field;field.clear();}
        else field+=ch;
    }
    if(escaped)field+='\\';fields+=field;return fields;
}
QVector<NetworkAdapterState> NetworkService::EnumerateAdapters(NetworkResult& result)
{
    QString out;result=Run({"-t","--escape","yes","-f","DEVICE,TYPE,STATE,CONNECTION","device","status"},&out);QVector<NetworkAdapterState> values;if(!result.ok)return values;
    for(const auto& line:out.split('\n',Qt::SkipEmptyParts)){const auto p=ParseTerseLine(line);if(p.size()<4||!IsUserNetworkAdapter(p[0],p[1],ethercatAdapter_))continue;NetworkAdapterState s;s.device=p[0];s.type=p[1];s.state=p[2];s.connection=p.mid(3).join(":");s.wifi=s.type=="wifi";QString detail;if(Run({"-g","IP4.ADDRESS,IP4.GATEWAY,IP4.DNS","device","show",s.device},&detail).ok){const auto rows=detail.split('\n');if(!rows.isEmpty())s.ipv4=rows[0];if(rows.size()>1)s.gateway=rows[1];if(rows.size()>2)s.dns=rows.mid(2).join(", ");}values.push_back(s);}return values;
}
QVector<WifiAccessPoint> NetworkService::ScanWifi(const QString& device,NetworkResult& result)
{
    QVector<WifiAccessPoint> values;if(device.isEmpty()){result={false,"No Wi-Fi adapter available."};return values;}QString out;result=Run({"-t","--escape","yes","-f","SSID,SIGNAL,SECURITY","device","wifi","list","ifname",device,"--rescan","yes"},&out,15000);if(!result.ok)return values;
    for(const auto& line:out.split('\n',Qt::SkipEmptyParts)){const auto p=ParseTerseLine(line);if(p.size()<3||p[0].isEmpty())continue;const QString security=p.mid(2).join(":");values.push_back({p[0],p[1].toInt(),!security.trimmed().isEmpty()&&security!="--"});}return values;
}
NetworkResult NetworkService::SetWiredDhcp(const QString& d)
{if(!IsUserNetworkAdapter(d,"ethernet",ethercatAdapter_))return {false,"Protected or invalid wired adapter"};QString c;auto r=Run({"-g","GENERAL.CONNECTION","device","show",d},&c);if(!r.ok||c.trimmed().isEmpty()||c.trimmed()=="--")return {false,"No NetworkManager connection profile for adapter"};c=c.trimmed();QString old;Run({"-g","ipv4.method,ipv4.addresses,ipv4.gateway,ipv4.dns","connection","show",c},&old);r=Run({"connection","modify",c,"ipv4.method","auto","ipv4.addresses","","ipv4.gateway","","ipv4.dns",""});if(!r.ok)return r;auto up=Run({"connection","up",c});if(!up.ok){const auto v=old.split('\n');if(v.size()>=4){Run({"connection","modify",c,"ipv4.method",v[0],"ipv4.addresses",v[1],"ipv4.gateway",v[2],"ipv4.dns",v.mid(3).join(",")});Run({"connection","up",c});}return {false,"Apply failed; previous profile restoration was attempted: "+up.message};}return up;}
NetworkResult NetworkService::SetWiredStatic(const QString& d,const QString& a,int prefix,const QString& g,const QString& dns)
{if(!IsUserNetworkAdapter(d,"ethernet",ethercatAdapter_))return {false,"Protected or invalid wired adapter"};if(!IsValidIpv4(a)||!IsValidPrefix(prefix)||!IsValidIpv4(g,true))return {false,"Invalid IPv4 address, prefix, or gateway"};for(const auto& v:dns.split(',',Qt::SkipEmptyParts))if(!IsValidIpv4(v.trimmed()))return {false,"Invalid DNS address"};QString c;auto r=Run({"-g","GENERAL.CONNECTION","device","show",d},&c);if(!r.ok||c.trimmed().isEmpty()||c.trimmed()=="--")return {false,"No NetworkManager connection profile for adapter"};c=c.trimmed();QString old;Run({"-g","ipv4.method,ipv4.addresses,ipv4.gateway,ipv4.dns","connection","show",c},&old);r=Run({"connection","modify",c,"ipv4.method","manual","ipv4.addresses",a+"/"+QString::number(prefix),"ipv4.gateway",g,"ipv4.dns",dns});if(!r.ok)return r;auto up=Run({"connection","up",c});if(!up.ok){const auto v=old.split('\n');if(v.size()>=4){Run({"connection","modify",c,"ipv4.method",v[0],"ipv4.addresses",v[1],"ipv4.gateway",v[2],"ipv4.dns",v.mid(3).join(",")});Run({"connection","up",c});}return {false,"Apply failed; previous profile restoration was attempted: "+up.message};}return up;}
NetworkResult NetworkService::SetWifiEnabled(bool e){return Run({"radio","wifi",e?"on":"off"});}
NetworkResult NetworkService::ConnectWifi(const QString& d,const QString& ssid,const QString& password)
{if(!IsUserNetworkAdapter(d,"wifi",ethercatAdapter_)||ssid.isEmpty())return {false,"Invalid Wi-Fi adapter or SSID"};QStringList a{"device","wifi","connect",ssid,"ifname",d};if(!password.isEmpty())a<<"password"<<password;return Run(a,nullptr,20000);}
NetworkResult NetworkService::DisconnectWifi(const QString& d)
{if(!IsUserNetworkAdapter(d,"wifi",ethercatAdapter_))return {false,"Invalid Wi-Fi adapter"};return Run({"device","disconnect",d});}
