#pragma once
#include <filesystem>
#include <string>

bool temTradutor(const std::filesystem::path& caminho);
bool aplicarTraducao(
        const std::filesystem::path& caminhoTemp,
        std::filesystem::path& caminhoProcessado);
bool processarRecurso(
        const std::filesystem::path& caminhoOriginal,
        const std::filesystem::path& pastaDefinitiva);
bool extensaoPermitida(const std::filesystem::path& caminho);
void processarPastaRecursos(
        const std::filesystem::path& pastaOrigem,
        const std::filesystem::path& pastaDefinitiva);
void organizarBinariosParaTraducao(const std::string& baseDirPath);
void processOnlySupportedFiles(const std::string& baseDirPath);
bool modelFileExists(const std::string& path);
void processModelsWithChecks(const std::string& baseDirPath);
