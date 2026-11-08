// Test-only baseline oracle. Never linked into the Rust application.
#include "document.h"
#include <QApplication>
#include <QDir>
#include <QDebug>
int main(int argc,char**argv){
 QApplication app(argc,argv);QString error;
 if(argc==4&&QString(argv[1])=="--roundtrip"){
  Document d;if(!d.load(argv[2],&error)||!d.save(argv[3],&error)){qCritical()<<error;return 1;}
  return d.composite().save(QString(argv[3])+".png")?0:1;
 }
 if(argc!=3||QString(argv[1])!="--fixtures")return 2;
 QDir().mkpath(argv[2]);Document d;d.create({12,9});
 auto &l=d.state.layers[0];l.name=QString::fromUtf8("Türkçe — α");l.pixels.fill(QColor(40,90,130,210));
 l.mask=QImage(l.pixels.size(),QImage::Format_ARGB32_Premultiplied);l.mask.fill(QColor(180,180,180));
 l.opacity=.73;l.color=QColor(13,29,111,233);l.tones.gamma=1.2;l.filters.noise=4;l.geometry.scale(.8,1.1);l.offset={2,-1};
 if(!d.save(QString(argv[2])+"/qt-v5.pdeck",&error)){qCritical()<<error;return 1;}
 d.setSelection(Selection(1,2,5,4));d.saveSelection(QString::fromUtf8("Alan α"));
 if(!d.save(QString(argv[2])+"/qt-v6.pdeck",&error)){qCritical()<<error;return 1;}
 if(!d.convertToSmartObject(&error)){qCritical()<<error;return 1;}
 QImage replacement(7,5,QImage::Format_ARGB32_Premultiplied);replacement.fill(QColor(230,60,90,175));
 if(!d.replaceSmartObjectContents(replacement,&error)||!d.save(QString(argv[2])+"/qt-v7.pdeck",&error)){qCritical()<<error;return 1;}
 d.composite().save(QString(argv[2])+"/qt-v7.png");
 return 0;
}
