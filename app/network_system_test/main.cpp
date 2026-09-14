#include "NetworkService.h"
#include "ApplicationShutdownCoordinator.h"
#include <cstdlib>
#include <iostream>

static void require(bool ok,const char* m){if(!ok){std::cerr<<"FAIL: "<<m<<'\n';std::exit(1);}}
class FakeBackend final:public NetworkBackend
{
public:
    NetworkResult Run(const QStringList& a,QString& out,int)override{calls.push_back(a);if(fail)return {false,"NetworkManager unavailable"};out=output;return {true,{}};}
    QVector<QStringList> calls;QString output;bool fail=false;
};
int main()
{
    require(!NetworkService::IsUserNetworkAdapter("lo","ethernet","enp6s0"),"loopback filter");
    require(!NetworkService::IsUserNetworkAdapter("enp6s0","ethernet","enp6s0"),"EtherCAT filter");
    require(!NetworkService::IsUserNetworkAdapter("docker0","ethernet","enp6s0"),"virtual filter");
    require(NetworkService::IsUserNetworkAdapter("enp5s0","ethernet","enp6s0"),"wired allow");
    require(NetworkService::IsValidIpv4("192.168.0.35")&&!NetworkService::IsValidIpv4("999.1.1.1")&&!NetworkService::IsValidIpv4("1.2.3"),"IPv4 validation");
    const auto parsed=NetworkService::ParseTerseLine("wlan0:wifi:connected:Office\\:Lab");require(parsed.size()==4&&parsed[3]=="Office:Lab","nmcli escaped terse parser");
    const auto wifiParsed=NetworkService::ParseTerseLine("Guest\\:5G:87:WPA2");require(wifiParsed.size()==3&&wifiParsed[0]=="Guest:5G"&&wifiParsed[1]=="87","nmcli Wi-Fi parser");
    FakeBackend b;NetworkService n(b);n.SetEthercatAdapter("enp6s0");b.output="Office LAN";require(n.SetWiredStatic("enp5s0","192.168.0.35",24,"192.168.0.1","1.1.1.1,8.8.8.8").ok,"static apply");require(!n.SetWiredStatic("enp5s0","bad",24,"","1.1.1.1").ok,"static reject");require(n.SetWiredDhcp("enp5s0").ok,"dhcp");
    const QString secret="p@ss;$(touch never)";require(n.ConnectWifi("wlan0","Office WiFi",secret).ok,"wifi connect");const auto args=b.calls.back();require(!args.contains("/bin/sh")&&!args.contains("-c")&&args.contains(secret),"argument list no shell");
    b.fail=true;NetworkResult er;require(n.EnumerateAdapters(er).isEmpty()&&!er.ok,"NetworkManager unavailable");
    b.fail=false;b.output="enp6s0:ethernet:connected:EtherCAT\nenp5s0:ethernet:connected:Office\\:LAN\nwlan0:wifi:disconnected:--\n";const int enumerationCall=b.calls.size();const auto enumerated=n.EnumerateAdapters(er);require(er.ok&&enumerated.size()==2&&enumerated[0].device=="enp5s0"&&enumerated[0].connection=="Office:LAN"&&enumerated[1].wifi,"adapter enumeration parser and EtherCAT exclusion");require(!b.calls[enumerationCall].contains("--separator")&&b.calls[enumerationCall].contains("--escape"),"nmcli 1.54 compatible arguments");
    int cleanup=0,power=0;ApplicationActivitySnapshot activity;auto make=[&]{return ApplicationShutdownCoordinator([&]{return activity;},[&](QString&){++cleanup;return true;},[&](QString&){++power;return true;});};QString error;
    {auto c=make();require(c.Request(false,error)&&cleanup==1&&power==0,"app only");}
    cleanup=power=0;{auto c=make();require(c.Request(true,error)&&cleanup==1&&power==1,"cleanup before power");}
    for(int i=0;i<4;++i){cleanup=power=0;activity={};if(i==0)activity.jog=true;if(i==1)activity.motion=true;if(i==2)activity.sequence=true;if(i==3)activity.calibration=true;auto c=make();require(!c.Request(true,error)&&cleanup==0&&power==0,"active reject");}
    activity={};cleanup=power=0;{ApplicationShutdownCoordinator c([&]{return activity;},[&](QString&){++cleanup;return false;},[&](QString&){++power;return true;});require(!c.Request(true,error)&&power==0,"cleanup failure no power");}
    activity={};cleanup=power=0;{auto c=make();require(c.Request(false,error)&&!c.Request(false,error)&&cleanup==1&&power==0,"repeated exit");}
    std::cout<<"Network and orderly shutdown policy tests passed\n";
}
