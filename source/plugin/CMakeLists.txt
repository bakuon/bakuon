cmake_minimum_required(VERSION 3.20 FATAL_ERROR)

# ============================================================================
# bakuon::plugin 锟斤拷锟斤拷 锟斤拷准 C++ 原锟斤拷锟斤拷锟斤拷锟斤拷锟斤拷锟绞╋拷锟教拷猓拷锟斤拷锟斤拷锟?GUI / Qt锟斤拷锟斤拷
# 注锟斤拷目锟斤拷锟斤拷锟斤拷 bakuon_plugin 锟斤拷为 plugin锟斤拷锟斤拷 core / gui 锟斤拷锟斤拷一锟铰碉拷锟斤拷锟斤拷习锟竭ｏ拷
# 锟斤拷锟斤拷锟斤拷锟斤拷锟接︼拷锟轿?bakuon::plugin锟斤拷原 bakuon::bakuon_plugin 未锟斤拷锟斤拷锟斤拷 CMakeLists 锟斤拷锟矫ｏ拷锟斤拷锟斤拷锟斤拷锟斤拷全锟斤拷锟斤拷
# ============================================================================
project(
    plugin
    VERSION 1.0.0
    LANGUAGES CXX)

bakuon_add_module(
    NAME
    ${PROJECT_NAME}
    PATH
    ${CMAKE_CURRENT_SOURCE_DIR}
    INCLUDE_DIRS
    ${BAKUON_ROOT_DIR}/include)

# 锟斤拷锟斤拷 include/bakuon/plugin/ 锟斤拷锟斤拷头锟侥硷拷锟斤拷锟斤拷锟斤拷莶锟斤拷锟?PUBLIC_INCLUDE_DIRS锟斤拷
# 锟斤拷 plugin 锟斤拷锟剿讹拷锟斤拷墓锟斤拷锟?API 之锟襟，凤拷锟斤拷 source/gui/CMakeLists.txt 锟斤拷写锟斤拷锟斤拷
# 锟铰斤拷 include/bakuon/plugin/ 目录锟斤拷锟斤拷锟斤拷 PUBLIC_INCLUDE_DIRS ${BAKUON_ROOT_DIR}/include锟斤拷

# 位锟斤拷锟睫关达拷锟斤拷锟斤拷锟节革拷 CMakeLists.txt 通锟斤拷 CMAKE_POSITION_INDEPENDENT_CODE 全锟街匡拷锟斤拷锟斤拷
# 锟斤拷锟斤拷锟节此碉拷锟斤拷锟斤拷锟斤拷 锟斤拷锟斤拷 锟矫撅拷态锟斤拷锟斤拷锟秸会被锟斤拷锟接斤拷锟皆癸拷锟斤拷锟斤拷锟斤拷式锟斤拷锟截碉拷锟斤拷锟斤拷时锟斤拷锟侥ｏ拷椤?
