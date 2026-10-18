#pragma once

#include <QIcon>
#include <QSize>
#include <QString>
#include <initializer_list>

inline QIcon applicationIcon() {
    QIcon icon(QStringLiteral(":/photodeck.png"));
    // PNG is supported by Qt itself, without an SVG/ICO image-format plugin.
    // Exact small representations keep the photo-frame gap crisp in title bars.
    for (int size : {16, 24, 32, 48, 64, 128, 256})
        icon.addFile(QStringLiteral(":/icons/photodeck-%1.png").arg(size), QSize(size, size));
    return icon;
}
