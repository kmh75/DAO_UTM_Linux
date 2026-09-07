#include "TrendGraphWidget.h"

#include <QApplication>
#include <QImage>
#include <QPainter>

#include <cmath>
#include <limits>

int main(int argc, char** argv)
{
    QApplication app(argc,argv);
    TrendGraphWidget graph;graph.resize(800,240);
    for(int i=0;i<100000;++i)graph.append(std::sin(i*.01)*100.0,std::cos(i*.007)*10.0);
    graph.append(std::numeric_limits<double>::quiet_NaN(),0.0);
    graph.append(0.0,std::numeric_limits<double>::infinity());
    graph.setForceUnitLabel("kgf");
    QImage image(graph.size(),QImage::Format_ARGB32_Premultiplied);image.fill(Qt::transparent);
    QPainter painter(&image);graph.render(&painter);return image.isNull()?1:0;
}
