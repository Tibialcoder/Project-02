import os
import subprocess
import sys
from pathlib import Path

def extract_sa2b_iso():
    # Caminhos base
    root_dir = Path(__file__).resolve().parent.parent
    iso_path = root_dir / "iso_data" / "SonicAdventure2Battle.iso"
    output_dir = root_dir / "assets" / "files"
    sys_dir = root_dir / "iso_data" / "sys"

    if not iso_path.exists():
        print(f"[Erro] ISO não encontrada em: {iso_path}")
        print("Coloque a sua cópia da ISO USA (GSAE01) na pasta iso_data/ com o nome 'SonicAdventure2Battle.iso'")
        return

    print(f"[1/3] Verificando ferramenta 'wit' (Wiimms ISO Tools)...")
    try:
        subprocess.run(["wit", "--version"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=True)
    except (subprocess.CalledProcessError, FileNotFoundError):
        print("[Erro] A ferramenta 'wit' não está instalada ou não está no PATH do sistema.")
        print("Instale o Wiimms ISO Tools (https://wit.wiimm.de/) para prosseguir com a extração automática.")
        return

    # Cria os diretórios de destino se não existirem
    output_dir.mkdir(parents=True, exist_ok=True)
    sys_dir.mkdir(parents=True, exist_ok=True)

    print(f"[2/3] Extraindo arquivos da ISO para {output_dir}...")
    # O comando 'wit extract' extrai o conteúdo da partição de dados do GameCube
    temp_extract = root_dir / "iso_data" / "temp_extracted"
    
    try:
        subprocess.run(["wit", "extract", str(iso_path), str(temp_extract)], check=True)
        
        # O main.dol fica na pasta sys da ISO
        dol_source = temp_extract / "sys" / "main.dol"
        if dol_source.exists():
            import shutil
            shutil.copy(dol_source, sys_dir / "main.dol")
            print("[Info] main.dol copiado com sucesso para iso_data/sys/")

        # Os arquivos de jogo ficam na pasta files da ISO
        files_source = temp_extract / "files"
        if files_source.exists():
            # Aqui você pode copiar ou estruturar os arquivos para assets/files/
            print(f"[Info] Arquivos de recursos extraídos em {files_source}")
            print("[Info] Mova os recursos necessários para assets/files/ conforme o mapeamento do VFS.")

        print("[3/3] Limpeza de arquivos temporários...")
        # shutil.rmtree(temp_extract) # Descomente se quiser apagar a pasta temporária automaticamente

        print("\n[Sucesso] Extração concluída com sucesso!")

    except subprocess.CalledProcessError as e:
        print(f"[Erro] Falha ao executar o comando de extração: {e}")

if __name__ == "__main__":
    extract_sa2b_iso()