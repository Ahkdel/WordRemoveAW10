TEMPLATE = app

QT += qml quick widgets charts sql xml core network

QTPLUGIN += qsqlite

CONFIG += c++11 \
	link_pkgconfig \
	static \
	lang-es_ES \
	lang-en_GB \
	lang-de_DE \
	lang-fr_FR \
	lang-it_IT \

static {
	QT += svg
	QTPLUGIN += qtvirtualkeyboardplugin
}

PKGCONFIG += gstreamer-1.0 Qt5GLib-2.0 Qt5GStreamer-1.0 glib-2.0 gobject-2.0

SOURCES += main.cpp

HEADERS += WordRemove.h \
