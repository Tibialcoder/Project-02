package com.example.sa2b;

import android.content.Intent;
import android.content.res.AssetManager;
import android.net.Uri;
import android.opengl.GLSurfaceView;
import android.os.Bundle;
import android.os.Environment;
import android.os.Handler;
import android.os.Looper;
import android.os.ParcelFileDescriptor;
import android.util.Log;
import android.view.MotionEvent;
import android.widget.Button;
import android.widget.TextView;
import androidx.activity.result.ActivityResultLauncher;
import androidx.activity.result.contract.ActivityResultContracts;
import androidx.appcompat.app.AppCompatActivity;
import java.io.File;
import java.util.concurrent.Executors;
import javax.microedition.khronos.egl.EGLConfig;
import javax.microedition.khronos.opengles.GL10;

public class MainActivity extends AppCompatActivity {
    static {
        // Carrega exatamente o nome da sua lib definida no CMakeLists.txt
        System.loadLibrary("sa2b_engine");
    }

    // Declarações JNI para o C++
    public static native void initAssetManager(AssetManager assetManager);
    public static native void nativeInit();
    public static native void nativeRender();
    public static native void nativeLoadISO(int fileDescriptor);
    public native boolean nativeExtractFile(int fileDescriptor, String fileName, String outputPath);
    public native boolean nativeExtractAll(int fileDescriptor, String outputDirPath);
    public native void nativeInitPipeline(String basePath);
    private GLSurfaceView glSurfaceView;
    private TextView txtProgress;
    private TextView txtCurrentFile;
    private File[] assetFiles;
    private int currentIndex = 0;
    private final Handler handler = new Handler(Looper.getMainLooper());
    private Runnable assetCyclerRunnable;
    private static final int PICK_ISO_REQUEST = 42;

    private final ActivityResultLauncher<Intent> folderPickerLauncher = registerForActivityResult(
        new ActivityResultContracts.StartActivityForResult(),
        result -> {
            if (result.getResultCode() == RESULT_OK && result.getData() != null) {
                Uri treeUri = result.getData().getData();
                if (treeUri != null) {
                    // Persiste a permissão para o app poder ler e escrever nessa pasta sempre que precisar
                    getContentResolver().takePersistableUriPermission(
                        treeUri,
                        Intent.FLAG_GRANT_READ_URI_PERMISSION | Intent.FLAG_GRANT_WRITE_URI_PERMISSION
                    );

                    // Aqui você pode salvar a Uri nas preferências ou iniciar a extração em massa
                    Log.i("SA2B_Modding", "Pasta selecionada com sucesso: " + treeUri);
                }
            }
        }
    );

    private final ActivityResultLauncher<String[]> isoPickerLauncher = registerForActivityResult(
        new ActivityResultContracts.OpenDocument(),
        uri -> {
            if (uri != null) {
                try {
                    // Obtém o FileDescriptor seguro da ISO selecionada
                    ParcelFileDescriptor pfd = getContentResolver().openFileDescriptor(uri, "r");
                    if (pfd != null) {
                        int fileDescriptor = pfd.getFd();

                        // Define o diretório Project-02 em Documents
                        File documentsDir = Environment.getExternalStoragePublicDirectory(Environment.DIRECTORY_DOCUMENTS);
                        File project02Dir = new File(documentsDir, "Project-02");
                        if (!project02Dir.exists()) {
                            project02Dir.mkdirs();
                        }

                        // Inicia a extração em background para não travar a tela
                        runExtraction(fileDescriptor, project02Dir.getAbsolutePath(), pfd);
                    }
                } catch (Exception e) {
                    Log.e("SA2B_ROM", "Erro ao abrir a ISO: " + e.getMessage());
                }
            }
        }
    );

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        // 1. Envia o gerenciador de assets do APK para o C++
        initAssetManager(getAssets());

        File documentsDir = Environment.getExternalStoragePublicDirectory(Environment.DIRECTORY_DOCUMENTS);
        File project02Dir = new File(documentsDir, "Project-02");

        // Processa arquivos pendentes em segundo plano sem bloquear a abertura da tela.
        new Thread(() -> nativeInitPipeline(project02Dir.getAbsolutePath()),
                "sa2b-init-pipeline").start();

        if (isFirstBoot(project02Dir)) {
            setContentView(R.layout.activity_main);

            // Vincula o botão do XML
            Button btnSelectIso = findViewById(R.id.btnSelectIso);
            btnSelectIso.setOnClickListener(v -> {
                // Abre o seletor de arquivos filtrando por qualquer tipo binário/genérico
                isoPickerLauncher.launch(new String[]{"*/*"});
            });
        } else {
            startGameEngine();
        }
    }

    // Método que o C++ vai chamar diretamente para atualizar a UI
    public void onExtractionProgress(int current, int total) {
        runOnUiThread(() -> {
            if (txtProgress != null) {
                int remaining = total - current;
                txtProgress.setText("Extraindo: " + current + " de " + total + " (Faltam: " + remaining + ")");
            }
        });
    }

    private boolean isFirstBoot(File projectDir) {
        if (!projectDir.exists()) return true;
        File[] files = projectDir.listFiles();
        return files == null || files.length == 0;
    }

    private void runExtraction(int fd, String outputDirPath, ParcelFileDescriptor pfd) {
        setContentView(R.layout.activity_splash);
        txtProgress = findViewById(R.id.txtProgress);

        Executors.newSingleThreadExecutor().execute(() -> {
            Log.i("SA2B_ROM", "Iniciando extração dos assets para a pasta Project-02...");

            boolean success = nativeExtractAll(fd, outputDirPath);

            try {
                pfd.close(); // Fecha o descritor após o uso
            } catch (Exception ignored) {}

            runOnUiThread(() -> {
                if (success) {
                    Log.i("SA2B_ROM", "Extração finalizada com sucesso! Inicializando o motor OpenGL ES...");

                    // 1. Cria a sua GLSurfaceView personalizada (ou a padrão do Android)
                    SA2BGLSurfaceView glSurfaceView = new SA2BGLSurfaceView(MainActivity.this);

                    // 2. Substitui o layout do botão/texto atual pelo motor gráfico em tela cheia
                    MainActivity.this.glSurfaceView = glSurfaceView;
                    setContentView(R.layout.activity_game);
                    MainActivity.this.glSurfaceView = findViewById(R.id.glSurfaceView);
                    initializeAssetCycle();

                } else {
                    Log.e("SA2B_ROM", "Falha na extração dos arquivos.");
                }
            });
        });
    }

    // GLSurfaceView personalizada para o motor SA2B
    public static class SA2BGLSurfaceView extends GLSurfaceView {
        public SA2BGLSurfaceView(android.content.Context context) {
            super(context);
            setEGLContextClientVersion(2);
            setRenderer(new SA2BRenderer());
        }
    }

    private void startGameEngine() {
        // Substitui a tela pela GLSurfaceView do motor OpenGL ES
        setContentView(R.layout.activity_game);
        glSurfaceView = findViewById(R.id.glSurfaceView);
        initializeAssetCycle();
    }

    private void initializeAssetCycle() {
        // O layout activity_game já foi carregado antes deste método ser chamado.
        txtCurrentFile = findViewById(R.id.txtCurrentFile);

        try {
            File documentsDir = Environment.getExternalStoragePublicDirectory(
                    Environment.DIRECTORY_DOCUMENTS);
            File modelsDir = new File(documentsDir, "Project-02/models");

            if (modelsDir.exists()) {
                assetFiles = modelsDir.listFiles();
            }

            if (assetFiles != null && assetFiles.length > 0) {
                startAssetCycle();
            } else if (txtCurrentFile != null) {
                txtCurrentFile.setText("Pasta encontrada, mas vazia ou sem acesso.");
            }
        } catch (Exception e) {
            Log.e("SA2B_DEBUG", "Erro ao carregar arquivos: " + e.getMessage(), e);
            if (txtCurrentFile != null) {
                txtCurrentFile.setText("Não foi possível acessar a pasta de assets.");
            }
        }
    }

    private void startAssetCycle() {
        assetCyclerRunnable = new Runnable() {
            @Override
            public void run() {
                if (assetFiles != null && assetFiles.length > 0) {
                    File currentFile = assetFiles[currentIndex];
                    txtCurrentFile.setText("Exibindo: " + currentFile.getName() + " ("
                            + (currentIndex + 1) + "/" + assetFiles.length + ")");
                    currentIndex = (currentIndex + 1) % assetFiles.length;
                }
                handler.postDelayed(this, 5000);
            }
        };
        handler.post(assetCyclerRunnable);
    }

    // Renderizador personalizado conectado ao pipeline C++
    public static class SA2BRenderer implements GLSurfaceView.Renderer {
        @Override
        public void onSurfaceCreated(GL10 gl, EGLConfig config) {
            nativeInit();
        }

        @Override
        public void onSurfaceChanged(GL10 gl, int width, int height) {
            // Ajustes de viewport quando a superfície mudar
        }

        @Override
        public void onDrawFrame(GL10 gl) {
            nativeRender();
        }
    }

    // Ao tocar na tela, abre o seletor de arquivos para o usuário escolher a ISO do jogo
    @Override
    public boolean onTouchEvent(MotionEvent event) {
        if (event.getAction() == MotionEvent.ACTION_DOWN) {
            abrirSeletorDeIso();
            return true;
        }
        return super.onTouchEvent(event);
    }

    private void abrirSeletorDeIso() {
        Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        intent.addCategory(Intent.CATEGORY_OPENABLE);
        intent.setType("*/*"); // Você pode usar "application/octet-stream" se preferir
        startActivityForResult(intent, PICK_ISO_REQUEST);
    }

    // Chame este método quando quiser que o usuário escolha a pasta (ex: ao clicar num botão "Selecionar Pasta de Mods"):
    public void openFolderSelector() {
        Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE);
        folderPickerLauncher.launch(intent);
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        android.util.Log.d("SA2B_Java", "onActivityResult chamado. ResultCode: " + resultCode);

        if (requestCode == PICK_ISO_REQUEST && resultCode == RESULT_OK && data != null) {
            Uri uri = data.getData();
            android.util.Log.d("SA2B_Java", "URI do arquivo selecionado: " + (uri != null ? uri.toString() : "null"));

            try {
                // Exemplo usando o FileDescriptor que você já pegou da ISO
                ParcelFileDescriptor pfd = getContentResolver().openFileDescriptor(uri, "r");
                if (pfd != null) {
                    int fd = pfd.getFd();

                    // 1. Primeiro você pode chamar o carregamento geral da ISO
                    nativeLoadISO(fd);

                    // Cria a pasta Project-02 dentro do diretório público Documents do dispositivo
                    File documentsDir = Environment.getExternalStoragePublicDirectory(Environment.DIRECTORY_DOCUMENTS);
                    File project02Dir = new File(documentsDir, "Project-02");

                    if (!project02Dir.exists()) {
                        boolean created = project02Dir.mkdirs();
                        if (created) {
                            Log.i("SA2B_Modding", "Pasta Project-02 criada com sucesso em Documents!");
                        }
                    }

                    // O caminho completo que será passado para o C++ extrair os arquivos
                    String outputDirPath = project02Dir.getAbsolutePath();

                    // 2. Agora você pode extrair qualquer arquivo de dentro da ISO para a pasta Project-02!
                    File outputFile = new File(project02Dir, "alphaTitle.prs");
                    boolean extracted = nativeExtractFile(fd, "alphaTitle.prs", outputFile.getAbsolutePath());

                    if (extracted) {
                        Log.i("SA2B", "Arquivo extraído com sucesso para: " + outputFile.getAbsolutePath());
                    }

                    pfd.close();
                } else {
                    Log.e("SA2B_Java", "ParcelFileDescriptor é NULL!");
                }
            } catch (Exception e) {
                e.printStackTrace();
            }
        } else {
            android.util.Log.w("SA2B_Java", "Seleção de arquivo cancelada ou dados nulos.");
        }
    }

    @Override
    protected void onResume() {
        super.onResume();
        if (glSurfaceView != null) glSurfaceView.onResume();
    }

    @Override
    protected void onPause() {
        super.onPause();
        if (glSurfaceView != null) glSurfaceView.onPause();
    }

    @Override
    protected void onDestroy() {
        super.onDestroy();
        if (assetCyclerRunnable != null) {
            handler.removeCallbacks(assetCyclerRunnable);
        }
    }
}