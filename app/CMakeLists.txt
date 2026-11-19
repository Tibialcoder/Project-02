cmake_minimum_required(VERSION 3.22.1)
project("sa2b_engine")

# Adiciona sua biblioteca compartilhada (.so)
add_library(sa2b_engine SHARED
        native-lib.cpp
)

# Encontra e vincula as bibliotecas nativas do Android (OpenGL ES, EGL, Android, Log)
find_library(log-lib log)
find_library(gles-lib GLESv3)
find_library(egl-lib EGL)
find_library(android-lib android)

target_link_libraries(sa2b_engine
        ${log-lib}
        ${gles-lib}
        ${egl-lib}
        ${android-lib}
        # Se você estiver usando a GameActivity do AGDK, adicione a lib de glue aqui se necessário
)