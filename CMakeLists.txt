cmake_minimum_required(VERSION 3.22.1)
project("sa2b_recomp")

# Define o padrão C++17 e C11
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_C_STANDARD 11)

# Definições globais para Big-Endian / arquitetura recompilada
add_definitions(-DGEKKO -D_ARCH_PPC)

# Diretórios de inclusão (Headers)
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# Adiciona a biblioteca SDL2 (geralmente integrada via gradle/prefab no Android Studio)
find_package(SDL2 REQUIRED)

# Lista todos os arquivos fonte da camada de plataforma e do jogo recompilado
add_library(
    sa2b_recomp SHARED
    
    # Camada de Plataforma e Stubs (C++ / C)
    src/main.cpp
    src/vfs.cpp
    src/dolphin_os.cpp
    src/gx_backend.cpp
    
    # Arquivos C gerados pelo decomp-toolkit / recompilador do main.dol
    src/recomp/main_dol_code.c
)

# Vincula as bibliotecas nativas do Android e o SDL2
target_link_libraries(
    sa2b_recomp
    PRIVATE
    SDL2::SDL2
    GLESv3
    EGL
    android
    log
    m
)