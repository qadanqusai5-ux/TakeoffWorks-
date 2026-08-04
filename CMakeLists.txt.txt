cmake_minimum_required(VERSION 3.20)
project(TakeoffWorks_Native LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# Find Qt6
find_package(Qt6 REQUIRED COMPONENTS Core Widgets Gui)

# إعداد PDFium (لن نحتاج لتعقيدات، سنقوم بتحميله مع المشروع)
set(PDFium_DIR "" CACHE PATH "Path to PDFium binaries")
if(NOT PDFium_DIR)
    message(FATAL_ERROR "PDFium_DIR not set. Please set to the folder containing pdfium.dll and include/")
endif()

find_library(PDFium_LIBRARY NAMES pdfium PATHS ${PDFium_DIR} NO_DEFAULT_PATH)
find_path(PDFium_INCLUDE_DIR NAMES fpdfview.h PATHS ${PDFium_DIR}/include NO_DEFAULT_PATH)

if(NOT PDFium_LIBRARY OR NOT PDFium_INCLUDE_DIR)
    message(FATAL_ERROR "PDFium library or headers not found in ${PDFium_DIR}")
endif()

# إنشاء الملف التنفيذي
add_executable(TakeoffWorks_Native main.cpp)

# ربط المكتبات
target_link_libraries(TakeoffWorks_Native
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    ${PDFium_LIBRARY}
)

# تضمين المجلدات
target_include_directories(TakeoffWorks_Native PRIVATE ${PDFium_INCLUDE_DIR})

# نسخ ملف PDFium.dll إلى مجلد النتيجة (مهم جداً لكي يعمل البرنامج)
if(WIN32)
    add_custom_command(TARGET TakeoffWorks_Native POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${PDFium_DIR}/pdfium.dll"
        $<TARGET_FILE_DIR:TakeoffWorks_Native>
    )
endif()