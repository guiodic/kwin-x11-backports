# KI18N Translation Domain for this library.
add_definitions(-DTRANSLATION_DOMAIN=\"kcm_kwin_virtualdesktops_x11\")

########### next target ###############

set(kcm_kwin_virtualdesktops_x11_PART_SRCS
    ../../virtualdesktopsdbustypes.cpp
    animationsmodel.cpp
    desktopsmodel.cpp
    virtualdesktops.cpp
    virtualdesktopsdata.cpp
)

kconfig_add_kcfg_files(kcm_kwin_virtualdesktops_x11_PART_SRCS virtualdesktopssettings.kcfgc GENERATE_MOC)

kcmutils_add_qml_kcm(kcm_kwin_virtualdesktops_x11 SOURCES ${kcm_kwin_virtualdesktops_x11_PART_SRCS})

target_link_libraries(kcm_kwin_virtualdesktops_x11 PRIVATE
    Qt::DBus

    KF6::I18n
    KF6::KCMUtils
    KF6::KCMUtilsQuick
    KF6::XmlGui

    kcmkwincommon
)
